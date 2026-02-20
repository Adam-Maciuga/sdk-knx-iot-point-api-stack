// =============================================================================
// dns-sd Drop-in Replacement CLI
// =============================================================================
// A robust, Bonjour-compatible mDNS service registration tool using 
// MeaMod.DNS.Multicast (fork of Makaretu.Dns.Multicast).
//
// This implementation is designed as a drop-in replacement for Apple's dns-sd
// command-line tool, specifically supporting the -R (register) command used
// by KNX IoT applications.
//
// Compatible with command format:
//   dns-sd -R <Name> <Type> <Domain> <Port> [<TXT>...]
//
// Example of Use:
//   dns-sd -R "00FA00112233" "_knx._udp,_ia12345678-1a,_pm,_00FA00112233" "local" "5683" "SP=30"
//
// =============================================================================

using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using MeaMod.DNS.Model;
using MeaMod.DNS.Multicast;

namespace DnsSdResponder;

/// <summary>
/// Main entry point for the dns-sd drop-in replacement CLI.
/// </summary>
internal static class Program
{
    // =========================================================================
    // Constants
    // =========================================================================
    
    /// <summary>Version string for display.</summary>
    private const string Version = "1.0.0";
    
    /// <summary>Default mDNS domain.</summary>
    private const string DefaultDomain = "local";
    
    /// <summary>Default TTL for DNS records in seconds.</summary>
    private const int DefaultTtlSeconds = 4500;
    
    /// <summary>Interval between re-announcements in milliseconds.</summary>
    private const int ReannounceIntervalMs = 60000; // 1 minute
    
    /// <summary>Maximum retry attempts for network operations.</summary>
    private const int MaxNetworkRetries = 5;
    
    /// <summary>Delay between network retry attempts in milliseconds.</summary>
    private const int NetworkRetryDelayMs = 1000;
    
    /// <summary>Startup delay to allow network interfaces to stabilize.</summary>
    private const int StartupDelayMs = 500;

    // =========================================================================
    // Exit Codes (matching Bonjour dns-sd behavior)
    // =========================================================================
    
    private const int ExitSuccess = 0;
    private const int ExitInvalidArguments = 1;
    private const int ExitNetworkError = 2;
    private const int ExitServiceError = 3;
    private const int ExitInterrupted = 4;

    // =========================================================================
    // Main Entry Point
    // =========================================================================
    
    /// <summary>
    /// Application entry point.
    /// </summary>
    /// <param name="args">Command-line arguments.</param>
    /// <returns>Exit code.</returns>
    public static async Task<int> Main(string[] args)
    {
        // Setup console for proper signal handling
        ConfigureConsole();
        
        // Parse and validate command-line arguments
        var parseResult = CommandLineParser.Parse(args);
        
        if (parseResult.ShowHelp)
        {
            PrintUsage();
            return ExitSuccess;
        }
        
        if (parseResult.ShowVersion)
        {
            PrintVersion();
            return ExitSuccess;
        }
        
        if (!parseResult.IsValid)
        {
            Logger.Error(parseResult.ErrorMessage ?? "Invalid arguments");
            PrintUsage();
            return ExitInvalidArguments;
        }
        
        // Execute the requested command
        return parseResult.Command switch
        {
            DnsSdCommand.Register => await ExecuteRegisterCommandAsync(parseResult.RegisterOptions!),
            _ => ExitInvalidArguments
        };
    }

    // =========================================================================
    // Command Execution
    // =========================================================================
    
    /// <summary>
    /// Executes the -R (register) command to advertise a service.
    /// </summary>
    /// <param name="options">Registration options.</param>
    /// <returns>Exit code.</returns>
    private static async Task<int> ExecuteRegisterCommandAsync(RegisterOptions options)
    {
        Logger.Info($"dns-sd {Version} - mDNS Service Registration");
        Logger.Info(new string('=', 60));
        
        // Log configuration
        LogRegistrationDetails(options);
        
        // Create cancellation token for graceful shutdown
        using var cts = new CancellationTokenSource();
        
        // Setup signal handlers for graceful shutdown
        SetupSignalHandlers(cts);
        
        // Create and configure the mDNS responder
        MdnsResponder? responder = null;
        
        try
        {
            // Wait briefly for network interfaces to stabilize
            await Task.Delay(StartupDelayMs, cts.Token);
            
            // Initialize the responder with retry logic
            responder = await InitializeResponderWithRetryAsync(options, cts.Token);
            
            if (responder == null)
            {
                Logger.Error("Failed to initialize mDNS responder after multiple attempts");
                return ExitNetworkError;
            }
            
            // Start the responder
            await responder.StartAsync(cts.Token);
            
            // Print registration confirmation (matches Bonjour output format)
            PrintRegistrationConfirmation(options);
            
            // Run until cancellation
            await responder.RunAsync(cts.Token);
            
            return ExitSuccess;
        }
        catch (OperationCanceledException)
        {
            Logger.Info("Service registration cancelled");
            return ExitInterrupted;
        }
        catch (Exception ex)
        {
            Logger.Error($"Unexpected error: {ex.Message}");
            Logger.Debug($"Stack trace: {ex.StackTrace}");
            return ExitServiceError;
        }
        finally
        {
            // Ensure cleanup
            if (responder != null)
            {
                await responder.StopAsync();
                responder.Dispose();
            }
            
            Logger.Info("Service unregistered");
        }
    }

