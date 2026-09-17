# Runtime conformance tests

Python/pytest tests that exercise the full stack over real CoAP/OSCORE on IPv6,
mimicking EITT certification. A `runtime_test_server` (built from
`runtime_test_server.c`) is the device under test (DUT).

The tests are split into three tiers, one directory each. The shared
infrastructure they all build on — `conftest.py`, `transport/`, `eitt/`,
`gui/`, `data/`, `tools/` — stays at this level.

| Tier | Directory | What it is | Needs a DUT |
| --- | --- | --- | --- |
| eitt | `eitt_tests/` | `test_eitt.py`: one generated pytest item per EITT TestCase, read from the conformance template at collection time | yes |
| runtime | `runtime_tests/` | hand-written device tests for behaviour the template does not cover (CoAP option/payload hardening, SWU edge cases, observe, ...) | yes |
| harness | `harness_tests/` | tests of the interpreter and the GUI themselves | no |

**The rule for the runtime tier:** a hand-written test exists only for
behaviour the conformance template does *not* cover. Before adding one, look
for a covering `TestCase` in `data/KnxConformanceTestTemplate-PointAPI.xml`; if
one exists, the eitt tier already tests it and the hand-written test must not
be added. A test that deliberately overlaps a template case — because it
asserts materially more than the template does (an exact response code where
the template accepts a class, CBOR field-level read-back, state across a
reboot) — is kept, but must carry the case id:

```python
@pytest.mark.eitt_overlap("5.3.8.1")
def test_write_at_and_verify(self, coap, oscore_ctx):
    ...
```

Runtime-tier tests are named by behaviour, never by EITT id; EITT overlaps
are recorded only with `@pytest.mark.eitt_overlap`. List the current
overlaps with
`pytest tests/runtime/runtime_tests -m eitt_overlap --collect-only -q`.
The per-test classification behind the split is in
`tests/docs/runtime-test-inventory.md`.

`eitt_tests/test_eitt.py` generates its items directly from
`data/KnxConformanceTestTemplate-PointAPI.xml` via `pytest_generate_tests`,
driven by the interpreter in `eitt/`. See `tests/docs/eitt-xml-interpreter.md`
for the full design.

## Layout

```
conftest.py            pytest fixtures: builds/starts the DUT, CoAP + OSCORE sessions
pytest.ini             puts this directory on sys.path so the tiers can import
                       conftest / transport / eitt / gui
runtime_test_server.c  the device under test
eitt_tests/            EITT tier — test_eitt.py
runtime_tests/         runtime tier — behaviour-named test modules
harness_tests/         harness tier — test_events.py, test_comment_scheme.py,
                       test_project_copy.py, test_gui_*.py
eitt/                  the EITT XML interpreter (model, runner, provisioning,
                       hooks, content matching, JSON<->CBOR schema, overrides)
gui/                   the PySide6 runner front-end
transport/             wire-level clients: CoAP, OSCORE, SPAKE2+, mDNS
data/                  EITT inputs: the conformance template, EITT's own key
                       maps + comment-command scheme, the variable-values
                       JSON, the SWU firmware dummy, and traces/ (9 reference
                       runs recorded by the real tool)
tools/                 run scripts (Docker + local CI) and SPAKE2+ record helpers
```

## Running in Docker (recommended)

`tools/run-in-docker.ps1` builds the stack and runs the tests inside the
`itgesellschaft/knxiot-knx-ci` container with a veth pair — the same setup as GitLab CI.
Requires Docker Desktop running (Linux engine).

```powershell
# Run the whole runtime suite (all three tiers)
.\tests\runtime\run-in-docker.ps1

# Run one tier (-Tier all|eitt|runtime|harness, default all)
.\tests\runtime\run-in-docker.ps1 -Tier eitt
.\tests\runtime\run-in-docker.ps1 -Tier runtime
.\tests\runtime\run-in-docker.ps1 -Tier eitt -Section 5.6

# Run a subset (any pytest -k filter)
.\tests\runtime\run-in-docker.ps1 -PytestArgs "-k","Security::"   # one section
.\tests\runtime\run-in-docker.ps1 -PytestArgs "-k","5.3.17.6"     # a single TestCase

# Run a specific file, pass extra single-dash args
.\tests\runtime\run-in-docker.ps1 tests/runtime/runtime_tests/test_observe.py -x

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
  `test_spake_lockout_unblocks_after_timeout` is skipped by a static `skipif` and
  never runs, even in a full suite.
- Some conditional tests skip under cumulative suite load rather than failing
  (e.g. the `test_unicast_discovery_*` malformed-length cases wait up to 20 s for a
  discovery GET). They pass when run in isolation; a skip there is a timing
  artefact, not a defect.
- The script builds fresh inside the container's filesystem (no bind mount),
  avoiding stale CMake cache issues.

## Running on Linux / CI directly

`run-local-ci.sh` does the same steps as the CI job (veth pair, build, pytest)
and is intended to run inside a Linux container or CI runner. See the comments
at the top of that script for `git archive` invocation examples.
