# EITT replacement GUI — plan (PySide6)

**Status: PLAN ONLY. No implementation yet.**

Goal: a **runner front-end** that replaces EITT for KNX-IoT Point API work —
browse the conformance template, run it against a DUT, and watch the telegrams
live in the same three-pane shape EITT uses. Not a general KNX tool: EITT covers
other protocols and profiles we do not care about.

## Why PySide6 rather than Tauri/Electron

The parsers already exist in Python and are the expensive part to rebuild:

| Existing asset | What the GUI gets for free |
|---|---|
| `eitt/model.py` `parse_eitt_xml()` | The whole left tree + telegram grid. **60 ms** for the full template (10 collections, 193 TestCases, 6,559 sequence items) |
| `eitt/resource_schema.py` | Decode `RawContent` CBOR into readable `CoAP Content`, per resource path |
| `data/EITT_KnxCommentCommandsScheme.xml` | The Telegram Properties pane for comment commands — Command/Description/Format/params, verbatim from EITT's own grammar |
| `eitt/runner.py` + `transport/` | The execution engine. This is the part a from-scratch GUI could never justify rewriting |
| `data/traces/*.xml` | Reference traces for the tracebuffer pane; largest is 2.1 MB |

A Tauri/Rust front-end would mean either reimplementing all of that or shipping
Python alongside it anyway. Revisit Tauri only if this ever needs distributing
to people who don't have the repo.

**Performance note:** the data is smaller than it looks — 6,559 rows worst case,
traces ≤2.1 MB. Virtualisation is about scroll smoothness, not survival, and Qt's
model/view is virtualised natively. A full template reparse is 60 ms, so
file-watch-and-reparse is viable; no incremental parsing needed.

## Layout (mirrors the two EITT views)

```text
┌────────────────────────┬──────────────────────────────────────────────┐
│ LEFT (tabbed)          │ RIGHT TOP: telegram grid, tab per collection │
│  • CTL: project tree   │  Nr | Active | Connection | Security | DPT | │
│    templates, "Copy of │  Direction | RF Info | SN/DoA | Type |       │
│    …", PREPARATIONs,   │  Method | URI Endpoint | URI Parameter |     │
│    Trace Storage       │  CoAP Content | Wait                         │
│  • Telegram Properties │  (comment rows interleaved inline)           │
│    (clicked row)       ├──────────────────────────────────────────────┤
│  • Interfaces/Filter/  │ RIGHT BOTTOM: tracebuffer                    │
│    Trigger/Conformance │  <AF NAK (TX) / >AF NAK (RX) + verdict       │
└────────────────────────┴──────────────────────────────────────────────┘
```

`QMainWindow` + `QSplitter`; left pane is a `QTabWidget` exactly as EITT's
bottom-left tab strip works. Selecting a template row OR a trace row drives the
same Telegram Properties pane (planned vs actual).

## Phases

### Phase 0 — prerequisite: structured telemetry (do this first)

**The runner cannot currently feed a tracebuffer.** `StepResult` carries only
`ok`/`reason`/`item`; every wire detail goes to stdout through 47 `_dbg_coap` /
`_dbg_oscore` / `_dbg_group` `print()` calls. Without fixing that, the GUI would
have to scrape its own subprocess output — the thing that makes EITT's own logs
painful.

- Add an event sink (`Protocol` with `on_telegram`, `on_step`, `on_case`), default
  a no-op so pytest behaviour is unchanged.
- Emit a structured record per telegram: direction, type, code, path, SecContext,
  mid, token, raw bytes, decoded body, kid/PIV, timestamp, eval result.
- Keep the `_dbg_*` prints as one sink implementation so `-CoapDebug` still works.
- **This is independently valuable** — it is also what would let us diff a run
  against `traces/5_X.xml` automatically instead of by eye.

### Phase 1 — read-only viewer

Tree + grid + properties, no execution. Proves the layout and the model/view
work, and is already useful for the template/trace/commands checks we do
constantly.

- `QAbstractItemModel` over `parse_eitt_xml()` output (do not copy into
  per-row widgets).
- Comment rows rendered inline, styled like EITT (blue-ish, no Nr).
- `Activate="no"` rows visually distinct — this matters, we have **97**
  deactivated CoAPMessages and mistaking them for live cost real time.
- Telegram Properties: attributes for a `CoAPMessage`; for a comment, look the
  command up in the scheme file and show Command/Description/Format/params.

### Phase 2 — trace viewer + planned-vs-actual

- Load `data/traces/*.xml` into the bottom pane, `TelNo`-ordered, TX/RX styled.
- Rows with `TelNo=None` are captured-but-not-awaited traffic — render them
  differently (this distinction is load-bearing and easy to lose).
- Select a template row → highlight the corresponding trace row and diff them.
  This is the single most useful feature for our workflow.

### Phase 3 — runner front-end

- Run in a `QThread`; **never** touch widgets from it — Qt signals only.
- Drive `SequenceRunner` in-process via the Phase 0 sink, so no output scraping.
- Grid decorates live: pass/fail per row, current row highlighted.
- Selection = scope: run a collection, a TestCase, or from a row.
- Surface what the harness already needs: docker vs local, `DEVICE_HOST`,
  `RUNTIME_TEST_SERVER_PREFIX` (netns), `KNX_MULTICAST_SCOPE`, `-TimeScale`,
  the debug switches.