    // =========================================================================
    // Initialization Helpers
    // =========================================================================
    
    /// <summary>
    /// Initializes the mDNS responder with retry logic for network failures.
    /// </summary>
    private static async Task<MdnsResponder?> InitializeResponderWithRetryAsync(
        RegisterOptions options,
        CancellationToken cancellationToken)
    {
        for (int attempt = 1; attempt <= MaxNetworkRetries; attempt++)
        {
            try
            {
                Logger.Debug($"Initializing mDNS responder (attempt {attempt}/{MaxNetworkRetries})...");
                
                var responder = new MdnsResponder(options);
                
                // Verify network interfaces are available
                if (!NetworkUtilities.HasValidNetworkInterfaces())
                {
                    Logger.Warning("No valid network interfaces found, waiting...");
                    await Task.Delay(NetworkRetryDelayMs, cancellationToken);
                    continue;
                }
                
                return responder;
            }
            catch (SocketException ex)
            {
                Logger.Warning($"Network error during initialization: {ex.Message}");
                
                if (attempt < MaxNetworkRetries)
                {
                    Logger.Info($"Retrying in {NetworkRetryDelayMs}ms...");
                    await Task.Delay(NetworkRetryDelayMs, cancellationToken);
                }
            }
            catch (Exception ex)
            {
                Logger.Error($"Failed to initialize responder: {ex.Message}");
                throw;
            }
        }
        
        return null;
    }

    // =========================================================================
    // Signal Handling
    // =========================================================================
    
    /// <summary>
    /// Configures the console for proper operation.
    /// </summary>
    private static void ConfigureConsole()
    {
        try
        {
            // Set UTF-8 encoding for proper character display
            Console.OutputEncoding = Encoding.UTF8;
            Console.InputEncoding = Encoding.UTF8;
        }
        catch
        {
            // Ignore encoding errors on platforms that don't support it
        }
    }
    
    /// <summary>
    /// Sets up signal handlers for graceful shutdown.
    /// </summary>
    private static void SetupSignalHandlers(CancellationTokenSource cts)
    {
        // Handle Ctrl+C and Ctrl+Break
        Console.CancelKeyPress += (sender, e) =>
        {
            e.Cancel = true; // Prevent immediate termination
            Logger.Info("");
            Logger.Info("Received shutdown signal, stopping service...");
            cts.Cancel();
        };
        
        // Handle process termination on supported platforms
        AppDomain.CurrentDomain.ProcessExit += (sender, e) =>
        {
            if (!cts.IsCancellationRequested)
            {
                Logger.Debug("Process exit detected, cleaning up...");
                cts.Cancel();
            }
        };
        
        // Handle unhandled exceptions
        AppDomain.CurrentDomain.UnhandledException += (sender, e) =>
        {
            if (e.ExceptionObject is Exception ex)
            {
                Logger.Error($"Unhandled exception: {ex.Message}");
                Logger.Debug($"Stack trace: {ex.StackTrace}");
            }
            cts.Cancel();
        };
    }

    // =========================================================================
    // Output Helpers
    // =========================================================================
    
    /// <summary>
    /// Logs the registration details.
    /// </summary>
    private static void LogRegistrationDetails(RegisterOptions options)
    {
        Logger.Info($"Instance Name: {options.InstanceName}");
        Logger.Info($"Service Type:  {options.ServiceType}");
        Logger.Info($"Domain:        {options.Domain}");
        Logger.Info($"Port:          {options.Port}");
        
        if (options.Subtypes.Count > 0)
        {
            Logger.Info($"Subtypes:      {string.Join(", ", options.Subtypes)}");
        }
        
        if (options.TxtRecords.Count > 0)
        {
            Logger.Info($"TXT Records:   {string.Join(", ", options.TxtRecords.Select(kv => $"{kv.Key}={kv.Value}"))}");
        }
        
        Logger.Info(new string('=', 60));
    }
    
