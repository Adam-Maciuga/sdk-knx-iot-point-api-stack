# Runtime tier — hand-written device tests

Python/pytest tests that exercise the full stack over real CoAP/OSCORE on
IPv6. The `runtime_test_server` built from
[`../common/runtime_test_server.c`](../common/runtime_test_server.c) is the
device under test (DUT).

This directory holds **only** behaviour-named `test_*.py` modules. The
fixtures, transport clients and run scripts they build on live in
[`../common/`](../common/); the template-driven tier and its data live in
[`../eitt/`](../eitt/). See [`../README.md`](../README.md) for the whole map.

## The rule for this tier

A hand-written test exists only for behaviour the conformance template does
*not* cover. Before adding one, look for a covering `TestCase` in
`../eitt/data/KnxConformanceTestTemplate-PointAPI.xml`; if one exists, the
eitt tier already tests it and the hand-written test must not be added.

A test that deliberately overlaps a template case — because it asserts
materially more than the template does (an exact response code where the
template accepts a class, CBOR field-level read-back, state across a reboot)
— is kept, but must carry the case id:

```python
@pytest.mark.eitt_overlap("5.3.8.1")
def test_write_at_and_verify(self, coap, oscore_ctx):
    ...
```

Tests here are named for the **behaviour** they test, never for an EITT id;
template overlap is recorded only by the marker and, where it helps, a
docstring starting "Overlaps EITT 5.x.y.z: ...". List the current overlaps
with:

```
pytest tests/runtime -m eitt_overlap --collect-only -q
```

The per-test classification behind the split is in
[`../docs/runtime-test-inventory.md`](../docs/runtime-test-inventory.md).

## Module order matters

Several modules leave device state behind that a later module relies on (the
reset and SPAKE2+ modules in particular), so execution order is **declared
explicitly** in `RUNTIME_MODULE_ORDER` in
[`../common/fixtures.py`](../common/fixtures.py), not derived from the file
names. A new module must be added to that list.

Within a module, order is definition order. A class is not the unit of
ordering — an individual test is: moving a class to satisfy one ordering
constraint can break another test inside it.

## Running in Docker (the only valid way)

`../common/tools/run-in-docker.ps1` builds the stack and runs the tests
inside the `itgesellschaft/knxiot-knx-ci` container with a veth pair — the
same setup as GitLab CI. Requires Docker Desktop running (Linux engine).

```powershell
# Both device tiers (the default)
.\tests\common\tools\run-in-docker.ps1

# One tier (-Tier all|eitt|runtime|harness, default all)
.\tests\common\tools\run-in-docker.ps1 -Tier runtime
.\tests\common\tools\run-in-docker.ps1 -Tier eitt
.\tests\common\tools\run-in-docker.ps1 -Tier eitt -Section 5.6

# A subset (any pytest -k filter)
.\tests\common\tools\run-in-docker.ps1 -PytestArgs "-k","Security::"

# A specific file, plus extra single-dash args
.\tests\common\tools\run-in-docker.ps1 tests/runtime/test_observe.py -x

# Test uncommitted/untracked changes (default tests only committed HEAD)
.\tests\common\tools\run-in-docker.ps1 -IncludeUncommitted -Tier runtime

# Include tests gated behind RUN_SLOW_TESTS (currently the ~60 s SPAKE2+
# brute-force auto-unblock test, which is skipped by default)
.\tests\common\tools\run-in-docker.ps1 -RunSlowTests -k "unblocks"
```

**Never run these natively on the Windows host.** The suite needs a veth
pair and a `dut` network namespace that only the Docker harness creates;
nothing warns you, collection and reporting look normal, and the result is
a pile of environment-induced failures that read as a code regression.

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
  single-dash args (`-k`, `-x`, `-v`) and file paths work. Pass them through
  `-PytestArgs` when you need double-dash ones.
- A `-k` expression must be a single token. `-k 'a or b'` is split by PowerShell
  before it reaches pytest and collects nothing; run each filter separately.
- Run it from PowerShell, not Git Bash: Git Bash's `tar` shadows Windows
  `tar.exe` and breaks `-IncludeUncommitted`, and `-PytestArgs "a","b"`
  arrives as one string.
- `-RunSlowTests` sets `RUN_SLOW_TESTS=1` in the container. Without it,
  `test_spake_lockout_unblocks_after_timeout` is skipped by a static `skipif` and
  never runs, even in a full suite.
- Some conditional tests skip under cumulative suite load rather than failing
  (e.g. the `test_unicast_discovery_*` malformed-length cases wait up to 20 s for a
  discovery GET). They pass when run in isolation; a skip there is a timing
  artefact, not a defect.
- The script builds fresh inside the container's filesystem (no bind mount),
  avoiding stale CMake cache issues.

## Running on Linux / CI directly

`../common/tools/run-local-ci.sh` does the same steps as the CI job (veth
pair, build, pytest) and is intended to run inside a Linux container or CI
runner. `../common/tools/run-local-ci-zephyr.sh` drives pytest against an
already-running external DUT (real Zephyr hardware or the virtual EITT GUI)
instead. See the comments at the top of each script.
