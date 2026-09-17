# EITT replacement GUI — plan (PySide6)

**Status: phases 0 and 1 implemented (2026-07-27). Phases 2-4 still plan only.**

| Phase | State | Where |
|---|---|---|
| 0 — structured telemetry | **done** | `tests/runtime/eitt/events.py` |
| 1 — read-only viewer | **done** | `tests/runtime/gui/` |
| 2 — trace viewer + diff | plan only | — |
| 3 — runner front-end | plan only | — |
| 4 — operator affordances | plan only | — |

Verified at **214 passed / 9 skipped / 2 xfailed in 8:43** in Docker
(exit 0). The conformance suite is unchanged — it was 177 passed, and the
additional passes are the new headless tests, which run in the image too
because they need no DUT. Usage and design constraints:
`tests/runtime/gui/README.md`.

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

### Phase 0 — prerequisite: structured telemetry — IMPLEMENTED

Built as `eitt/events.py`: `Telegram` / `StepEvent` / `CaseEvent` records,
a `RunSink` Protocol, and an `EventBus` that fans out to zero or more sinks.
`SequenceRunner(..., sink=...)` is the opt-in; the default is `NullBus`,
whose `active` is `False`, so a pytest run builds no records at all.

Three properties are pinned by `test_events.py` (headless, no DUT):

- constructing a runner the way `test_eitt.py` does leaves telemetry off;
- the same TestCase run with and without a sink drives the CoAP client
  **identically** (the additive-only regression guard);
- a sink that raises cannot fail a run — `EventBus` swallows sink
  exceptions and records them in `errors`. A telemetry consumer is an
  observer, and a GUI crash must never turn a passing conformance test red.

One ordering bug found and fixed while wiring it: `_last_request_path` was
assigned *after* `_remember_response()`, so every emitted response would
have been labelled with the previous request's path.

The `_dbg_*` prints were left exactly as they are. They are orthogonal to
the bus, `-CoapDebug` still works, and folding them in would have meant
touching the code 177 passing tests depend on for no gain.

**Original rationale.** The runner could not feed a tracebuffer: `StepResult` carries only
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

### Phase 1 — read-only viewer — IMPLEMENTED

`gui/models.py` (`ProjectTreeModel`, `SequenceTableModel`) + `gui/main_window.py`,
launched with `python -m gui.eitt_gui`. Everything below was built as
specified; `test_gui_models.py` covers it offscreen.

Two things worth recording from the build:

- **Qt is NOT in `tests/runtime/requirements.txt`.** That file is what the
  Docker conformance image installs, and it must stay Qt-free — the GUI
  dependency lives in `gui/requirements.txt`. `test_gui_models.py`
  `importorskip`s PySide6, so it is inert in the image.
- **The comment-command grammar got its own reader**, `eitt/comment_scheme.py`,
  because the properties pane needs it and it is useful without Qt (it is
  also the right place to look up an unrecognised `@`-command, per F7/F8).
  The one subtlety: the 27 commands share prefixes (`@`, `@@`, `@@!`,
  `@@[`, `@@[w`, `@@[pahdev`), so lookup resolves **longest-first** — a
  naive `startswith` scan returns `@` for every comment in the template.

Original scope, all delivered:

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

## Phase 1b — template is a SOURCE; copy-over makes it runnable

Settled with the user 2026-07-27. The CTL tab showing the master template is
**strictly read-only**: no editing, no launching, no run controls. Work
happens on copies.

- **Checkboxes on the tree**, at every level — collection, suite, TestCase —
  tri-state so a checked parent implies its children.
- **Right-click → "Copy to new project"** on any node. The scope is the node
  acted on: right-clicking `5.2` copies the whole collection.