    /// <summary>
    /// Prints the registration confirmation in Bonjour-compatible format.
    /// </summary>
    private static void PrintRegistrationConfirmation(RegisterOptions options)
    {
        // Match Bonjour dns-sd output format
        Console.WriteLine();
        Console.WriteLine($"DATE: {DateTime.Now:ddd MMM dd HH:mm:ss yyyy}");
        Console.WriteLine($"ServiceID {Guid.NewGuid():N}");
        Console.WriteLine($"Registering Service {options.InstanceName}.{options.ServiceType}.{options.Domain}. port {options.Port}");
        Console.WriteLine($" Host: {NetworkUtilities.GetHostName()}.{options.Domain}.");
        
        if (options.TxtRecords.Count > 0)
        {
            var txtDisplay = string.Join(" ", options.TxtRecords.Select(kv => $"{kv.Key}={kv.Value}"));
            Console.WriteLine($" TXT: {txtDisplay}");
        }
        
        Console.WriteLine();
        Console.WriteLine("Press Ctrl+C to stop...");
        Console.WriteLine();
    }
    
    /// <summary>
    /// Prints usage information.
    /// </summary>
    private static void PrintUsage()
    {
        Console.WriteLine($"dns-sd {Version} - Bonjour-compatible mDNS Service Registration Tool");
        Console.WriteLine();
        Console.WriteLine("Usage: dns-sd -R <Name> <Type> <Domain> <Port> [<TXT>...]");
        Console.WriteLine();
        Console.WriteLine("Commands:");
        Console.WriteLine("  -R, --register    Register (advertise) a service on the network");
        Console.WriteLine();
        Console.WriteLine("Arguments:");
        Console.WriteLine("  Name              Service instance name (e.g., \"MyDevice\")");
        Console.WriteLine("  Type              Service type with optional subtypes, comma-separated");
        Console.WriteLine("                    Format: <type>,<subtype1>,<subtype2>,...");
        Console.WriteLine("                    Example: _knx._udp,_ia12345678-1a,_pm,_ABC123");
        Console.WriteLine("  Domain            mDNS domain (typically \"local\")");
        Console.WriteLine("  Port              Service port number (1-65535)");
        Console.WriteLine("  TXT               Optional TXT record entries (e.g., \"SP=30\")");
        Console.WriteLine();
        Console.WriteLine("Options:");
        Console.WriteLine("  -h, --help        Show this help message");
        Console.WriteLine("  -v, --version     Show version information");
        Console.WriteLine("  -d, --debug       Enable debug output");
        Console.WriteLine();
        Console.WriteLine("Examples:");
        Console.WriteLine("  dns-sd -R \"MyKnxDevice\" \"_knx._udp\" \"local\" 5683");
        Console.WriteLine("  dns-sd -R \"00FA00112233\" \"_knx._udp,_ia12345678-1a,_pm\" \"local\" 5683 \"SP=30\"");
        Console.WriteLine();
        Console.WriteLine("Exit Codes:");
        Console.WriteLine("  0    Success");
        Console.WriteLine("  1    Invalid arguments");
        Console.WriteLine("  2    Network error");
        Console.WriteLine("  3    Service error");
        Console.WriteLine("  4    Interrupted");
    }
    
    /// <summary>
    /// Prints version information.
    /// </summary>
    private static void PrintVersion()
    {
        Console.WriteLine($"dns-sd version {Version}");
        Console.WriteLine($"Runtime: {RuntimeInformation.FrameworkDescription}");
        Console.WriteLine($"OS: {RuntimeInformation.OSDescription}");
        Console.WriteLine($"Architecture: {RuntimeInformation.ProcessArchitecture}");
    }
}

// =============================================================================
// Command Line Parser
// =============================================================================

/// <summary>
/// Supported dns-sd commands.
/// </summary>
internal enum DnsSdCommand
{
    None,
    Register
}

/// <summary>
/// Options for the -R (register) command.
/// </summary>
internal sealed class RegisterOptions
{
    /// <summary>Service instance name.</summary>
    public required string InstanceName { get; init; }
    
    /// <summary>Service type (e.g., "_knx._udp").</summary>
    public required string ServiceType { get; init; }
    
    /// <summary>mDNS domain (typically "local").</summary>
    public required string Domain { get; init; }
    
    /// <summary>Service port number.</summary>
    public required ushort Port { get; init; }
    
    /// <summary>Service subtypes.</summary>
    public required IReadOnlyList<string> Subtypes { get; init; }
    
    /// <summary>TXT record key-value pairs.</summary>
    public required IReadOnlyDictionary<string, string> TxtRecords { get; init; }
}

/// <summary>
/// Result of command-line parsing.
/// </summary>
internal sealed class ParseResult
{
    public bool IsValid { get; init; }
    public bool ShowHelp { get; init; }
    public bool ShowVersion { get; init; }
    public DnsSdCommand Command { get; init; }
    public RegisterOptions? RegisterOptions { get; init; }
    public string? ErrorMessage { get; init; }
    
    public static ParseResult Help() => new() { IsValid = true, ShowHelp = true };
    public static ParseResult VersionInfo() => new() { IsValid = true, ShowVersion = true };
    public static ParseResult Error(string message) => new() { IsValid = false, ErrorMessage = message };
    public static ParseResult Success(RegisterOptions options) => 
        new() { IsValid = true, Command = DnsSdCommand.Register, RegisterOptions = options };
}

