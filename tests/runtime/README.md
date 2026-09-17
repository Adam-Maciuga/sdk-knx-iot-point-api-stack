# Runtime conformance tests

Python/pytest tests that exercise the full stack over real CoAP/OSCORE on IPv6,
mimicking EITT certification. A `runtime_test_server` (built from
`runtime_test_server.c`) is the device under test (DUT).

`test_eitt.py` is the whole suite: it generates one pytest item per EITT
TestCase directly from
`data/KnxConformanceTestTemplate-PointAPI.xml` at collection time via
`pytest_generate_tests`, driven by the interpreter in `eitt/`. See
`tests/docs/eitt-xml-interpreter.md` for the full design. (A hand-written
`test_5_*.py` suite used to live here; it has been removed — the
interpreter replaces it.)

## Layout

```
conftest.py            pytest fixtures: builds/starts the DUT, CoAP + OSCORE sessions
test_eitt.py           the suite — one generated test per EITT TestCase
runtime_test_server.c  the device under test
eitt/                  the EITT XML interpreter (model, runner, provisioning,
                       hooks, content matching, JSON<->CBOR schema, overrides)
transport/             wire-level clients: CoAP, OSCORE, SPAKE2+, mDNS
data/                  EITT inputs: the conformance template, EITT's own key
                       maps + comment-command scheme, the variable-values
                       JSON, the SWU firmware dummy, and traces/ (9 reference
                       runs recorded by the real tool)
tools/                 run scripts (Docker + local CI), the GUI runner, and
                       SPAKE2+ record helpers
```

## Running in Docker (recommended)

`tools/run-in-docker.ps1` builds the stack and runs the tests inside the
`itgesellschaft/knxiot-knx-ci` container with a veth pair — the same setup as GitLab CI.
Requires Docker Desktop running (Linux engine).

```powershell
# Run the whole runtime suite
.\tests\runtime\run-in-docker.ps1

# Run a subset (any pytest -k filter)
.\tests\runtime\run-in-docker.ps1 -PytestArgs "-k","Security::"   # one section
.\tests\runtime\run-in-docker.ps1 -PytestArgs "-k","5.3.17.6"     # a single TestCase

# Run a specific file, pass extra single-dash args
.\tests\runtime\run-in-docker.ps1 tests/runtime/test_5_4_observe.py -x

# Test uncommitted/untracked changes (default tests only committed HEAD)
.\tests\runtime\run-in-docker.ps1 -IncludeUncommitted -k "5_4"

# Include tests gated behind RUN_SLOW_TESTS (currently the ~60 s SPAKE2+
# brute-force auto-unblock test, which is skipped by default)
.\tests\runtime\run-in-docker.ps1 -RunSlowTests -k "6d_unblocks"
```

### Reading results

- Failing tests are listed at the end under `=== FAILED TESTS (N) ===`,
  followed by a green `PASSED` or red `FAILED` banner.
- Tracebacks default to `--tb=long --showlocals`, so each failing frame shows
  the local variable values (decoded CBOR, response codes, sequence numbers).

### Notes / gotchas

- Without `-IncludeUncommitted` the script tests **committed HEAD**, so commit
  your work first (or pass the flag).
- `-IncludeUncommitted` packages the working tree (tracked + untracked,
  respecting `.gitignore`).
- Double-dash pytest args (e.g. `--tb=line`) are mis-parsed by PowerShell;
  single-dash args (`-k`, `-x`, `-v`) and file paths work.
- A `-k` expression must be a single token. `-k 'a or b'` is split by PowerShell
  before it reaches pytest and collects nothing; run each filter separately.
- `-RunSlowTests` sets `RUN_SLOW_TESTS=1` in the container. Without it,
  `test_5_3_1_6d_unblocks_after_timeout` is skipped by a static `skipif` and
  never runs, even in a full suite.
- Some conditional tests skip under cumulative suite load rather than failing
  (e.g. the `test_5_4_1_15_bonus_*` malformed-length cases wait up to 20 s for a
  discovery GET). They pass when run in isolation; a skip there is a timing
  artefact, not a defect.
- The script builds fresh inside the container's filesystem (no bind mount),
  avoiding stale CMake cache issues.

## Running on Linux / CI directly

`run-local-ci.sh` does the same steps as the CI job (veth pair, build, pytest)
and is intended to run inside a Linux container or CI runner. See the comments
at the top of that script for `git archive` invocation examples.
