<#
.SYNOPSIS
    Run runtime conformance tests inside a Docker container with veth pairs.
    Replicates the GitLab CI device-test jobs (runtime-gcc, eitt-gcc) locally.

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

# ---------------------------------------------------------------------------
# The in-container logic lives in ONE place: tests/common/tools/run-local-ci.sh
# (shared with run-in-docker.sh, the bash twin of this runner, and step for step
# the same as .gitlab-ci.yml's device jobs). This script only packs the source
# tree, starts the container privileged and maps its switches onto that
# script's options. Whatever version of run-local-ci.sh is in the packed tree
# is the one that runs, so -IncludeUncommitted covers the runner itself too.
# ---------------------------------------------------------------------------
if (-not $PytestArgs) {
    # -v so the per-test PASSED/FAILED lines exist for the summary below;
    # --tb=long and --showlocals so a failure is diagnosable from the log.
    $PytestArgs = @("-v", "--tb=long", "--showlocals")
}
$InnerArgs = @(
    "--tier", $Tier,
    "--preset", $Preset,
    "--time-scale", $TimeScale.ToString([Globalization.CultureInfo]::InvariantCulture)
)
foreach ($s in $Section) { $InnerArgs += @("--section", $s) }
if ($ServerLogs)   { $InnerArgs += "--server-logs" }
if ($OscoreDebug)  { $InnerArgs += "--oscore-debug" }
if ($CoapDebug)    { $InnerArgs += "--coap-debug" }
if ($GroupDebug)   { $InnerArgs += "--group-debug" }
if ($RunSlowTests) { $InnerArgs += "--slow" }
$InnerArgs += "--"
$InnerArgs += $PytestArgs

function ConvertTo-BashSingleQuoted([string]$s) {
    return "'" + ($s -replace "'", "'\''") + "'"
}
$QuotedInner = ($InnerArgs | ForEach-Object { ConvertTo-BashSingleQuoted $_ }) -join " "

$Source = if ($IncludeUncommitted) { "working tree (uncommitted + untracked)" } else { "committed HEAD" }
Write-Host "=== Running device tests in Docker ($Image) ===" -ForegroundColor Cyan
Write-Host "    Repo:   $RepoRoot"
Write-Host "    Branch: $(git -C $RepoRoot branch --show-current)"
Write-Host "    Tier:   $Tier"
Write-Host "    Inner:  run-local-ci.sh $QuotedInner"
Write-Host "    Source: $Source" -ForegroundColor Yellow
Write-Host ""

# The one-line launcher is passed base64-encoded so no quoting of the pytest
# arguments has to survive cmd.exe, PowerShell and bash at once.
# A CRLF working copy of the shell scripts (an editor, or a checkout made before
# .gitattributes forced eol=lf on *.sh) would fail in bash with "$'\x0D': command
# not found"; strip the carriage returns before running.
$Bash = "sed -i 's/\x0D//g' tests/common/tools/*.sh && exec bash tests/common/tools/run-local-ci.sh $QuotedInner`n"
$B64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Bash))

$Tar = Join-Path ([IO.Path]::GetTempPath()) "knx-runtime-$([Guid]::NewGuid().ToString('N')).tar"
$List = "$Tar.list"
$code = 1
try {
    # Pack the tree into a tar the container unpacks into its own filesystem:
    # no bind mount, so no stale CMake caches, CRLF surprises or try-compile
    # failures from Docker Desktop's file sharing.
    if ($IncludeUncommitted) {
        Push-Location $RepoRoot
        try {
            $files = git ls-files --cached --others --exclude-standard --full-name
            [IO.File]::WriteAllText($List, ($files -join "`n") + "`n",
                                    [Text.UTF8Encoding]::new($false))
            # Windows' own tar.exe by full path: when this script is launched from
            # Git Bash, /usr/bin/tar shadows it and fails with "Cannot connect to C".
            & "$env:SystemRoot\System32\tar.exe" -c -f $Tar -C $RepoRoot -T $List
        } finally {
            Pop-Location
        }
    } else {
        git -C $RepoRoot archive --format=tar -o $Tar HEAD
    }

    $inner  = 'echo $B64SCRIPT | base64 -d > /run.sh && mkdir -p /w && '
    $inner += 'tar -xf - -C /w && cd /w && bash /run.sh'

    # Persistent caches. The container is --rm, so without these EVERY run
    # recompiles all objects (mbedtls, tinycbor, the whole stack), re-downloads
    # get-pip.py and re-resolves requirements.txt. Named volumes survive between
    # runs: knx-rt-build -> /w/build (build/ is gitignored, so the untar never
    # touches it and CMake decides what to rebuild), knx-rt-pip -> pip's cache.
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