/// <summary>
/// Parses command-line arguments into structured options.
/// </summary>
internal static class CommandLineParser
{
    // Service type validation pattern: _<name>._<protocol>
    private static readonly Regex ServiceTypePattern = new(
        @"^_[a-zA-Z0-9]([a-zA-Z0-9\-]*[a-zA-Z0-9])?\._(tcp|udp)$",
        RegexOptions.Compiled | RegexOptions.IgnoreCase);
    
    // Subtype validation pattern: _<name>
    private static readonly Regex SubtypePattern = new(
        @"^_[a-zA-Z0-9][a-zA-Z0-9\-]*$",
        RegexOptions.Compiled | RegexOptions.IgnoreCase);
    
    /// <summary>
    /// Parses command-line arguments.
    /// </summary>
    /// <param name="args">Command-line arguments.</param>
    /// <returns>Parse result.</returns>
    public static ParseResult Parse(string[] args)
    {
        if (args.Length == 0)
        {
            return ParseResult.Error("No arguments provided");
        }
        
        // Check for help/version flags first
        if (args.Any(a => a is "-h" or "--help" or "-?" or "/?"))
        {
            return ParseResult.Help();
        }
        
        if (args.Any(a => a is "-v" or "--version"))
        {
            return ParseResult.VersionInfo();
        }
        
        // Check for debug mode
        bool debugMode = args.Any(a => a is "-d" or "--debug");
        if (debugMode)
        {
            Logger.EnableDebug();
            args = args.Where(a => a is not "-d" and not "--debug").ToArray();
        }
        
        // Parse command
        string command = args[0].ToLowerInvariant();
        
        return command switch
        {
            "-r" or "--register" => ParseRegisterCommand(args),
            _ => ParseResult.Error($"Unknown command: {args[0]}. Use -R to register a service.")
        };
    }
    
    /// <summary>
    /// Parses the -R (register) command arguments.
    /// </summary>
    private static ParseResult ParseRegisterCommand(string[] args)
    {
        // Minimum required: -R <Name> <Type> <Domain> <Port>
        if (args.Length < 5)
        {
            return ParseResult.Error("Insufficient arguments for -R command. Required: <Name> <Type> <Domain> <Port>");
        }
        
        // Parse instance name
        string instanceName = args[1].Trim().Trim('"');
        if (string.IsNullOrWhiteSpace(instanceName))
        {
            return ParseResult.Error("Instance name cannot be empty");
        }
        
        if (instanceName.Length > 63)
        {
            return ParseResult.Error("Instance name exceeds maximum length of 63 characters");
        }
        
        // Parse service type and subtypes
        string typeWithSubtypes = args[2].Trim().Trim('"');
        var parseTypeResult = ParseServiceTypeAndSubtypes(typeWithSubtypes);
        
        if (!parseTypeResult.IsValid)
        {
            return ParseResult.Error(parseTypeResult.ErrorMessage!);
        }
        
        // Parse domain
        string domain = args[3].Trim().Trim('"').TrimEnd('.').ToLowerInvariant();
        if (string.IsNullOrWhiteSpace(domain))
        {
            domain = "local";
        }
        
        if (!IsValidDomain(domain))
        {
            return ParseResult.Error($"Invalid domain: {domain}");
        }
        
        // Parse port
        string portStr = args[4].Trim().Trim('"');
        if (!ushort.TryParse(portStr, out ushort port) || port == 0)
        {
            return ParseResult.Error($"Invalid port number: {portStr}. Must be 1-65535.");
        }
        
        // Parse TXT records (remaining arguments)
        var txtRecords = ParseTxtRecords(args.Skip(5));
        
        var options = new RegisterOptions
        {
            InstanceName = instanceName,
            ServiceType = parseTypeResult.ServiceType!,
            Domain = domain,
            Port = port,
            Subtypes = parseTypeResult.Subtypes!,
            TxtRecords = txtRecords
        };
        
        return ParseResult.Success(options);
    }
    
