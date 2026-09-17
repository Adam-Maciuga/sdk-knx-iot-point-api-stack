# Tests

Five tiers, one directory each. Two of them are pytest tiers over a live
device; the other three are compiled, or shared code, or neither.

| Tier | Directory | What it is | Needs a DUT | Run with |
| --- | --- | --- | --- | --- |
| unit | [`unit/`](unit/) | Google Test, C++ exercising the C functions directly | no | `ctest --preset linux-test-gcc` |
| fuzz | [`fuzz/`](fuzz/) | libFuzzer harnesses over the pre-auth parsers, clang only | no | `ctest --preset linux-fuzz-clang` |
| common | [`common/`](common/) | shared test infrastructure — **no tests live here** | — | — |
| eitt | [`eitt/`](eitt/) | the conformance template, interpreted: one pytest item per EITT `TestCase` | yes | `run-in-docker.ps1 -Tier eitt` |
| runtime | [`runtime/`](runtime/) | hand-written device tests for behaviour the template does *not* cover | yes | `run-in-docker.ps1 -Tier runtime` |

`eitt/` also holds the harness tier — [`eitt/harness_tests/`](eitt/harness_tests/),
tests of the interpreter and the GUI themselves, which need no DUT and no
Docker (`pytest tests/eitt/harness_tests -q`).

## The two pytest tiers

```
tests/
  pytest.ini        rootdir ini: pythonpath = . common eitt ; testpaths = eitt runtime
  conftest.py       thin — re-exports everything common/fixtures.py defines,
                    because pytest reads fixtures and hooks only from a conftest

  common/           shared by both tiers, holds no tests
    fixtures.py       fixtures, constants, helpers and the pytest hooks
                      (--section, tier ordering); builds and starts the DUT,
                      performs SPAKE2+ and OSCORE provisioning
    transport/        wire-level clients: CoAP, OSCORE, SPAKE2+, mDNS
    tools/            run-in-docker.ps1, run-local-ci*.sh, SPAKE2+ helpers
    runtime_test_server.c   the device under test
    requirements.txt  what the Docker conformance image installs

  eitt/             the template-driven tier
    test_eitt.py      generates one item per TestCase at collection time
    interpreter/      the EITT XML interpreter (model, runner, provisioning,
                      hooks, content matching, JSON<->CBOR schema, overrides)
    gui/              the PySide6 runner front-end
    data/             the conformance template, EITT's key maps and
                      comment-command scheme, the variable-values JSON, the
                      SWU firmware dummy, traces/ (reference runs)
    harness_tests/    interpreter and GUI self-tests, no DUT

  runtime/          the hand-written tier — behaviour-named test_*.py only
```

Both tiers import the shared code by plain module name — `from fixtures
import DUT_IA`, `from transport.coap_client import CoapClient`, `from
interpreter.runner import SequenceRunner`, `from gui.main_window import
MainWindow`. That works from any working directory because `pytest.ini`'s
`pythonpath = . common eitt` puts `tests/`, `tests/common/` and
`tests/eitt/` on `sys.path` for every run, whichever tier is selected.

The interpreter package is named `interpreter`, not `eitt`: a package named
`eitt` inside a directory named `eitt` that is itself on `sys.path` would
shadow itself.

## Running

Always in Docker for anything that needs the DUT — the suite needs a veth
pair and a `dut` network namespace that only the Docker harness creates, and
a host-native run fails in ways that look like code regressions:

```powershell
.\tests\common\tools\run-in-docker.ps1                 # both device tiers, each against a fresh DUT
.\tests\common\tools\run-in-docker.ps1 -Tier eitt
.\tests\common\tools\run-in-docker.ps1 -Tier runtime
.\tests\common\tools\run-in-docker.ps1 -Tier eitt -Section 5.6
.\tests\common\tools\run-in-docker.ps1 -IncludeUncommitted -Tier runtime
```

The same from Linux, macOS or Git Bash (`--help` lists every option):

```bash
tests/common/tools/run-in-docker.sh                    # both device tiers
tests/common/tools/run-in-docker.sh --tier eitt --section 5.6
tests/common/tools/run-in-docker.sh --tier runtime --uncommitted
tests/common/tools/run-in-docker.sh --tier harness     # no device needed
```

Both wrappers pack the tree (committed HEAD, or the working tree with
`-IncludeUncommitted` / `--uncommitted`), start the CI image privileged and run
`common/tools/run-local-ci.sh` inside it. That script is the in-container half
and takes the same `--tier` / `--section` options, so a Linux CI runner can
call it directly.

See [`runtime/README.md`](runtime/README.md) for the runner's options and
gotchas, and [`eitt/README.md`](eitt/README.md) for the EITT tier.
