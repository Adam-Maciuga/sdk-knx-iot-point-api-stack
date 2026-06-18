# Runtime conformance tests

Python/pytest tests that exercise the full stack over real CoAP/OSCORE on IPv6,
mimicking EITT certification. A `runtime_test_server` (built from
`runtime_test_server.c`) is the device under test (DUT).

## Running in Docker (recommended)

`run-in-docker.ps1` builds the stack and runs the tests inside the
`itgesellschaft/knxiot-knx-ci` container with a veth pair — the same setup as GitLab CI.
Requires Docker Desktop running (Linux engine).

```powershell
# Run the whole runtime suite
.\tests\runtime\run-in-docker.ps1

# Run a subset (any pytest -k filter)
.\tests\runtime\run-in-docker.ps1 -k "5_4"        # observe tests
.\tests\runtime\run-in-docker.ps1 -k "5_4_2_1"    # a single test

# Run a specific file, pass extra single-dash args
.\tests\runtime\run-in-docker.ps1 tests/runtime/test_5_4_observe.py -x

# Test uncommitted/untracked changes (default tests only committed HEAD)
.\tests\runtime\run-in-docker.ps1 -IncludeUncommitted -k "5_4"
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
- The script builds fresh inside the container's filesystem (no bind mount),
  avoiding stale CMake cache issues.

## Running on Linux / CI directly

`run-local-ci.sh` does the same steps as the CI job (veth pair, build, pytest)
and is intended to run inside a Linux container or CI runner. See the comments
at the top of that script for `git archive` invocation examples.