    /// <summary>
    /// Parses service type and subtypes from comma-separated string.
    /// </summary>
    private static (bool IsValid, string? ServiceType, List<string>? Subtypes, string? ErrorMessage) 
        ParseServiceTypeAndSubtypes(string typeWithSubtypes)
    {
        if (string.IsNullOrWhiteSpace(typeWithSubtypes))
        {
            return (false, null, null, "Service type cannot be empty");
        }
        
        var parts = typeWithSubtypes.Split(',', StringSplitOptions.RemoveEmptyEntries)
            .Select(p => p.Trim())
            .ToList();
        
        if (parts.Count == 0)
        {
            return (false, null, null, "Service type cannot be empty");
        }
        
        string serviceType = parts[0];
        
        // Validate service type format
        if (!ServiceTypePattern.IsMatch(serviceType))
        {
            // Allow relaxed validation for compatibility
            Logger.Warning($"Service type '{serviceType}' may not be RFC 6763 compliant");
        }
        
        // Parse subtypes
        var subtypes = new List<string>();
        foreach (var subtype in parts.Skip(1))
        {
            if (string.IsNullOrWhiteSpace(subtype))
                continue;
            
            // Validate subtype format (should start with _)
            string normalizedSubtype = subtype.StartsWith('_') ? subtype : $"_{subtype}";
            
            if (!SubtypePattern.IsMatch(normalizedSubtype))
            {
                Logger.Warning($"Subtype '{subtype}' may not be RFC 6763 compliant");
            }
            
            subtypes.Add(normalizedSubtype);
        }
        
        return (true, serviceType, subtypes, null);
    }
    
    /// <summary>
    /// Parses TXT record arguments.
    /// </summary>
    private static Dictionary<string, string> ParseTxtRecords(IEnumerable<string> args)
    {
        var records = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        
        foreach (var arg in args)
        {
            string txt = arg.Trim().Trim('"');
            
            if (string.IsNullOrWhiteSpace(txt))
                continue;
            
            int eqIndex = txt.IndexOf('=');
            
            if (eqIndex > 0)
            {
                // Key=Value format
                string key = txt[..eqIndex].Trim();
                string value = txt[(eqIndex + 1)..].Trim();
                
                if (!string.IsNullOrEmpty(key))
                {
                    // Validate key length (max 9 characters recommended by RFC 6763)
                    if (key.Length > 9)
                    {
                        Logger.Warning($"TXT key '{key}' exceeds recommended maximum of 9 characters");
                    }
                    
                    records[key] = value;
                }
            }
            else if (eqIndex == 0)
            {
                // =Value format (invalid, skip)
                Logger.Warning($"Invalid TXT record format: {txt}");
            }
            else
            {
                // Boolean key (no value)
                string key = txt.Trim();
                if (!string.IsNullOrEmpty(key))
                {
                    records[key] = string.Empty;
                }
            }
        }
        
        return records;
    }
    
    /// <summary>
    /// Validates a domain name.
    /// </summary>
    private static bool IsValidDomain(string domain)
    {
        if (string.IsNullOrWhiteSpace(domain))
            return false;
        
        // Allow "local" and standard domain names
        if (domain == "local")
            return true;
        
        // Basic domain validation
        return domain.All(c => char.IsLetterOrDigit(c) || c == '.' || c == '-');
    }
}

// =============================================================================
// mDNS Responder
// =============================================================================

/// <summary>
/// Manages mDNS service registration and advertisement.
/// </summary>
internal sealed class MdnsResponder : IDisposable
{
    private readonly RegisterOptions _options;
    private readonly MulticastService _mdns;
    private readonly ServiceDiscovery _serviceDiscovery;
    private readonly ServiceProfile _serviceProfile;
    private readonly object _lock = new();
    
    private bool _isRunning;
    private bool _isDisposed;
    private Timer? _reannounceTimer;
    private Timer? _networkMonitorTimer;
    private int _announcementCount;
    private readonly List<PTRRecord> _subtypePtrRecords = new();

    /// <summary>
    /// Creates a new mDNS responder instance.
    /// </summary>
    /// <param name="options">Registration options.</param>
    public MdnsResponder(RegisterOptions options)
    {
        _options = options ?? throw new ArgumentNullException(nameof(options));
        
        // Create multicast service with error handling
        _mdns = new MulticastService();
        ConfigureMulticastService();
        
        // Create service discovery
        _serviceDiscovery = new ServiceDiscovery(_mdns);
        
        // Create service profile
        _serviceProfile = CreateServiceProfile();
        
        Logger.Debug("mDNS responder initialized");
    }
    
    /// <summary>
    /// Configures the multicast service with event handlers.
    /// </summary>
    private void ConfigureMulticastService()
    {
        // Handle network interface changes
        _mdns.NetworkInterfaceDiscovered += (sender, args) =>
        {
            foreach (var nic in args.NetworkInterfaces)
            {
                Logger.Debug($"Network interface discovered: {nic.Name} ({nic.Id})");
            }
        };

        // Handle query received (for debugging)
        // Handle query received - respond to subtype queries
        _mdns.QueryReceived += (sender, args) =>
        {
            foreach (var question in args.Message.Questions)
            {
                Logger.Debug($"Query received: {question.Name} ({question.Type})");

                // Check if this is a query for one of our subtypes
                if (question.Type == DnsType.PTR || question.Type == DnsType.ANY)
                {
                    var matchingPtr = _subtypePtrRecords.FirstOrDefault(ptr =>
                        ptr.Name.ToString().Equals(question.Name.ToString(), StringComparison.OrdinalIgnoreCase));

                    if (matchingPtr != null)
                    {
                        Logger.Debug($"Responding to subtype query: {question.Name}");

                        var response = new Message
                        {
                            Id = args.Message.Id,
                            QR = true, // This is a response
                            AA = true  // Authoritative answer
                        };

                        response.Answers.Add(matchingPtr);

                        // Add additional records for the service
                        AddServiceRecordsToResponse(response);

                        _mdns.SendAnswer(response);
                    }
                }
            }
        };

        // Handle answer received (for debugging)
        _mdns.AnswerReceived += (sender, args) =>
        {
            foreach (var answer in args.Message.Answers)
            {
                Logger.Debug($"Answer received: {answer.Name} ({answer.Type})");
            }
        };
        
        // Handle malformed messages
        _mdns.MalformedMessage += (sender, data) =>
        {
            Logger.Warning($"Malformed mDNS message received ({data.Length} bytes)");
        };
    }