- **A separate copy each time**, EITT-style (`Copy of 5.4.1. POST /k V40`
  sits alongside the original in EITT's own project tree). Copies are listed
  and switched between; they do not merge.
- Copies are editable and launchable. The master is never written.

### Preparation is REFERENCED, never prepended — verified 2026-07-27

An early assumption that a copy gets its preparation concatenated at the top
was **wrong**. Measured against the template:

- The 4 preparations (`NEW PROJECT SETUP`, `AUTH PREPARATION`,
  `IA PREPARATION`, `TABLE PREPARATION`) live in ONE dedicated collection,
  "Test Sequence Preparation". No 5.x collection contains its own.
- **63 of 193 TestCases** reach them via an inline `@#"..."` comment inside
  their own sequence; the other 130 need none.
- The reference sits **where the test needs it, not at the top**: only 7 of
  131 references are at index ≤2, and one sits at index **371 of 429**
  (5.3.20.1). Prepending would run preparation at the wrong point and change
  what the test does.
- Reference counts: `Copy of AUTH PREPARATION` ×65, `Copy of IA PREPARATION`
  ×46, `Copy of TABLE PREPARATION` ×20, plus two upload-block sequences.
- **The referenced name is `"Copy of AUTH PREPARATION"`, which does not
  exist in the XML** — only `AUTH PREPARATION` does. This is the tell for
  EITT's intended workflow: the template's cases are written to reference
  *the operator's copies*, so in EITT you copy the preparations into your
  working project first. `PreparationRegistry.resolve()` already handles
  this by stripping the `Copy of ` prefix and falling back.

Consequences for copy-over:

1. Copy the selected cases **verbatim** — no injection, no rewriting. Their
   `@#` references are already correct.
2. Scan the copied scope for `@#"..."` / `@>w"...;"` references and bring
   those preparation sequences into the copy **as siblings** (their own
   suite, as in the template), never concatenated.
3. **Preserve names exactly** — resolution is by name, so renaming a copied
   preparation breaks every reference to it.
4. Resolution must be **transitive** — a preparation may itself reference
   another. Measured on the real collections:

   | Copied scope | Cases | Pulled in |
   |---|---|---|
   | `5.1. Discovery` | 42 → 45 | AUTH, IA, TABLE |
   | `5.3. Security` | 45 → 48 | AUTH, IA, TABLE |
   | `5.2. /.well-known/knx` | 19 → 24 | AUTH, IA, TABLE, Upload Block First/Second Half |

   Note 5.1 *does* reference all three preparations (via 5.1.1.1, 5.1.2.2
   and 5.1.2.4) — an earlier claim here that it needed none was wrong.
   Nothing was left unresolved in any scope.

### EITT's pane names are INVERTED from what they suggest

Confirmed from screenshots of the real tool 2026-07-27. Use EITT's names,
not our own, or every EITT user loses their muscle memory:

| EITT tab | What it actually holds |
|---|---|
| **`Conformance Tests`** | the read-only MASTER template — the source |
| **`CTL`** | the editable working COPIES |

That is the opposite of what the words suggest in isolation ("CTL" reads
like the master), which is why it is written down here and in a comment at
the tab construction site. `Interfaces` / `Filter` / `Trigger` exist in EITT
but are not relevant to us and are deliberately not reproduced.

The tab strip runs along the **bottom** of the left pane, as EITT has it.

### Properties pane: a two-column table, greyed PER MESSAGE

Not a text block — EITT shows a grouped property grid, and it is genuinely
easier to scan. Three rules taken from the real screens:

- **A comment gets four rows** (`Command` / `Description` / `Format` /
  `Text`) plus the long description as a footer — not the telegram
  treatment. It carries no protocol.
- **Greying depends on the ROW, not a fixed list.** On an `OUT 2.05` the
  `Accepted Format`, `Invalid Auth Tag`, `Critical Option Number/Value`,
  `Wait for Response Timeout`, `Use Random Kid Context` and `Use Last RX
  Token` rows are greyed — every one of them live on the `IN GET` above it.
  They instruct the TOOL while SENDING, so they mean nothing on a row
  describing what the DUT should send back. `Expected Kid Context` is the
  mirror: greyed on a request, live (`<no check>`) on a response.
- **The footer is a separate area below the grid**, holding the resolved
  telegram and its payload:

  ```text
  ACK: 2.05 [Content] BDUT

  [Data:] </fp/g/1>;ct=60
          </fp/g/2>;ct=60
  ```

  Note EITT names response codes (`2.05 [Content]`) and renders the
  template's escaped `<\/fp\/g\/1>` as `</fp/g/1>`. The unescaping is
  DISPLAY ONLY — `content_match` still compares against the raw text.

### Sequence grid: EITT's columns, and colour PER ROW by protocol

Corrected from screenshots 2026-07-27/28.

**Columns**, in EITT's order:

```text
Nr | Active | Connection | Security | DPT | Direction | RF Info | SN / DoA
   | Type | Method | URI Endpoint | URI Parameter | CoAP Content | Wait
```

`RF Info` and `SN / DoA` are always empty for the Point API but are KEPT —
the operator's eye learns column positions, and dropping two shifts
everything after them. The last three are **relabelled per protocol**, as
EITT does: with mDNS rows on screen they read `Service (OUT)` /
`Service (IN)` / `Record Data`.

**Comments and case separators do NOT follow the columns.** They start at
`Nr` and span the full width. Putting a comment in the content column
instead (the first attempt) buries it far right where the eye never goes.
Note `setSpan` is sticky across `setModel`, so it must be CLEARED per row,
not merely set — a stale span hides every column but the first.

**Colour is per ROW, by connection protocol** — COAP green, mDNS violet —
so a block of mDNS steps is spottable without reading a word. A
per-column-only scheme (the first attempt) loses that. Two deliberate
additions beyond EITT, because they carry a verdict at a glance:

- `Type` keeps its own tint (CON/NON going out vs ACK coming back);
- a `4.xx`/`5.xx` in `Method` stays red, findable inside a green block.

Precedence: inactive → comment → error → type → protocol. A deactivated
telegram must read as OFF first and as COAP second.

### Themes: derive from the palette, never hardcode

The first version hardcoded colours chosen against a white background. On a
dark desktop the "near-black" default text sat at `#1e1e1e` on `#2d2d2d` —
invisible. Worse, **Windows' dark palette reports `AlternateBase` as pure
WHITE**, so `setAlternatingRowColors(True)` flashed every other row white
behind pale text.

`gui/theme.py` detects the theme from `QPalette.Base` and returns a matching
palette; both variants keep the same MEANING per colour and move only
luminance. `test_gui_theme.py` computes real WCAG ratios for every tint
against BOTH the base and the alternating-row background, in BOTH themes —
`inactive` failed at 3.15:1 (dark) and 2.74:1 (light) before being lifted;
everything now clears 4.5:1 AA. Tests must compare against the THEME, never
a literal colour, or they pass only in whichever mode was current.

### Clicking a telegram opens Telegram Properties

Clicking a row in the sequence grid brings the properties pane forward
automatically. The rule goes through one method,
`MainWindow.show_properties_for(item, source=...)`, and
`properties_source` records `"sequence"` or `"trace"`: whichever view was
clicked LAST owns the pane. The two describe the same telegram from
opposite sides (planned vs actual), so keeping the stale one would leave
the operator reading the wrong half without noticing. The trace side lands
in phase 2; the rule is already tested so it cannot diverge.

Selecting a case separator deliberately does NOT touch the pane — it is
view furniture, and blanking a pane being read is worse than doing nothing.

### Layout: both trees on the LEFT, one grid on the right

Settled with the user 2026-07-27 after seeing it run. `CTL (template)` and
`Copies` are tabs in the **left** pane; the right pane holds a **single**
telegram grid plus the tracebuffer.

- **Double-click** in either tree opens that scope in the grid. This is the
  interaction, not selection — single-click drives Telegram Properties only.
- **Double-clicking a category opens everything beneath it**: a collection
  opens all its cases, a suite its own, a copy root the whole project. Cases
  are laid end to end with a bold `CaseBreak` separator naming each
  (`5.2.1.1 …  [1/19]  —  4 steps`).
- **Telegram numbering runs continuously across the opened scope** (1–287
  for all of 5.2), not restarting per case — that is how EITT numbers a
  generated project, and it is what makes the grid comparable to a trace.
- Editability is fixed when the grid model is CONSTRUCTED, so a template
  case cannot be edited as a structural property rather than a checked flag.
  The header states which: `[READ-ONLY] …` (amber) / `[EDIT] …` (green).

### `Activate` is a TELEGRAM attribute — verified 2026-07-27

Corrected after the user pointed out EITT shows the column for telegrams
only. Counted over every `<Sequence>` child in the template:

| Element | carries `Activate` | absent |
|---|---|---|
| `CoAPMessage` | 3109 (all) — 97 are `no` | 0 |
| `DnsMessage` | 41 (all) | 0 |
| `Comment` | **2** | **3407** |

`model.py` defaults a missing `Activate` to yes. That is right for
EXECUTION — a comment without one must still run — but the view was reading
the default back as though the XML had stated it, putting a meaningless
checkbox on all 3407 comments. The grid now populates `Active` (text and
checkbox) only for `CoAPMessage`/`DnsMessage`; `setData` refuses anything
else rather than inventing state the template never carried.

The 2 exceptions are both `@@! Please check LAST UPDATE value` with
`Activate="no"` (5.2.9.1, 5.2.10.1b) — deactivated operator prompts. They
are not editable in the grid but must still RENDER greyed: a switched-off
prompt that looks live is worse than a missing one.

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
