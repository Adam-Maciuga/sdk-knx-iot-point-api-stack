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
    .\tests\common\tools\run-in-docker.ps1
    .\tests\common\tools\run-in-docker.ps1 -Section 5.6
    .\tests\common\tools\run-in-docker.ps1 -Section 5.5,5.6 -IncludeUncommitted
    .\tests\common\tools\run-in-docker.ps1 -k "5_9"
    .\tests\common\tools\run-in-docker.ps1 -IncludeUncommitted -k "5_9"
    .\tests\common\tools\run-in-docker.ps1 -Tier eitt -Section 5.6
    .\tests\common\tools\run-in-docker.ps1 -Tier runtime
    .\tests\common\tools\run-in-docker.ps1 tests/runtime/test_observe.py -v
    .\tests\common\tools\run-in-docker.ps1 --tb=long -x

.NOTES
    Use -Section to select an EITT section. Do NOT use -k for that: pytest's
    -k is a substring match, so `-k 5.6` also selects 5.5.5.6 and 5.5.6.1.
    Those then run without their sibling preparation TestCases and produce
    fabricated "response body could not be decoded" failures.
#>

param(
    [string]$Preset = "linux-test-gcc",
    [string]$Image = "itgesellschaft/knxiot-knx-ci:latest",
    # Test the current working tree (uncommitted + untracked files) instead of
    # committed HEAD. Respects .gitignore.
    [Alias("WorkingTree", "Dirty")]
    [switch]$IncludeUncommitted,
    # Set RUN_SLOW_TESTS inside the container so tests gated behind it run
    # (currently test_spake_lockout_unblocks_after_timeout, which waits out a ~60 s
    # brute-force lockout timer).
    [switch]$RunSlowTests,
    # Stream the device's own server-side logs (protocol/state-machine
    # detail) instead of the CI-quiet default. Useful when a failure needs
    # to be diagnosed from the device's point of view, not just the client.
    [switch]$ServerLogs,
    # Emit the client-side OSCORE layer's [oscore] debug lines
    # (KNX_TEST_DEBUG_OSCORE=1): per-message nonce / AAD / key / PIV,
    # for byte-comparing the client's crypto inputs against the device's.
    [switch]$OscoreDebug,
    # Emit the client-side CoAP layer's [coap] debug lines
    # (KNX_TEST_DEBUG_COAP=1): per-datagram send/recv, including the
    # multicast group listener's bind/join and captured telegrams.
    [switch]$CoapDebug,
    # Emit the EITT runner's [group] lines (KNX_TEST_DEBUG_GROUP=1):
    # whether a multicast group listener was armed for an upcoming
    # DUT-originated group telegram, and on which address.
    [switch]$GroupDebug,
    # Multiplier applied to every EITT TimeToNext inter-telegram wait
    # (RUNTIME_TEST_TIME_SCALE). The XML's waits are tuned for real KNX
    # hardware over a real network; over the Docker/veth loopback the
    # device answers in microseconds, so the default 0.05 (20x faster)
    # cuts a ~18min section-5.3 run to ~1-2min with no behavioral change.
    # Pass -TimeScale 1.0 to reproduce real-hardware pacing exactly.
    [double]$TimeScale = 0.05,
    # Skip the persistent build/pip cache volumes and build everything from
    # scratch inside a throwaway container. Slower (~60s of extra fixed
    # overhead), but the way to rule out a stale artifact — use it after
    # changing a CMake preset or when a failure smells like a build-cache
    # problem rather than a test problem.
    [switch]$NoCache,
    # Select whole EITT section(s) by number, e.g. -Section 5.6 (repeatable:
    # -Section 5.5,5.6). Prefer this over -PytestArgs "-k","5.6": `-k` is a
    # SUBSTRING match, so "5.6" also selects 5.5.5.6 and 5.5.6.1, which then
    # run WITHOUT their sibling preparation TestCases and fail spuriously.
    [string[]]$Section,
    # Which test tier to run (see tests/README.md):
    #   eitt    -> tests/eitt/                (template-driven interpreter,
    #                                          plus its no-DUT harness tests)
    #   runtime -> tests/runtime/             (hand-written device tests)
    #   harness -> tests/eitt/harness_tests/  (interpreter/GUI tests, no DUT)
    #   all     -> tests/eitt/ tests/runtime/ (both device tiers, the default)
    [ValidateSet('all', 'eitt', 'runtime', 'harness')]
    [string]$Tier = 'all',
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$PytestArgs
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path "$PSScriptRoot\..\..\..").Path