    /// <summary>
    /// Adds service records (SRV, TXT, A/AAAA) to a response message.
    /// </summary>
    private void AddServiceRecordsToResponse(Message response)
    {
        foreach (var resource in _serviceProfile.Resources)
        {
            // Add SRV, TXT, A, AAAA records as additional records
            if (resource.Type == DnsType.SRV ||
                resource.Type == DnsType.TXT ||
                resource.Type == DnsType.A ||
                resource.Type == DnsType.AAAA)
            {
                if (!response.AdditionalRecords.Any(r => r.Name.Equals(resource.Name) && r.Type == resource.Type))
                {
                    response.AdditionalRecords.Add(resource);
                }
            }
        }
    }

    /// <summary>
    /// Creates the service profile for advertisement.
    /// </summary>
    private ServiceProfile CreateServiceProfile()
    {
        var profile = new ServiceProfile(
            instanceName: _options.InstanceName,
            serviceName: _options.ServiceType,
            port: _options.Port
        );
        
        // Add host addresses
        AddHostAddresses(profile);

        // Add subtypes in Bonjour-compatible format
        foreach (var subtype in _options.Subtypes)
        {
            // Format: <subtype>._sub.<servicetype>.local
            var subtypeName = DomainName.Join(subtype, "_sub", profile.QualifiedServiceName);
            profile.Subtypes.Add(subtypeName.ToString());
            Logger.Debug($"Added subtype: {subtypeName}");

            // Create PTR record for subtype queries: _pm._sub._knx._udp.local -> ABC123._knx._udp.local
            var ptrRecord = new PTRRecord
            {
                Name = subtypeName,
                DomainName = profile.FullyQualifiedName,
                TTL = TimeSpan.FromSeconds(4500),
                Class = DnsClass.IN
            };
            _subtypePtrRecords.Add(ptrRecord);
            Logger.Debug($"Created subtype PTR: {subtypeName} -> {profile.FullyQualifiedName}");
        }

        // Add TXT records
        foreach (var (key, value) in _options.TxtRecords)
        {
            profile.AddProperty(key, value);
            Logger.Debug($"Added TXT record: {key}={value}");
        }
        
        return profile;
    }
    
    /// <summary>
    /// Adds host addresses to the service profile.
    /// </summary>
    private static void AddHostAddresses(ServiceProfile profile)
    {
        try
        {
            var addresses = NetworkUtilities.GetLocalAddresses();
            
            foreach (var address in addresses)
            {
                profile.AddProperty("addr", address.ToString());
                Logger.Debug($"Added address: {address}");
            }
        }
        catch (Exception ex)
        {
            Logger.Warning($"Failed to enumerate network addresses: {ex.Message}");
        }
    }
    
    /// <summary>
    /// Starts the mDNS responder.
    /// </summary>
    public async Task StartAsync(CancellationToken cancellationToken)
    {
        lock (_lock)
        {
            if (_isRunning)
            {
                Logger.Warning("mDNS responder is already running");
                return;
            }
            
            if (_isDisposed)
            {
                throw new ObjectDisposedException(nameof(MdnsResponder));
            }
            
            _isRunning = true;
        }
        
        Logger.Debug("Starting mDNS responder...");
        
        try
        {
            // Start the multicast service
            _mdns.Start();
            
            // Wait briefly for multicast to initialize
            await Task.Delay(100, cancellationToken);
            
            // Advertise the service
            _serviceDiscovery.Advertise(_serviceProfile);
            _announcementCount = 1;
            
            Logger.Debug($"Service advertised: {_serviceProfile.FullyQualifiedName}");
            
            // Setup periodic re-announcement
            SetupReannounceTimer();
            
            // Setup network monitoring
            SetupNetworkMonitoring();
        }
        catch (Exception ex)
        {
            lock (_lock)
            {
                _isRunning = false;
            }
            
            Logger.Error($"Failed to start mDNS responder: {ex.Message}");
            throw;
        }
    }
    
