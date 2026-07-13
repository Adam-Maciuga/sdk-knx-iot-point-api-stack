<#
.SYNOPSIS
    Run runtime conformance tests inside a Docker container with veth pairs.
    Replicates the GitLab CI linux-runtime-test job locally.

.DESCRIPTION
    By default the current git HEAD is exported with `git archive` and unpacked
    inside the container's native filesystem. This avoids Docker Desktop
    bind-mount issues (stale CMake caches, try-compile scratch failures, CRLF
    problems).

    Only COMMITTED changes are tested by default. Pass -IncludeUncommitted to
    package the live working tree (tracked + untracked files, respecting
    .gitignore) so uncommitted edits are included.

.EXAMPLE
    .\tests\runtime\run-in-docker.ps1
    .\tests\runtime\run-in-docker.ps1 -k "5_9"
    .\tests\runtime\run-in-docker.ps1 -IncludeUncommitted -k "5_9"
    .\tests\runtime\run-in-docker.ps1 tests/runtime/test_5_9_observe.py -v
    .\tests\runtime\run-in-docker.ps1 --tb=long -x
#>

param(
    [string]$Preset = "linux-test-gcc",
    [string]$Image = "itgesellschaft/knxiot-knx-ci:latest",
    # Test the current working tree (uncommitted + untracked files) instead of
    # committed HEAD. Respects .gitignore.
    [Alias("WorkingTree", "Dirty")]
    [switch]$IncludeUncommitted,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$PytestArgs
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path "$PSScriptRoot\..\..").Path

# Base pytest invocation always scans the runtime dir and skips the GUI runner
# (tkinter is not installed in the CI image). User args are appended, so
# `-k 5_9`, `-x`, or an explicit file path all work.
#   --tb=long     full tracebacks (why a test failed, all frames)
#   --showlocals  values of local variables at each failing frame
#   -rfE          short summary line per failed/errored test (parsed below)
$Base = "tests/runtime/ --ignore=tests/runtime/test_runner_gui.py"
if (-not $PytestArgs) {
    $PytestArgs = @("-v", "--tb=long", "--showlocals")
}
$ArgsStr = "$Base " + ($PytestArgs -join " ")

$Source = if ($IncludeUncommitted) { "working tree (uncommitted + untracked)" } else { "committed HEAD" }
Write-Host "=== Running runtime tests in Docker ($Image) ===" -ForegroundColor Cyan
Write-Host "    Repo:   $RepoRoot"
Write-Host "    Branch: $(git -C $RepoRoot branch --show-current)"
Write-Host "    Args:   $ArgsStr"
Write-Host "    Source: $Source" -ForegroundColor Yellow
Write-Host ""

# Bash script executed inside the container. Built as a here-string, normalised
# to LF, and base64-encoded so no CRLF or quoting survives the trip.
$Bash = @"
set -euo pipefail
echo '--- Installing iproute2 ---'
apt-get update -qq && apt-get install -y -qq iproute2 > /dev/null 2>&1
echo '--- Creating veth pair ---'
ip link add veth-dut type veth peer name veth-test
ip link set veth-dut up
ip link set veth-test up
sysctl -w net.ipv6.conf.veth-dut.disable_ipv6=0
sysctl -w net.ipv6.conf.veth-test.disable_ipv6=0
sleep 3
# Add default multicast route on both veth interfaces.
# The DUT (veth-dut) sends query responses via mdns_listen_sock6 using
# sendto() with sin6_scope_id=0, so the kernel needs a multicast route
# on veth-dut to resolve the outgoing interface.  The test client
# (veth-test) also needs the route to send its raw PTR queries to FF02::FB.
ip -6 route add ff00::/8 dev veth-dut || true
ip -6 route add ff00::/8 dev veth-test || true
DUT_ADDR=`$(ip -6 -o addr show dev veth-dut scope link | awk '{print `$4}' | cut -d/ -f1)
echo "DUT address=`${DUT_ADDR} on veth-dut / veth-test"
echo '--- Building runtime_test_server ($Preset) ---'
cmake --preset=$Preset
cmake --build --preset=$Preset --target runtime_test_server
echo '--- Installing Python dependencies ---'
curl -sSL https://bootstrap.pypa.io/get-pip.py -o /tmp/get-pip.py
python3 /tmp/get-pip.py --break-system-packages --quiet 2>/dev/null || true
python3 -m pip install --break-system-packages --quiet -r tests/runtime/requirements.txt
echo '--- Running tests ---'
RUNTIME_TEST_SERVER=build/$Preset/tests/runtime_test_server \
DUT_IFACE=veth-dut DEVICE_HOST=`$DUT_ADDR DEVICE_IFACE=veth-test \
COAP_TIMEOUT=15 SERVER_STARTUP_TIMEOUT=30 RUNTIME_TEST_QUIET=1 \
python3 -m pytest -rfE $ArgsStr
"@

$Bash = $Bash -replace "`r`n", "`n"
$B64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Bash))

