# EITT tier — the conformance template, interpreted

`test_eitt.py` reads KNX's official conformance template at collection time
and generates **one pytest item per `TestCase`** in it, then drives each one
over real CoAP/OSCORE against the DUT. Nothing here is hand-written per test
case: adopting a new template revision means dropping in a new XML file, not
porting tests. The drop-in and everything around it (companion files,
overrides, traces, baselines) is the procedure in
[`tests/docs/eitt-template-update.md`](../docs/eitt-template-update.md).

The full design is in
[`tests/docs/eitt-xml-interpreter.md`](../docs/eitt-xml-interpreter.md).

## Layout

```
test_eitt.py      the tier: pytest_generate_tests over the template
interpreter/      the EITT XML interpreter
                    model.py           parse the template into a project tree
                    runner.py          execute a sequence of telegrams
                    provisioning.py    EITT's @/@@[ comment commands
                    hooks.py           the multi-step preparations (SWU, ...)
                    content_match.py   response body matching
                    resource_schema.py JSON <-> CBOR key maps
                    variables.py       the variable table
                    overrides.py       + overrides.yaml, the local deviations
                    comment_scheme.py  EITT's own comment-command table
                    events.py          the TX/RX event bus both front-ends use
                    project_copy.py    scoped copies for the GUI
                    spake_flow.py      the SPAKE2+ span
gui/              the PySide6 runner front-end (see gui/README.md)
data/             the conformance template, EITT's key maps and
                  comment-command scheme, the variable-values JSON, the SWU
                  firmware dummy, traces/ (9 reference runs recorded by the
                  real tool)
harness_tests/    the harness tier: tests of the interpreter and the GUI
                  themselves. No DUT, no Docker.
```

The shared infrastructure both device tiers use — `fixtures.py`,
`transport/`, `tools/`, `runtime_test_server.c` — lives in
[`../common/`](../common/).

## The master template is read-only

`data/KnxConformanceTestTemplate-PointAPI.xml` must **never** be edited, not
even temporarily. To check that an assertion really bites, point the
interpreter at a scratch copy instead:

```
RUNTIME_TEST_EITT_XML=/path/to/mutated-copy.xml pytest tests/eitt/test_eitt.py
```

Device-specific deviations belong in `interpreter/overrides.yaml`, which is
applied over the read-only template at load time. There are three layers —
the template, the variable-values JSON, and the generated copy — and none of
them is the others' default; see
[`tests/docs/memory/reference-eitt-three-layer-variable-model.md`](../docs/memory/reference-eitt-three-layer-variable-model.md).

## Running

```powershell
# The whole tier, in Docker (the only valid way to run it — see below)
.\tests\common\tools\run-in-docker.ps1 -Tier eitt

# One section. Use -Section, NOT -k: pytest's -k is a substring match, so
# `-k 5.6` also drags in 5.5.5.6 and 5.5.6.1, which then run without their
# sibling preparation TestCases and produce fabricated failures.
.\tests\common\tools\run-in-docker.ps1 -Tier eitt -Section 5.6
.\tests\common\tools\run-in-docker.ps1 -Tier eitt -Section 5.5,5.6

# The harness tier needs neither a DUT nor Docker:
python -m pytest tests/eitt/harness_tests -q
```

**Never run the device part of this tier natively on the host.** It needs a
veth pair and a `dut` network namespace that only the Docker harness
creates; a host-native run silently produces ~23 environment-induced
failures and takes ~4x as long, and looks exactly like a code regression.
The wall clock is the fastest tell.

## The GUI

`gui/` is a second front-end onto the same engine, driving
`interpreter.runner.SequenceRunner` exactly as pytest does. Qt is
deliberately not in `../common/requirements.txt`, so the headless Docker
image stays Qt-free.

```sh
cd tests/eitt
pip install -r gui/requirements.txt
python -m gui.eitt_gui
```

See [`gui/README.md`](gui/README.md) and
[`tests/docs/eitt-gui-plan.md`](../docs/eitt-gui-plan.md).