### Phase 4 — the operator affordances EITT has and we substitute

- **`@@!` prompts**: EITT shows a modal and waits. Show the same modal, with a
  button that fires the mapped `/test/...` endpoint (or "I did it manually" for
  real hardware). Today `overrides.yaml`'s `manual_actions:` does this headlessly.
- **`@@[w` modal waits** and **`@[w` countdowns** in the toolbar, as EITT does.
- **`Active` checkbox editing** — EITT's enable/disable telegram, which the
  certification deviations list instructs operators to use (e.g. "disable 253,
  enable 254"). Writes go to the **copy**, never the master — see Decisions.
  Note those telegram NUMBERS won't match ours; identify the step by section +
  context instead.

## Explicit non-goals

- Other KNX protocols/profiles (TP, RF, classic) — IoT Point API only.
- Being bug-compatible with EITT's odd/legacy corners.
- Editing the read-only template. The three-layer variable model
  (template → variable-values JSON → generated copy) must be preserved.
- Replacing pytest. The GUI is a second front-end onto the same engine; CI keeps
  running headless, and the two must not diverge.

## Risks

1. **Two front-ends diverging.** Mitigate by making the GUI consume
   `SequenceRunner` through the Phase 0 sink only — no parallel execution path.
2. **Phase 0 touching the runner.** It is the code 121 passing tests depend on.
   Keep it purely additive: a default no-op sink, verified by a full green run
   before and after.
3. **Qt threading.** Any widget access off the GUI thread crashes rather than
   warns. Signals only, always.
4. **Trace format assumptions.** These are recorded EITT output, not a documented
   schema. Parse defensively; unknown attributes must not break a load.

## Decisions (settled 2026-07-26)

### The master XML stays read-only; edits go to a COPY

Mirror what EITT does — its project tree literally shows `5.4.1. POST /k V40`
alongside `Copy of 5.4.1. POST /k V40`, and the operator edits the copy.

- `data/KnxConformanceTestTemplate-PointAPI.xml` is **never written**. It is the
  shipped master and the interpreter already treats it as such.
- "Generate copy" writes a separate project file the GUI owns and may mutate
  (toggling `Active`, editing `<Content>`, etc.).
- This is the same three-layer variable model already documented
  (template → variable-values JSON → generated copy), so the copy is the natural
  home for operator edits. See `eitt-xml-interpreter.md` and
  the three-layer variable note.
- Consequence for the runner: it must accept a project path rather than assuming
  the master. `parse_eitt_xml(path)` already takes one, so this is plumbing, not
  redesign. `test_eitt.py` keeps pointing at the master so CI is unaffected.
- Open sub-question for implementation time: whether a copy is a full XML
  serialisation (faithful to EITT, diffable, but we must write valid EITT XML) or
  master + an edit overlay (cheaper, but diverges from EITT's model). Prefer full
  serialisation for fidelity; decide when Phase 4 is actually built.

### Both docker AND live DUT

Not either/or — a connection profile the user picks:

- **Docker** — `run-in-docker.ps1`: builds the server, veth pair, DUT in its own
  netns (`RUNTIME_TEST_SERVER_PREFIX`), ULA addresses, `DEVICE_HOST` injected.
  The GUI shells out and streams output.
- **Live DUT** — `EXTERNAL_DUT` + mDNS/CoAP multicast discovery
  (`conftest.discover_dut()` already does exactly this: multicast
  `GET /.well-known/core?ep=knx://sn.<serial>` → host/port/iface). The GUI needs a
  device picker fed by that discovery, plus the interface selection Windows makes
  necessary.
- Live DUT is where the `@@!` prompts become *real* modals ("power cycle the
  device, press OK") rather than a `/test/...` substitution — the control
  endpoints may not exist on certification hardware. The Phase 4 modal must
  therefore offer BOTH "fire the endpoint" and "I did it manually".

### Telegram numbers are dynamic — do not try to map the deviation numbers

EITT numbers telegrams **per generated project**, sequentially over the active
steps, so the numbers depend on which TestCases were generated and which steps
are active. Verified on `traces/5_4_1.xml`: numbering runs 1→325 with the only
gaps (99, 100) exactly where a deactivated pair sits (5.4.1.9's `Activate="no"`
discovery steps).

- So the deviations list's "disable telegram 253, enable 254" numbers will
  **never match our copy**, and the earlier "establish the mapping" open item is
  dropped as unachievable-by-design.
- What IS achievable: extrapolate **which test/telegram is meant** from the
  section plus surrounding context, and record that instead. E.g. 5.2's 253/254
  is the `:OSV`-vs-date/time pair; 5.10's 108–113 are the multi-IPv6 pagination
  cases.
- GUI implication: the `Nr` column is generated by us at load time over the
  active rows — display only, never an identifier. Anything durable (overrides,
  bookmarks, the manual-action map) must key on
  `TestCase.qualified_name` + message `ID` (the XML GUIDs), which are stable
  across regeneration. `overrides.yaml` already does this correctly.