# Pack the sources into a binary tar. PowerShell pipes corrupt binary streams,
# so write to a temp file and feed it to docker stdin via a cmd redirect.
#   - default:            committed HEAD via `git archive`
#   - -IncludeUncommitted: the working tree (tracked + untracked, minus
#                          .gitignore) tarred from the live files so
#                          uncommitted edits are included.
$Tar = Join-Path ([IO.Path]::GetTempPath()) "knx-runtime-$([Guid]::NewGuid().ToString('N')).tar"
$List = "$Tar.list"
$code = 1
try {
    if ($IncludeUncommitted) {
        # git ls-files honours .gitignore and lists tracked + new untracked files.
        Push-Location $RepoRoot
        try {
            $files = git ls-files --cached --others --exclude-standard --full-name
            # Write a BOM-free, LF-terminated list; tar.exe chokes on a BOM.
            [IO.File]::WriteAllText($List, ($files -join "`n") + "`n",
                                    [Text.UTF8Encoding]::new($false))
            # tar.exe (bsdtar) ships with Windows 10+. -T reads the file list.
            tar -c -f $Tar -C $RepoRoot -T $List
        } finally {
            Pop-Location
        }
    } else {
        git -C $RepoRoot archive --format=tar -o $Tar HEAD
    }

    $inner  = 'echo $B64SCRIPT | base64 -d > /run.sh && mkdir -p /w && '
    $inner += 'tar -xf - -C /w && cd /w && bash /run.sh'
    # Merge stderr into stdout inside cmd so PowerShell does not treat native
    # stderr (e.g. CMake warnings) as a terminating error.
    $docker = "docker run --rm -i --privileged " +
              "--sysctl net.ipv6.conf.all.disable_ipv6=0 " +
              "-e B64SCRIPT=$B64 $Image bash -c ""$inner"" < ""$Tar"" 2>&1"

    $ErrorActionPreference = "Continue"
    # Stream output live while also capturing it so we can summarise failures.
    $output = cmd /c $docker 2>&1 | ForEach-Object {
        Write-Host $_
        $_
    }
    $code = $LASTEXITCODE
}
finally {
    Remove-Item $Tar -ErrorAction SilentlyContinue
    Remove-Item $List -ErrorAction SilentlyContinue
}

# Extract failing/erroring test IDs from pytest's -v result lines:
# e.g. "tests/runtime/test_x.py::Class::method FAILED [ 42%]"
$failed = $output |
    Select-String -Pattern '^tests/runtime/\S+\s+(FAILED|ERROR)\s+\[' |
    ForEach-Object { $_.ToString().Trim() }

Write-Host ""
if ($failed) {
    Write-Host "=== FAILED TESTS ($($failed.Count)) ===" -ForegroundColor Red
    $failed | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
}

Write-Host ""
if ($code -eq 0) {
    Write-Host "=== PASSED (exit $code) ===" -ForegroundColor Green
} else {
    Write-Host "=== FAILED (exit $code) ===" -ForegroundColor Red
}
exit $code
