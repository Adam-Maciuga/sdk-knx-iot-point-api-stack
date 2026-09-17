# EITT replacement GUI

A PySide6 front-end onto the same conformance engine pytest uses. Plan and
rationale: [`tests/docs/eitt-gui-plan.md`](../../docs/eitt-gui-plan.md).

```sh
cd tests/runtime
pip install -r gui/requirements.txt
python -m gui.eitt_gui                      # the shipped master template
python -m gui.eitt_gui path/to/project.xml  # any EITT project XML
```

## Status

| Phase | What | State |
|---|---|---|
| 0 | Structured telemetry (`eitt/events.py`) | **done** |
| 1 | Read-only viewer: tree, telegram grid, properties | **done** |
| 1b | Checkboxes + copy-over; editable working copies | **done** |
| 2 | Trace viewer + planned-vs-actual diff | not started |
| 3 | Runner front-end (execute against a DUT) | not started |
| 4 | Operator affordances (`@@!` modals, `Active` editing) | not started |

Phases 0-1b verified in Docker at **214 passed / 9 skipped / 2 xfailed**
(8:43, exit 0) — the 177-case conformance suite is unchanged; the rest are
the headless tests below, which need no DUT and so run in the image too.

## Using it

**Left pane, `CTL (template)`** — the shipped master, strictly read-only.
Tick collections/suites/cases (tri-state) and press *Copy checked to new
project*, or right-click any node to copy just that scope.

**Left pane, `Copies (n)`** — your working copies, each a root in one tree.
Editable. Remove via the button or right-click.

**Double-click** anything in either tree to open it in the grid on the
right. A collection opens **all** its cases end to end, separated by a bold
row naming each; numbering runs continuously across the whole scope, as
EITT numbers a generated project. The header says which you are looking at:

```text
[READ-ONLY]  5.2. /.well-known/knx  —  19 test cases, 612 steps.
[EDIT]       Copy of 5.2 …          —  editable copy. Toggle 'Active' …
```

**`Active` appears on telegrams only** (`CoAPMessage`/`DnsMessage`) — it is
a telegram attribute, and comments do not carry one. Toggling it in a copy
renumbers immediately, since `Nr` is assigned over the active rows.

## Why Qt is not in `requirements.txt`

`tests/runtime/requirements.txt` is what the Docker conformance image
installs. The GUI is a *second front-end*, not part of the engine, so its
dependency lives in `gui/requirements.txt` and the headless image stays
Qt-free. Nothing under `gui/` may be imported by the conformance suite.

## Design constraints

**One engine, two front-ends.** The GUI drives `eitt.runner.SequenceRunner`
through the `eitt.events` sink. It must never grow a parallel execution
path — that is the main risk the plan calls out, and it is what would let a
GUI run and a CI run disagree.

**Telemetry can never fail a test.** `EventBus` swallows sink exceptions by
design. A consumer is an observer; a GUI crash must not turn a passing
conformance test red. The default sink is `NullBus`, whose `active` is
`False`, so a pytest run builds no records at all.

**The master template is read-only.** `data/KnxConformanceTestTemplate-PointAPI.xml`
is never written. Operator edits (phase 4) go to a generated copy, mirroring
what EITT itself does with its "Copy of …" projects.

**`Nr` is display only.** EITT numbers telegrams per generated project over
the active rows, so the numbers shift when activation changes and can never
be mapped onto the certification deviations list. Anything durable keys on
the XML `ID` GUID instead.

## Layout

```
gui/
  eitt_gui.py     entry point (python -m gui.eitt_gui)
  main_window.py  QMainWindow, splitters, properties pane
  models.py       ProjectTreeModel / SequenceTableModel
```

Supporting modules that live in `eitt/` because they are useful without Qt:

- `eitt/events.py` — the telemetry bus (phase 0)
- `eitt/comment_scheme.py` — reader for EITT's own `@`-command grammar,
  which drives the properties pane for comment rows

## Tests

Headless, no DUT and no Docker needed:

```sh
cd tests/runtime
python -m pytest test_events.py test_comment_scheme.py test_gui_models.py -q
```

`test_gui_models.py` sets `QT_QPA_PLATFORM=offscreen` and skips itself when
PySide6 is absent, so it is inert inside the Docker image.