# Base pytest invocation names the tier directory (or directories) selected
# by -Tier. User args are appended, so `-k Security::`, `-x`, or an explicit
# file path all work. The shared infrastructure the tiers import lives in
# tests/common/ (fixtures.py, transport/, tools/) and holds no tests, so it
# is simply never named here.
#
# `all` lists BOTH tier directories explicitly rather than leaning on
# tests/pytest.ini's `testpaths`: testpaths applies only when pytest gets no
# path argument at all, and this script always passes one.
#   --tb=long     full tracebacks (why a test failed, all frames)
#   --showlocals  values of local variables at each failing frame
#   -rfE          short summary line per failed/errored test (parsed below)
$Base = switch ($Tier) {
    'eitt'    { "tests/eitt/" }
    'runtime' { "tests/runtime/" }
    'harness' { "tests/eitt/harness_tests/" }
    default   { "tests/eitt/ tests/runtime/" }
}
if (-not $PytestArgs) {
    $PytestArgs = @("-v", "--tb=long", "--showlocals")
}
# -Section 5.5,5.6 -> --section 5.5 --section 5.6 (see the param's comment
# for why this exists instead of just using -k).
foreach ($s in $Section) {
    $PytestArgs += @("--section", $s)
}
# Each element of $PytestArgs becomes ONE bash word inside the heredoc
# below, however many spaces it contains (e.g. -k "eitt and Security::").
# Bash re-tokenizes $ArgsStr when it expands unquoted on the pytest
# command line, so an argument with an embedded space (a multi-term -k
# expression, a value with quotes) would otherwise be split back apart
# and fed to pytest as two separate arguments. Single-quoting each
# element -- with embedded single quotes escaped the POSIX way,
# '\'' -- keeps it intact as one word no matter what it contains.
function ConvertTo-BashSingleQuoted([string]$s) {
    return "'" + ($s -replace "'", "'\''") + "'"
}
$QuotedPytestArgs = $PytestArgs | ForEach-Object { ConvertTo-BashSingleQuoted $_ }
$ArgsStr = "$Base " + ($QuotedPytestArgs -join " ")
$QuietFlag = if ($ServerLogs) { "0" } else { "1" }
$OscoreDbgFlag = if ($OscoreDebug) { "1" } else { "0" }
$CoapDbgFlag = if ($CoapDebug) { "1" } else { "0" }
$GroupDbgFlag = if ($GroupDebug) { "1" } else { "0" }

$Source = if ($IncludeUncommitted) { "working tree (uncommitted + untracked)" } else { "committed HEAD" }
$SlowEnv = if ($RunSlowTests) { "RUN_SLOW_TESTS=1 " } else { "" }
Write-Host "=== Running runtime tests in Docker ($Image) ===" -ForegroundColor Cyan
Write-Host "    Repo:   $RepoRoot"
Write-Host "    Branch: $(git -C $RepoRoot branch --show-current)"
Write-Host "    Tier:   $Tier"
Write-Host "    Args:   $ArgsStr"
Write-Host "    Source: $Source" -ForegroundColor Yellow
Write-Host ""