    /// <summary>
    /// Runs the responder until cancellation.
    /// </summary>
    public async Task RunAsync(CancellationToken cancellationToken)
    {
        Logger.Debug("mDNS responder running, waiting for cancellation...");
        
        try
        {
            // Wait indefinitely until cancelled
            await Task.Delay(Timeout.Infinite, cancellationToken);
        }
        catch (OperationCanceledException)
        {
            Logger.Debug("Cancellation requested");
        }
    }
    
    /// <summary>
    /// Stops the mDNS responder.
    /// </summary>
    public async Task StopAsync()
    {
        lock (_lock)
        {
            if (!_isRunning)
            {
                return;
            }
            
            _isRunning = false;
        }
        
        Logger.Debug("Stopping mDNS responder...");
        
        // Stop timers
        _reannounceTimer?.Dispose();
        _reannounceTimer = null;
        
        _networkMonitorTimer?.Dispose();
        _networkMonitorTimer = null;
        
        try
        {
            // Unadvertise the service (sends goodbye packets)
            _serviceDiscovery.Unadvertise(_serviceProfile);
            
            // Allow time for goodbye packets to be sent
            await Task.Delay(500);
        }
        catch (Exception ex)
        {
            Logger.Warning($"Error during service unadvertisement: {ex.Message}");
        }
        
        try
        {
            // Stop multicast service
            _mdns.Stop();
        }
        catch (Exception ex)
        {
            Logger.Warning($"Error stopping multicast service: {ex.Message}");
        }
        
        Logger.Debug("mDNS responder stopped");
    }
    
    /// <summary>
    /// Sets up periodic service re-announcement.
    /// </summary>
    private void SetupReannounceTimer()
    {
        _reannounceTimer = new Timer(
            callback: _ => ReannounceService(),
            state: null,
            dueTime: 60000, // First re-announce after 1 minute
            period: 60000   // Then every minute
        );
    }
    
    /// <summary>
    /// Re-announces the service.
    /// </summary>
    private void ReannounceService()
    {
        if (!_isRunning || _isDisposed)
            return;
        
        try
        {
            _serviceDiscovery.Announce(_serviceProfile);
            _announcementCount++;
            Logger.Debug($"Service re-announced (count: {_announcementCount})");
        }
        catch (Exception ex)
        {
            Logger.Warning($"Failed to re-announce service: {ex.Message}");
        }
    }
    
    /// <summary>
    /// Sets up network change monitoring.
    /// </summary>
    private void SetupNetworkMonitoring()
    {
        // Monitor for network address changes
        NetworkChange.NetworkAddressChanged += OnNetworkAddressChanged;
        NetworkChange.NetworkAvailabilityChanged += OnNetworkAvailabilityChanged;
        
        // Periodic network check as backup
        _networkMonitorTimer = new Timer(
            callback: _ => CheckNetworkHealth(),
            state: null,
            dueTime: 30000, // First check after 30 seconds
            period: 30000   // Then every 30 seconds
        );
    }
    
    /// <summary>
    /// Handles network address changes.
    /// </summary>
    private void OnNetworkAddressChanged(object? sender, EventArgs e)
    {
        if (!_isRunning || _isDisposed)
            return;
        
        Logger.Info("Network address change detected, re-announcing service...");
        
        try
        {
            ReannounceService();
        }
        catch (Exception ex)
        {
            Logger.Warning($"Error handling network change: {ex.Message}");
        }
    }
    
    /// <summary>
    /// Handles network availability changes.
    /// </summary>
    private void OnNetworkAvailabilityChanged(object? sender, NetworkAvailabilityEventArgs e)
    {
        if (_isDisposed)
            return;
        
        if (e.IsAvailable)
        {
            Logger.Info("Network became available, re-announcing service...");
            
            if (_isRunning)
            {
                try
                {
                    ReannounceService();
                }
                catch (Exception ex)
                {
                    Logger.Warning($"Error handling network availability: {ex.Message}");
                }
            }
        }
        else
        {
            Logger.Warning("Network became unavailable");
        }
    }
    
    /// <summary>
    /// Performs periodic network health check.
    /// </summary>
    private void CheckNetworkHealth()
    {
        if (!_isRunning || _isDisposed)
            return;
        
        try
        {
            if (!NetworkUtilities.HasValidNetworkInterfaces())
            {
                Logger.Warning("No valid network interfaces detected");
            }
        }
        catch (Exception ex)
        {
            Logger.Debug($"Network health check error: {ex.Message}");
        }
    }
    
    /// <summary>
    /// Disposes the responder.
    /// </summary>
    public void Dispose()
    {
        if (_isDisposed)
            return;
        
        _isDisposed = true;
        
        // Unsubscribe from events
        NetworkChange.NetworkAddressChanged -= OnNetworkAddressChanged;
        NetworkChange.NetworkAvailabilityChanged -= OnNetworkAvailabilityChanged;
        
        // Dispose timers
        _reannounceTimer?.Dispose();
        _networkMonitorTimer?.Dispose();
        
        // Dispose services
        _serviceDiscovery.Dispose();
        _mdns.Dispose();
        
        Logger.Debug("mDNS responder disposed");
    }
}

