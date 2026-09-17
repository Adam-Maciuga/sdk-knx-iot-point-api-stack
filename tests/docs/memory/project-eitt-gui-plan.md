---
name: project-eitt-gui-plan
description: "PySide6 GUI replacing EITT — phases 0 (telemetry bus) and 1 (read-only viewer) IMPLEMENTED 2026-07-27; phases 2-4 still plan only. Plan at tests/docs/eitt-gui-plan.md, usage at tests/runtime/gui/README.md"
metadata: 
  node_type: memory
  type: project
  originSessionId: 4d0fc175-3a82-427c-bd95-25153f25e4ad
  modified: 2026-07-29T07:43:16.242Z
---

A **PySide6 GUI replacing EITT** for KNX-IoT Point API work — a **runner
front-end**, not only a viewer. Planned 2026-07-26; **phases 0 and 1 built
2026-07-27** on branch `feature/runtime_tests_v2`.

| Phase | State | Where |
| --- | --- | --- |
| 0 — structured telemetry | **done** | `tests/runtime/eitt/events.py` |
| 1 — read-only viewer | **done** | `tests/runtime/gui/` |
| 1b — copy-over + editable copies | **done** | `tests/runtime/eitt/project_copy.py` |
| 2 — LIVE tracebuffer (not a saved-trace viewer) | **done** | `gui/trace.py`, `gui/trace_view.py` |
| 3 — runner front-end | **done** | `gui/runner_thread.py` |
| 4 — `@@!` operator modals | **done** | `gui/main_window.py` |
| — | still open | loading a saved trace; persisting the CTL screen + selection |

## ⚠ NOT Docker-verified as of 2026-07-29

Commits `a7382ba4` + `71d94bd4` are **unverified against the conformance
suite**. `a7382ba4` touches `conftest.py`, `runner.py`, `coap_client.py` and
`hooks.py` — all shared with the 177-case suite — and **F15 changes what
goes on the wire**: nine TestCases that previously emitted no discovery
traffic now send six telegrams each. 5.1.1.x is *about* discovery, so this
could plausibly shift a result. **Run Docker before building further.**

194 headless tests pass (`test_gui_*.py`, `test_events.py`,
`test_project_copy.py`, `test_comment_scheme.py`) — none need a DUT.

## EITT's pane names are INVERTED — do not "fix" them

| EITT tab | What it holds |
| --- | --- |
| **`Conformance Tests`** | the read-only MASTER template |
| **`CTL`** | the editable working COPIES |

"CTL" reads like it should be the master; it is not. Confirmed from
screenshots of the real tool 2026-07-27. `Interfaces`/`Filter`/`Trigger`
exist in EITT but the user said they are not relevant to us. Tab strip runs
along the BOTTOM of the left pane.

Properties pane is a two-column grouped table, and **greying is per-row,
not a fixed list**: send-side fields (Accepted Format, Invalid Auth Tag,
Critical Option, response timeout, Random Kid, Last RX Token) are live on an
`IN` request and greyed on an `OUT` response; `Expected Kid Context` is the
mirror. A Comment gets only four rows.

## Two things the user corrected — both were MY wrong assumption

**Preparation is REFERENCED, never prepended.** Each TestCase that needs one
carries an inline `@#"Copy of AUTH PREPARATION"` comment *inside its own
sequence*, at the point it is needed (63 of 193 cases; one reference sits at
index 371 of 429). The referenced name does NOT exist in the XML — only
`AUTH PREPARATION` does — which is EITT telling us its workflow: template
cases reference the OPERATOR's copies. So copy-over copies cases verbatim
and pulls referenced preparations in as siblings, transitively, names
preserved.

**`Activate` is a TELEGRAM attribute.** All 3109 CoAPMessages and all 41
DnsMessages carry it; only 2 of 3409 Comments do. `model.py` defaults a
missing one to yes — right for execution, but the view read that default
back and put a meaningless checkbox on 3407 comments. EITT shows the column
for telegrams only.

**Lesson:** when the user says "EITT only does X", count it in the template
before agreeing OR disagreeing. Both times the measurement settled it in one
command and contradicted what I had inferred from aggregates.

Plan: `tests/docs/eitt-gui-plan.md`. Usage/design constraints:
`tests/runtime/gui/README.md`. Launch with `python -m gui.eitt_gui` from
`tests/runtime/`.

**Verified at 199 passed / 9 skipped / 2 xfailed in 8:47 (Docker).** The
177-case conformance suite is unchanged by phase 0; the extra passes are new
headless tests. Run them without a DUT:
`pytest test_events.py test_comment_scheme.py test_gui_models.py`.

## What phase 0 actually gives you

`SequenceRunner(..., sink=...)` emits `Telegram` / `StepEvent` / `CaseEvent`.
Default is `NullBus` (`active` is False) so pytest builds no records at all.
**A sink can never fail a test** — `EventBus` swallows sink exceptions by
design. This is also what would let a run be diffed against
`data/traces/5_X.xml` automatically instead of by eye.

Also added `eitt/comment_scheme.py`, a Qt-free reader for EITT's own
`@`-command grammar. Useful beyond the GUI: it's the right place to look up
an unrecognised `@`-command (see [[feedback-eitt-comment-commands-are-behavioural]]).
Lookup is **longest-prefix-first** — the 27 commands share prefixes (`@`,
`@@`, `@@!`, `@@[`, `@@[w`, `@@[pahdev`), so a naive `startswith` scan
returns `@` for everything.

## Traps hit while building it — don't repeat

- **Qt must NOT go in `tests/runtime/requirements.txt`.** That file is what
  the Docker conformance image installs. GUI deps live in
  `gui/requirements.txt`; nothing under `gui/` may be imported by the suite.
- **`pytest.importorskip` is not enough for an optional dep.** It only
  converts `ModuleNotFoundError`; an installed-but-broken Qt raises plain
  `ImportError` and kills collection for the WHOLE session. Use
  `try/except ImportError` + `pytest.skip(allow_module_level=True)`.
- **Never hardcode `data/...` relative paths in tests.** The Docker harness
  runs pytest from the repo root; resolve against `__file__`. This cost a
  full 9-minute run.

## Decisions settled by the user 2026-07-26 — don't re-ask

1. **Master XML stays READ-ONLY; edits go to a generated COPY**, as EITT
   does. `MainWindow(template=...)` already accepts any project path
   (verified), so phase 4's plumbing exists. Remaining choice: full XML
   serialisation of the copy (preferred) vs master + edit overlay.
2. **BOTH docker AND live DUT**, as a connection profile. Live DUT is where
   `@@!` becomes a REAL modal — certification hardware may lack the
   `/test/...` endpoints — so the phase 4 dialog must offer both "fire the
   endpoint" and "I did it manually".
3. **Telegram numbers are dynamic and unmappable** — `Nr` is display-only,
   generated at load over the ACTIVE rows. Anything durable keys on
   `qualified_name` + the message's XML `ID` GUID. See
   [[reference-eitt-certification-accepted-deviations]].
4. **PySide6, not Tauri/Electron** — the parsers are already Python and are
   the expensive part. Revisit only if it must ship without the repo.

**Biggest standing risk:** two front-ends diverging. The GUI must drive
`SequenceRunner` through the event sink — never a parallel execution path.

Suite status: [[project-eitt-5-3-status]]. Always run in Docker:
[[feedback-eitt-always-run-in-docker]].