# Bash script executed inside the container. Built as a here-string, normalised
# to LF, and base64-encoded so no CRLF or quoting survives the trip.
$Bash = @"
set -euo pipefail
echo '--- Installing iproute2 ---'
apt-get update -qq && apt-get install -y -qq iproute2 > /dev/null 2>&1
echo '--- Creating veth pair (DUT in its own netns) ---'
# The DUT lives in a SEPARATE network namespace, the test tool stays in the
# root one, and the veth pair is the only link between them.
#
# Why separate namespaces and not just a veth pair in one: the DUT learns the
# TOOL's IPv6 address from the SOURCE address of our discovery reply
# (oc_knx_client.c knx_coap_discovery_response stores
# data->endpoint->addr.ipv6.address). With both ends in one namespace every
# address is local, so the kernel answers "source for a packet to ::1?" with
# ::1 itself — `ip -6 route get` says `dev lo` for both — and the DUT records
# its OWN address, then sends 5.4.1.4/5.4.1.15's unicast s-mode telegram to
# itself. Distinct prefixes and /128 + explicit host routes do NOT fix that;
# only real namespace separation does.
ip netns add dut
ip link add veth-dut type veth peer name veth-test
ip link set veth-dut netns dut
ip link set veth-test up
ip netns exec dut ip link set lo up
ip netns exec dut ip link set veth-dut up
ip netns exec dut sysctl -w net.ipv6.conf.veth-dut.disable_ipv6=0
sysctl -w net.ipv6.conf.veth-test.disable_ipv6=0
sleep 3
# Multicast route on both ends. The DUT sends query responses via
# mdns_listen_sock6 using sendto() with sin6_scope_id=0, so the kernel needs a
# multicast route to resolve the outgoing interface. The test client needs it
# to send its raw PTR queries to FF02::FB.
ip netns exec dut ip -6 route add ff00::/8 dev veth-dut || true
ip -6 route add ff00::/8 dev veth-test || true
# Give each end a UNIQUE LOCAL ADDRESS (fd00::/8, RFC 4193) alongside its
# automatic link-local, so the network matches the scope the rest of the setup
# assumes. KNX_MULTICAST_SCOPE defaults to 5 (site-local) on BOTH sides —
# CMakeLists.txt line 84 for the stack, fixtures.py MC_SCOPE for the tests —
# but a veth pair comes up link-local only (fe80::/10, RFC 6724 score 2, not
# 5). That mismatch silently disables every DEVICE-ORIGINATED s-mode message:
# oc_init_read_next (oc_knx_fp.c) waits for
# oc_connectivity_get_network_scope() >= SCOPE_SITE_LOCAL before sending, logs
# "network not (yet)up with correct scope" (651 times in one run), gives up
# after 20 attempts, and sends nothing.
ip netns exec dut ip -6 addr add fd11:9988:7766::1/64 dev veth-dut nodad || true
ip -6 addr add fd11:9988:7766::2/64 dev veth-test nodad || true
sleep 1
DUT_ADDR=fd11:9988:7766::1
echo "DUT address=`${DUT_ADDR} (netns 'dut', veth-dut) <-> tool fd11:9988:7766::2 (veth-test)"
echo '--- Building runtime_test_server ($Preset) ---'
cmake --preset=$Preset
cmake --build --preset=$Preset --target runtime_test_server
echo '--- Installing Python dependencies ---'
# Both steps are skippable on a warm cache volume: pip itself is installed
# into /usr/local (cached), and the wheels land in the cached pip dir, so a
# second run neither downloads get-pip.py nor re-resolves requirements.
if ! python3 -m pip --version > /dev/null 2>&1; then
  curl -sSL https://bootstrap.pypa.io/get-pip.py -o /tmp/get-pip.py
  python3 /tmp/get-pip.py --break-system-packages --quiet 2>/dev/null || true
fi
python3 -m pip install --break-system-packages --quiet \
  -r tests/common/requirements.txt
echo '--- Running tests ---'
RUNTIME_TEST_SERVER=build/$Preset/tests/runtime_test_server \
RUNTIME_TEST_SERVER_PREFIX='ip netns exec dut' \
DUT_IFACE=veth-dut DEVICE_HOST=`$DUT_ADDR DEVICE_IFACE=veth-test \
COAP_TIMEOUT=5 SERVER_STARTUP_TIMEOUT=30 RUNTIME_TEST_QUIET=$QuietFlag \
RUNTIME_TEST_TIME_SCALE=$TimeScale KNX_TEST_DEBUG_OSCORE=$OscoreDbgFlag \
KNX_TEST_DEBUG_COAP=$CoapDbgFlag KNX_TEST_DEBUG_GROUP=$GroupDbgFlag \
KNX_TEST_DEBUG_MDNS=$GroupDbgFlag \
$SlowEnv`python3 -m pytest -rfE $ArgsStr
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

    # Persistent caches. The container is --rm, so without these EVERY run
    # recompiles all 197 objects (mbedtls, tinycbor, the whole stack),
    # re-downloads get-pip.py and re-resolves requirements.txt — ~60s of
    # fixed overhead before a single test executes, even when only a Python
    # test file changed. Named volumes survive between runs:
    #   knx-rt-build  -> /w/build   (CMake tree; safe because build/ is
    #                                gitignored, so the untar never touches it
    #                                and CMake's own dependency checking
    #                                decides what to rebuild)
    #   knx-rt-pip    -> pip's wheel cache, so requirements resolve offline
    # NOT cached: /usr/local — the image's own toolchain lives there
    # (/usr/local/bin/gcc), so shadowing it with a volume would break the
    # build outright.
    # Pass -NoCache for a guaranteed-clean build (e.g. after changing a
    # CMake preset or chasing a suspected stale-artifact problem).
    $mounts = ""
    if (-not $NoCache) {
        $mounts = "-v knx-rt-build:/w/build " +
                  "-v knx-rt-pip:/root/.cache/pip "
    }

    # Merge stderr into stdout inside cmd so PowerShell does not treat native
    # stderr (e.g. CMake warnings) as a terminating error.
    $docker = "docker run --rm -i --privileged " +
              "--sysctl net.ipv6.conf.all.disable_ipv6=0 " +
              $mounts +
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
#      "tests/eitt/test_eitt.py::test_5_1_1_1_site_local FAILED [  3%]"
$failed = $output |
    Select-String -Pattern '^tests/(eitt|runtime)/\S+\s+(FAILED|ERROR)\s+\[' |
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