// =============================================================================
// Network Utilities
// =============================================================================

/// <summary>
/// Utility methods for network operations.
/// </summary>
internal static class NetworkUtilities
{
    /// <summary>
    /// Gets the local hostname.
    /// </summary>
    public static string GetHostName()
    {
        try
        {
            return Dns.GetHostName();
        }
        catch
        {
            return Environment.MachineName;
        }
    }
    
    /// <summary>
    /// Checks if valid network interfaces are available.
    /// </summary>
    public static bool HasValidNetworkInterfaces()
    {
        try
        {
            var interfaces = NetworkInterface.GetAllNetworkInterfaces();
            
            return interfaces.Any(nic =>
                nic.OperationalStatus == OperationalStatus.Up &&
                nic.NetworkInterfaceType != NetworkInterfaceType.Loopback &&
                nic.SupportsMulticast &&
                HasValidUnicastAddress(nic));
        }
        catch
        {
            return false;
        }
    }
    
    /// <summary>
    /// Checks if a network interface has a valid unicast address.
    /// </summary>
    private static bool HasValidUnicastAddress(NetworkInterface nic)
    {
        try
        {
            var properties = nic.GetIPProperties();
            
            return properties.UnicastAddresses.Any(addr =>
                (addr.Address.AddressFamily == AddressFamily.InterNetwork ||
                 addr.Address.AddressFamily == AddressFamily.InterNetworkV6) &&
                !IPAddress.IsLoopback(addr.Address) &&
                !addr.Address.IsIPv6LinkLocal);
        }
        catch
        {
            return false;
        }
    }
    
    /// <summary>
    /// Gets local IP addresses suitable for mDNS.
    /// </summary>
    public static IEnumerable<IPAddress> GetLocalAddresses()
    {
        var addresses = new List<IPAddress>();
        
        try
        {
            var interfaces = NetworkInterface.GetAllNetworkInterfaces();
            
            foreach (var nic in interfaces)
            {
                if (nic.OperationalStatus != OperationalStatus.Up)
                    continue;
                
                if (nic.NetworkInterfaceType == NetworkInterfaceType.Loopback)
                    continue;
                
                if (!nic.SupportsMulticast)
                    continue;
                
                var properties = nic.GetIPProperties();
                
                foreach (var unicast in properties.UnicastAddresses)
                {
                    var addr = unicast.Address;
                    
                    if (IPAddress.IsLoopback(addr))
                        continue;
                    
                    if (addr.IsIPv6LinkLocal)
                        continue;
                    
                    if (addr.AddressFamily == AddressFamily.InterNetwork ||
                        addr.AddressFamily == AddressFamily.InterNetworkV6)
                    {
                        addresses.Add(addr);
                    }
                }
            }
        }
        catch (Exception ex)
        {
            Logger.Debug($"Error enumerating network addresses: {ex.Message}");
        }
        
        return addresses;
    }
}

// =============================================================================
// Logger
// =============================================================================

/// <summary>
/// Simple console logger with level support.
/// </summary>
internal static class Logger
{
    private static bool _debugEnabled;
    private static readonly object _lock = new();
    
    /// <summary>
    /// Enables debug logging.
    /// </summary>
    public static void EnableDebug() => _debugEnabled = true;
    
    /// <summary>
    /// Logs a debug message.
    /// </summary>
    public static void Debug(string message)
    {
        if (_debugEnabled)
        {
            Log("DEBUG", message, ConsoleColor.Gray);
        }
    }
    
    /// <summary>
    /// Logs an info message.
    /// </summary>
    public static void Info(string message)
    {
        Log("INFO", message, ConsoleColor.White);
    }
    
    /// <summary>
    /// Logs a warning message.
    /// </summary>
    public static void Warning(string message)
    {
        Log("WARN", message, ConsoleColor.Yellow);
    }
    
    /// <summary>
    /// Logs an error message.
    /// </summary>
    public static void Error(string message)
    {
        Log("ERROR", message, ConsoleColor.Red);
    }
    
    /// <summary>
    /// Logs a message with the specified level and color.
    /// </summary>
    private static void Log(string level, string message, ConsoleColor color)
    {
        lock (_lock)
        {
            var timestamp = DateTime.Now.ToString("HH:mm:ss.fff");
            var originalColor = Console.ForegroundColor;
            
            try
            {
                Console.ForegroundColor = color;
                Console.WriteLine($"[{timestamp}] [{level,-5}] {message}");
            }
            finally
            {
                Console.ForegroundColor = originalColor;
            }
        }
    }
}
