<#
.SYNOPSIS
    Run runtime conformance tests inside a Docker container with veth pairs.
    Replicates the GitLab CI linux-runtime-test job locally.

.EXAMPLE
    .\tests\runtime\run-in-docker.ps1
    .\tests\runtime\run-in-docker.ps1 -k "5_3_17"
    .\tests\runtime\run-in-docker.ps1 --tb=long -x
#>

param(
    [string]$Preset = "linux-test-gcc",
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$PytestArgs
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path "$PSScriptRoot\..\..").Path -replace '\\', '/'
$Image = "luftd/knx-ci:latest"

if (-not $PytestArgs) {
    $PytestArgs = @("-v", "--tb=short")
}
$ArgsStr = $PytestArgs -join " "

Write-Host "=== Running runtime tests in Docker ($Image) ===" -ForegroundColor Cyan
Write-Host "    Repo:  $RepoRoot"
Write-Host "    Args:  $ArgsStr"
Write-Host ""

# Convert Windows path to Docker mount format
$MountPath = $RepoRoot
if ($MountPath -match '^([A-Za-z]):(.*)') {
    $MountPath = "/" + $Matches[1].ToLower() + $Matches[2]
}

# Write the script to a temp file with LF line endings
$ScriptLines = @(
    "set -euo pipefail"
    "echo '--- Installing iproute2 ---'"
    "apt-get update -qq && apt-get install -y -qq iproute2 > /dev/null 2>&1"
    "echo '--- Creating veth pair ---'"
    "ip link add veth-dut type veth peer name veth-test"
    "ip link set veth-dut up"
    "ip link set veth-test up"
    "sleep 3"
    "sysctl -w net.ipv6.conf.veth-dut.disable_ipv6=0"
    "sysctl -w net.ipv6.conf.veth-test.disable_ipv6=0"
    "sleep 2"
    "DUT_ADDR=`$(ip -6 -o addr show dev veth-dut scope link | awk '{print `$4}' | cut -d/ -f1)"
    "echo `"DUT address=`${DUT_ADDR} on veth-dut / veth-test`""
    "echo '--- Building runtime_test_server ($Preset) ---'"
    "cmake --preset=$Preset --fresh"
    "cmake --build --preset=$Preset --target runtime_test_server"
    "echo '--- Installing Python dependencies ---'"
    "curl -sSL https://bootstrap.pypa.io/get-pip.py -o /tmp/get-pip.py"
    "python3 /tmp/get-pip.py --break-system-packages --quiet 2>/dev/null || true"
    "python3 -m pip install --break-system-packages --quiet -r tests/runtime/requirements.txt"
    "echo '--- Running tests ---'"
    "RUNTIME_TEST_SERVER=build/$Preset/tests/runtime_test_server DUT_IFACE=veth-dut DEVICE_HOST=`$DUT_ADDR DEVICE_IFACE=veth-test ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 COAP_TIMEOUT=15 SERVER_STARTUP_TIMEOUT=30 python3 -m pytest tests/runtime/ --ignore=tests/runtime/test_runner_gui.py $ArgsStr"
)
$ScriptContent = $ScriptLines -join "`n"
$ScriptFile = Join-Path $RepoRoot "tests/runtime/.docker-run.sh"
[System.IO.File]::WriteAllText($ScriptFile, $ScriptContent, [System.Text.UTF8Encoding]::new($false))

try {
    docker run --rm -it `
        --privileged `
        --sysctl net.ipv6.conf.all.disable_ipv6=0 `
        -v "${MountPath}:/src" `
        -w /src `
        $Image `
        bash /src/tests/runtime/.docker-run.sh
} finally {
    Remove-Item $ScriptFile -ErrorAction SilentlyContinue
}
