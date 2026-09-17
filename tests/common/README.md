# Shared test infrastructure

Everything the two device test tiers — [`../eitt/`](../eitt/) and
[`../runtime/`](../runtime/) — build on. **No tests live here**, and
`tests/pytest.ini`'s `testpaths` never names this directory, so pytest
never collects from it.

| | |
| --- | --- |
| `fixtures.py` | the fixtures, constants, helpers and pytest hooks both tiers use: builds and starts the DUT, waits for `RUNTIME_TEST_SERVER_READY`, performs the SPAKE2+ handshake and OSCORE provisioning, and yields the CoAP endpoint. Also `pytest_addoption` (`--section`) and `pytest_collection_modifyitems` (the tier ordering). |
| `transport/` | wire-level clients, no test knowledge: `coap_client`, `knx_oscore`, `knx_spake2plus`, `mdns_client`. |
| `tools/` | `run-in-docker.ps1` (the supported way to run either device tier), `run-local-ci.sh`, `run-local-ci-zephyr.sh`, and the SPAKE2+ record generator/verifier. |
| `runtime_test_server.c` | the headless CoAP device under test. Built by `tests/CMakeLists.txt` to `build/<preset>/tests/runtime_test_server`. |
| `requirements.txt` | what the Docker conformance image installs. Qt is deliberately absent — the GUI's own dependency lives in `../eitt/gui/requirements.txt`. |

## How the tiers reach it

`tests/pytest.ini` sets `pythonpath = . common eitt`, relative to the ini
file, so for every run — whichever tier is selected, whatever the working
directory — `sys.path` carries:

| entry | provides |
| --- | --- |
| `tests/` | `conftest.py` |
| `tests/common/` | `fixtures`, `transport.*` |
| `tests/eitt/` | `interpreter.*`, `gui.*` |

so a test writes `from fixtures import DUT_IA, set_lsm` and `from
transport.coap_client import CoapClient` and gets this directory.

## Why `fixtures.py` and not `conftest.py`

pytest collects fixtures and hooks **only** from a file named
`conftest.py`, and a conftest is not importable under a stable module name
from a sibling directory. Both are needed here: the tiers import constants
and helpers out of it by name (`from fixtures import MC_SCOPE`), *and*
pytest has to see its fixtures and hooks.

So the definitions live in this plain, importable module, and
`tests/conftest.py` is a two-line file that re-exports them into the
namespace pytest reads. Add a new `pytest_*` hook here and it must also be
named explicitly in `tests/conftest.py`'s import list.
