# EITT XML Interpreter — Design and Implementation Notes

Session: July 2026 · Branch: TBD

## Why this exists

The original approach was a hand-written suite (`tests/runtime/`,
176/179 EITT test IDs) built by porting each EITT conformance test from
`tests/eitt/data/KnxConformanceTestTemplate-PointAPI.xml` into Python
by hand. Every time that XML got a new revision (it is already at
Version 40 / 2025-12-04) those tests needed re-porting.

**That suite has now been removed** — this interpreter replaces it, and
is the only runtime conformance suite. Its behaviour is derived from the
XML at collection time, so an XML revision costs nothing to adopt.

`tests/eitt/interpreter/` is a from-scratch interpreter that reads the
XML directly and, via `pytest_generate_tests`
(`test_eitt.py`), generates one real pytest test item per
`TestCase` **at collection time** — nothing is written to disk, nothing
is hand-ported. Swapping to a new XML revision just changes what gets
collected on the next `pytest` run.

## Where things are

The suite is laid out by role:

```text
tests/runtime/
  conftest.py            fixtures: build/start the DUT, CoAP + OSCORE sessions
  test_eitt.py           the suite — one generated test per EITT TestCase
  runtime_test_server.c  the device under test
  eitt/                  this interpreter
  transport/             wire-level clients: CoAP, OSCORE, SPAKE2+, mDNS
  data/                  EITT inputs (template, key maps, variable values,
                         SWU firmware dummy) + traces/ reference runs
  tools/                 run scripts, GUI runner, SPAKE2+ helpers
```

| File | Purpose |
|------|---------|
| `tests/eitt/data/KnxConformanceTestTemplate-PointAPI.xml` | The EITT conformance-test-tool project file (source of truth for test content) |
| `tests/eitt/data/KnxConformanceTestTemplate-PointAPI.py` | The original EITT Python hook script (source of truth for `EvalFn`/`PatchXxxFn` behavior — ported, not executed directly) |
| `tests/eitt/interpreter/model.py` | XML parser → dataclasses mirroring the document hierarchy |
| `tests/eitt/interpreter/variables.py` | `#VAR` substitution + the Fields-scoped shared variable table |
| `tests/eitt/interpreter/content_match.py` | The `<Content>` matching mini-language |
| `tests/eitt/interpreter/hooks.py` | Ported `EvalFn`/`EvalResponseFn`/`PatchXxxFn` callbacks |
| `tests/eitt/interpreter/provisioning.py` | The `"@@[pah..."` comment-command engine |
| `tests/eitt/interpreter/resource_schema.py` | Per-resource-path JSON↔CBOR key mapping (see below — this is the piece EITT itself carries internally that nothing in either repo documents centrally) |
| `tests/eitt/interpreter/spake_flow.py` | Real SPAKE2+ handshake (bypasses the XML's own placeholder text) |
| `tests/eitt/interpreter/overrides.py` | Maintainer exceptions (manual-interaction / not-yet-reliable TestCases) |
| `tests/eitt/interpreter/runner.py` | The sequence player tying all of the above together |
| `tests/eitt/interpreter/overrides.yaml` | The actual override entries (see "Overrides" below) |
| `tests/eitt/test_eitt.py` | `pytest_generate_tests` hook + the single `test_eitt(eitt_case, ...)` test function |
| `tests/eitt/data/traces/5_1.xml` … `5_10.xml` | Real EITT-tool-produced trace logs from actual runs against this v40 XML, one per major section — ground truth for cross-checking any inferred behavior (see "Trace-log validation" below) |
| `tests/eitt/data/2026-02-25_09-36_EittConformanceTestVariableValues.json` | Every `Fields` default value EITT has actually used, across every collection/XML-version it has ever loaded — useful if a `Fields` `DefaultValue` in the XML ever looks stale or wrong |

## Document hierarchy (model.py)

```
EITT_KnxConformanceTests
  Fields (global variable declarations)
  TestCollections
    TestCollection (e.g. "5.1. Discovery")
      Fields (collection-scoped variables)
      TestSuites
        TestSuite (e.g. "5.1.1. GET /.well-known/core")
          TestCases
            TestCase (e.g. "5.1.1.2. Unicast Discovery...")
              Sequence
                Comment | CoAPMessage | DnsMessage   (ordered)
```

193 `TestCase`s, ~3,109 `CoAPMessage`s in the current (v40) XML.
`EittProject.iter_test_cases()` yields `(collection, suite, test_case)`
in document order; `TestCase.qualified_name` is
`"<collection>::<suite>::<case name>"` — this is what pytest test IDs
and `eitt/overrides.yaml` keys are built from.

There is **no `TestCase`-level `Fields` block anywhere in the
document** — variable scope is only global or per-`TestCollection`.

## The variable model (variables.py)

Two things must both be true for `#VARNAME` substitution to work:

1. Global `Fields` + the current `TestCollection`'s own `Fields` seed a
   `VariableTable` with each field's `DefaultValue`.
2. `VariableTable.substitute(text)` replaces every `#VARNAME` token —
   **variable names may start with a digit** (e.g.
   `#5_1_1_3_EXPECTED_IF`), so the regex is `#([A-Za-z0-9_]+)`, not the
   more obvious `#([A-Za-z_][A-Za-z0-9_]*)`. This was a real bug caught
   during development — the naive regex silently failed to resolve
   every collection-scoped, spec-numbered variable.

Hooks read/write the **same** table via `VariableTable.get/set`,
mirroring EITT's own `SetVariableTableEntry`/`GetVariableTableEntry`
callback pair (see `hooks.py`).

## The Content matching mini-language (content_match.py)

An expected-response `<Content>` block is one of:

- `*` — wildcard, accept any body.
- Literal JSON — structural equality after decoding.
- `/path: value` (one or more lines) — check one field of the decoded
  body; `value` may be a literal, `a|b` alternation, or a regex.
- `all: <regex>` — **every** comma-split link-format entry must match.
- `opt: <regex>` — if any entry matches, fine; if none do, also fine
  (manufacturer-optional resources).
- bare `<regex>` (no prefix) — **at least one** entry must match
  (mandatory existence — the default shape for `.well-known/core`
  resource-presence checks).

Link-format bodies are split into entries the same way
`conftest.parse_link_format()` does (comma-separated
`<href>;attr="val";...`). `content_match.match_content()` is the single
entry point; it auto-detects which of the above forms applies.

## Hooks (hooks.py)

Every `EvalFn`/`EvalResponseFn`/`PatchAddressFn`/`PatchContentFn`/
`PatchParameterFn` string actually used by the v40 XML has a ported
Python function here, cross-checked with **zero misses**:
`eval_5_2_4_1_step_1/3/5/7/9`, `set_original_fingerprint`,
`eval_compare_with_original_fingerprint`,
`pointapi_eval_5_1_2_1_get_port/get_address`, `patch_address_5_1_2_1`,
`swu_patch_content(_continue)`, `swu_patch_parameter(_continue)`,
`eval_5_10_1_1_IPv6_adresses`.

**Deliberately not ported** (confirmed dead / out of scope by the same
cross-check): `eval_pointapi_test` (defined in the original script,
never referenced by the XML) and the entire `kip_*` function family
(KNXnet/IP-flavored helpers — zero `kip_*` references anywhere in this
XML; they belong to a different/sibling conformance template that
happens to share the same `.py` file).

`get_hook(name)` raises `KeyError` for any hook name it doesn't know —
a future XML revision introducing a new hook must fail collection
loudly, not silently skip the step.

## EITT reference files (tests/eitt/data/) — consult these FIRST

Copied from a real EITT 4.4 installation (`C:\Users\Public\Documents\
Eitt44\CfgFiles`) 2026-07-24. These are authoritative: they are the
tables and grammar EITT itself uses, so they replace inference from
traces or the C source for anything they cover.

| File | What it settles |
|---|---|
| `EITT_KnxPointAPIResourceKeyMaps.xml` | The JSON↔CBOR key maps, incl. the `Default="yes"` table. Byte-identical to the installation's `CfgFiles/PointAPIResourceKeyMap.xml`. See the resource_schema.py section. |
| `EITT_KnxCommentCommandsScheme.xml` | The **full grammar of every `@`/`@@[` comment command** (27 of them), each with its parameter list and description. Extracted from `CfgFiles/KnxCommentCommandsTemplate.knxcmmt`, which is a ZIP. |

Other useful paths in that installation, not copied here (large, or not
yet needed): `CfgFiles/JsonSchemas/` (the Point API JSON schemas),
`CfgFiles/KnxIOTPointApiTemplate/` (the Interworking / Router / KNXnet-IP
template variants alongside the one we use), `CfgFiles/KnxDPTTemplate.knxdpt`
(datapoint types), `SupportFiles/`.

**What the command scheme corrected or confirmed** (things previously
inferred from usage):

- `pahmcast` takes **`join=<true/false>`** — *"Joins or leaves multicast
  group"* — plus **separate `rxcontext=` and `txcontext=`** parameters in
  addition to the single `context=` we handle. This is the authoritative
  confirmation of the multicast receive model (Known Gap #3).
- `pahctx` takes **`txseq=`** (*"next TX sequence number"*) and
  **`rxseq=`** (*"expected next RX sequence number"*) — explicit
  sequence-number control, the same area reverse-engineered for the
  replay tests — plus `accesstoken=`, `activate=`, `destination=`,
  `updatesession=`.
- **Our template uses three `pahctx` params that `_apply_pahctx` ignores**
  — `destination=` (3×), `updatesession=` (2×), `activate=` (1×) — and
  **ignoring them is correct, not a gap** (resolved 2026-07-25; full
  reasoning in `provisioning.py`'s module docstring). They are EITT's
  device↔context *association* commands (scheme Pos=10..12) and carry no
  crypto material. We resolve a context per step from each telegram's own
  `SecContext=`, so no binding is needed; `updatesession` terminates an
  accumulation block whose `mastersecret=` line has already built a fresh
  context with `ssn = 0` (5.4.1.3's next step is literally commented
  *"read request 1st time (echo chanllange)"*); and 5.4.1.4's `activate=`
  switches between two aliases that are mirror views of one OSCORE pair
  (`01.sender_key == 02.recipient_key`), which is why the recorded run
  `traces/5_4_1.xml` shows no telegram for it and relabels the DUT
  response from the scripted `...02` to `...01`.

  **Do not confuse the `pahctx` parameter `activate=` (1 use) with the XML
  attribute `Activate="yes"/"no"` (3614 uses)** — different namespaces.
  The attribute is the per-step enable/disable flag and is fully
  implemented (`item.activate`, plus the `Activate="no"`
  alternative-response fallback).
- Reassuring: our template only uses commands we already implement, so
  there are no missing *handlers* — only missing *parameters*.

## Provisioning — the `"@@[pah..."` comment DSL (provisioning.py)

Commands found in the v40 XML (with real occurrence counts):

| Command | Count | Purpose |
|---|---|---|
| `pahctx` | 186 | OSCORE security-context building (incremental: `senderid`, `recipientid`, `mastersecret`, `contextid`, `salt`, `remove=true`; the association params `activate`/`updatesession`/`destination` are intentionally ignored — see above) |
| `pahmcast` | 56 | Multicast group provisioning (`installationid`, `groupid`, `scope`, `context`, `join`) |
| `pahsnc` | 21 | Sequence-number-check toggle (`"on"`/`"off"`, bare value, no alias) |
| `MDNS` | 16 | mDNS request-check strictness (`rqcheck=on`/`off`, no alias) |
| `pahdev` | 14 | Device endpoint provisioning (`serialnumber`, `password`, `ipaddress`) |
| `pahdiscover` | 10 | Trigger a discovery cycle (no params) |
| `w` (as `@@[w"..."`) | 2 | Named wait, distinct from the `TimeToNext` attribute |

Plus two related-but-different embedded forms that are **not**
`"@@["`-prefixed and are handled in `runner.py`, not `provisioning.py`:
`@#"TestCaseName"` (131 occurrences — inline test-case reference, used
for the AUTH/IA/TABLE PREPARATION sequences) and `@>w"TestCaseName;
unlimited"` (2 occurrences — the SWU block-upload loop invocation).

### Parsing gotcha: three different argument shapes

- `pahdev`/`pahmcast`/`pahctx`: `"Alias;key=value;key2=value2"`.
- `pahsnc`/`w`: a single bare value, `"on"` / `"#SWU_WAIT_AFTER_DOWNLOAD"`
  — **no** `key=value` shape at all.
- `MDNS`: `"key=value"` with **no leading alias**.

Getting this wrong (treating `pahsnc"on"` as if `"on"` were an alias)
was a real bug caught during development — `parse_comment_command()`
now dispatches on an explicit per-command shape table
(`_BARE_VALUE_COMMANDS`, `_NO_ALIAS_COMMANDS`) rather than guessing from
the string content.

### Parsing gotcha: hex vs. ASCII sender/recipient/context IDs

`senderid=TmpTok` and `senderid=102a3b4c5d6e7f` both appear in the real
XML — the first is a literal ASCII identifier (matches
`conftest.py`'s own `TEST_SENDER_ID = b"RtTest"` convention), the
second is hex-encoded bytes. `_hex_or_ascii()` decodes as hex only when
every character is a hex digit AND the length is even; otherwise it's
treated as literal ASCII. Also a real bug caught during development —
the first version always called `bytes.fromhex()` and crashed on
`"TmpTok"`.

### The SPAKE2+-derived context (no explicit `mastersecret`)

The real AUTH PREPARATION's `'@@[pahctx"Temporary Key;senderid=TmpTok"'`
has **no** `mastersecret=` parameter at all — the master secret comes
from the SPAKE2+ exchange that runs immediately before it.
`ProvisioningEngine.pending_spake_result` (set by
`runner._run_spake_exchange`) is consumed one-shot by the next
`_apply_pahctx` call whose `sender_id` matches, so `ContextBuilder.build()`
can fall back to the SPAKE2+ shared key when no explicit secret was
ever given.

## SPAKE2+ (spake_flow.py) — why it's special-cased

The XML's own `<Content>` for the SPAKE2+ exchange embeds
tool-computed placeholders (`<TOOL.SHARE_P>`, `<TOOL.CONFIRM_P>`) that
only EITT's closed-source PAKE math can fill in — there is nothing in
the XML or the ported hooks that resolves them generically. `spake_flow.
run_spake_handshake()` drives the real 3-step exchange via
`knx_spake2plus.Spake2PlusClient` (the same client `conftest.py`'s
`oscore_ctx` fixture already uses) instead of trying to interpret those
messages generically.

**Non-contiguous exchange, real bug caught during development**: the 3
request/response pairs are **not** 6 contiguous `Sequence` items — the
real AUTH PREPARATION interleaves a `pahctx` comment between pair 2 and
pair 3. `runner._run_spake_exchange()` walks forward collecting exactly
3 request/response pairs while **deferring** any interleaved comments,
runs the real handshake, sets `pending_spake_result`, and only then
replays the deferred comments — so a mid-exchange `pahctx` resolves
against the just-completed handshake instead of running (and failing)
before the shared key exists.

## JSON↔CBOR key mapping (resource_schema.py)

**EITT's own key table is now in the repo** — see "EITT reference files"
above. `tests/eitt/data/EITT_KnxPointAPIResourceKeyMaps.xml` (copied from the
EITT installation at `CfgFiles/PointAPIResourceKeyMap.xml`, byte-
identical) is the authoritative source: EITT resolves a telegram's
human-readable JSON `<Content>` to wire CBOR using exactly these tables.
Consult it FIRST before hand-authoring or inferring any mapping.

`resource_schema.py` mirrors its structure:

- **`_DEFAULT_SCHEMA` is EITT's `Default="yes"` map** (all 27 keys,
  transcribed verbatim). EITT applies it to any path without a
  path-specific map, and `get_schema()` now falls back to it instead of
  raising. This is why most resources need no entry of their own — a
  path-specific `RESOURCE_SCHEMAS` entry is only required where the
  shape genuinely DIFFERS (e.g. `auth/at`'s RFC-8392 token map,
  `.well-known/knx/spake`).
- An unknown **field** name inside a body still raises loudly from
  `ResourceSchema.encode()`. That is the failure we want ("this field
  is in no map"), as opposed to the old, noisier one ("this path isn't
  listed"), which produced a long tail of one-off entries.

**Key numbering is resource-scoped, not global** — key `1` means
`value` in the default map but `version`/`iss` inside an OSCORE `cnf.osc`
or access-token map; key `2` is `cmd` by default but `ms`/`kid`/`sub` in
`auth/at.*`. EITT's file makes this explicit by shipping separate maps
per path, and even reuses keys *within* a map for mutually exclusive
JSON names (in `PAPI_RKM_OSCORE`: `ms`/`kid`/`sub` all = 2, `hkdf`/`aud`
= 3, `osc`/`alg` = 4, `nbf`/`salt` = 5). Encoding (JSON name → key) is
therefore unambiguous; decoding back is not, so prefer the name the XML
actually used rather than trusting a reverse lookup.

Historical note: every key originally derived here by hand — from
`knx-iot-docs`, the traces, and `cbor2.dumps(...)` calls in
`tests/runtime/`/`conftest.py` — was later confirmed to match
EITT's file exactly (27/27 default, 15/15 spake, zero mismatches). The
inference method was sound, but it was slow and left gaps for keys we
had not yet had reason to hit (`secFlags`, `pkgq`, `fault`, `g`,
`groupKey`, `overridden`, `dpt`, `knxClassicObj`, `a`, `c`, `seqCntr`,
and the spake response-side keys) — all now present.

### Real bug caught during development: the `"h'<hex>'"` literal convention

The XML represents CBOR byte strings as JSON string literals,
`"id":"h'74657374636667'"` — this is an EITT XML-authoring convention,
**not** a real wire format. The very first authenticated end-to-end run
(`5.2.3.1`) failed with the device unable to find the AT by `kid`, even
though AUTH PREPARATION itself reported success. Root cause: the AT's
`cnf.osc.id`/`ms` were being CBOR-encoded as the **literal ASCII text**
`"h'74657374636667'"` instead of the actual bytes it denotes, so the
device derived completely different OSCORE keys than the
`TokenTest` `OscoreContext` the interpreter itself was using for
"the same" nominal secret. `expand_hex_literals()` now recursively
expands every `"h'<hex>'"` string to real bytes inside
`ResourceSchema.encode()`. This is the kind of gap that produces a
**quiet, wrong-looking** authentication failure rather than a crash —
worth remembering if a future resource schema addition passes CBOR
encoding but still gets 4.01 from a live device.

### `/auth/at` has no flat schema of its own

**CONFIRMED against real EITT trace logs** (see "Trace-log validation"
below): the wire envelope is **always a bare CBOR array of raw entry
maps** — `81 BF 08...FF` (array of 1) for a single-entry write,
`82 BF 08...FF BF 08...FF` (array of 2) for a multi-entry write —
**never** a `{0: entry}` map wrapper, regardless of entry count. This
directly contradicts `conftest.py`'s own `oscore_ctx`/`auth_prepare`
fixtures, which send `{0: at_inner}` for a single entry — the device
apparently accepts both shapes (the hand-written suite's tests pass),
but the real EITT-recorded wire format is the array form, so that is
what `runner._resolve_body()` reproduces (fixed from an earlier,
incorrect `{0: entry}`-for-single-entry assumption).

## Trace-log validation

The user has access to the real EITT tool and provided 9 real,
EITT-produced trace logs from actual runs against this v40 XML —
`tests/eitt/data/traces/5_1.xml` through `5_10.xml` (one per
major section), each an XML `<TraceBuffer>` of every `<Comment>`/
`<CoAPMessage>`/`<DnsMessage>` EITT actually sent/received, with real
resolved attribute values (`RawContent` = actual wire hex bytes,
`EvalResult` = what EITT itself concluded, real `SeqNum`/`Mid`/`Token`)
where the template XML only ever has placeholders. Also provided:
`tests/eitt/data/2026-02-25_09-36_EittConformanceTestVariableValues.json`
— a dump of every `Fields` default value EITT actually used, across
every collection/version EITT has ever loaded (useful as a
cross-reference if a `Fields` `DefaultValue` in the XML ever looks
wrong or gets out of sync with what a real run used).

This resolved every previously-"inferred, not confirmed" item that a
trace could settle:

- **`/auth/at` envelope** — confirmed always a bare array (see above);
  fixed a real, previously-wrong assumption in `runner.py`.
- **`SeqNumMethod`** (numeric in traces: `0`/`1`/`2`, vs. the template's
  string labels `"not set"`/`"automatic"`/`"asinrequest"`/`"fix"`) —
  `0` = unsecured tool-generated request (SPAKE2+, discovery), `1` =
  secured (OSCORE `SecContext` present) tool-generated request, `2` =
  any DUT response. A few boundary exceptions exist right at the
  unsecured→secured handoff (bootstrapping OSCORE via a multicast
  discovery step), consistent with SNM tracking message *role* rather
  than strictly `CWay`.
- **`Echo`** (numeric in traces: `1`/`2`) — `1` = the DUT/peer issuing a
  4.01 Echo challenge in an `OUT` response, `2` = the tool echoing the
  challenge value back in a follow-up `IN` request. This is the RFC 8613
  Echo-challenge-and-retry flow, and `CoapClient.oscore_request()`
  implements it transparently (`[oscore] Echo challenge received,
  retrying` fires during `AUTH PREPARATION`/`5.2.3.1`).

  **But transparent retry is wrong whenever the script models the
  challenge as its own step.** 5.3.17.5 sends a group write, expects the
  4.01 as an explicit `OUT`, then replays the telegram with a
  deliberately WRONG `Echo="fix"`/`EchoValue="FF"` and asserts the device
  rejects it (the datapoint must stay `false`). With auto-retry active
  the client silently completed the handshake correctly, the write
  landed, and the test "passed" its way to a wrong value — a false
  negative that looked like a device bug. `runner.py` now looks ahead one
  step and passes `expect_echo_challenge=True` when the next active `OUT`
  has code `4.01`, which puts `oscore_request` in the same
  one-shot/no-retry mode the adversarial overrides already use.
- **`RspSchema`** — confirmed **zero** occurrences of the attribute
  itself across all 9 trace files (not just always-empty in the
  template) — genuinely vestigial/unused by EITT at runtime.
- **Non-`OK` `EvalResult` values** — real traces show what an actual
  EITT-recorded mismatch looks like, e.g.
  `"Message code differs, planned: 4.01, received: 4.03"` and
  `"Cannot find JSON pointer: /2/value"` — useful reference for wording
  our own `content_match.MatchResult.reason` strings similarly.

The adversarial attributes used by 5.3.17.x/5.3.19.x
(`SeqNoOffset`, `SeqNoFn="fix"`, `InvalidAuthTag`, `RndKid`,
`SeqNoFn="asinrequest"`) **were** in the traced runs after all, in the
blocks the section index below maps out — the traces resolve every one
of them, and all are now implemented. Note the trace's own
`CoAPSecurity` child element is where `KeyID`/`PartialIV`/`KidContext`
live; the top-level attributes alone don't show them.

Still open: `CheckRxKid` (appears nowhere in a traced run that exercises
it distinguishably). `DnsMessage` playback is implemented — see "mDNS
discovery (DnsMessage)".

### How EITT generates a runnable test (and where variable values come from)

General EITT knowledge, and it changes how the template should be read.
There are three layers, not one:

1. **The conformance test template** —
   `tests/eitt/data/KnxConformanceTestTemplate-PointAPI.xml`. Ships with EITT
   and is **READ-ONLY**. Its `#VAR` tokens and `Fields`/`DefaultValue`
   entries are the *slots*, and its defaults are deliberately
   conservative — they are not necessarily what any real run uses.
2. **The variable-values JSON** —
   `tests/eitt/data/2026-02-25_09-36_EittConformanceTestVariableValues.json`.
   Layered itself: a `GLOBAL` block, then the matching test-suite block
   (keyed by GUID + `Name` + `Version` — ours is **V40** of
   "8/10/5 - KNX IoT Point API Tests", which is exactly the
   `PlanSource="Copy of 5.3. Security V40"` our traces were recorded
   against), then that suite's per-`Collections` blocks
   (`"5.1. Discovery"`, `"5.3. Security"`, …). Later layers win. Most
   string values are stored as space-separated hex of the ASCII
   (`"70 2F 31"` = `p/1`); many entries are intentionally EMPTY, awaiting
   the operator.
3. **The generated copy** — selecting a subset of the tree produces a
   working copy with those values resolved in. That copy is what runs,
   and an edited parameter can be pushed into an already-generated copy.

We mirror this with `VariableTable.for_collection(project, name,
overrides=...)`: layer 1 is the template's Fields defaults, and layers
2/3 are the `variables:` section of `tests/eitt/interpreter/overrides.yaml`.

**Diffing the JSON against the template defaults is a real bug-finder.**
All 112 variables appear on both sides; 10 disagree, and the split is
instructive:

- 4 are pure case/format noise (uppercase hex).
- 4 are the operator's local fill, where OUR template is the more correct
  one: the JSON still holds unfilled placeholders `{Datasheet Hostname}`
  and `C:/path/to/file.bin`, and `Windows` where ours says
  `Windows|Linux` for the Linux CI device.
- **2 are substantive.** `GENERAL_TIME_TO_NEXT` is `1000` in the template
  but `500` in the JSON — a leftover from the V31 refactor whose
  changelog reads *"Fix 1s wait times are replaced with
  'GENERAL_TIME_TO_NEXT'"*. It is referenced by **1489** messages, so
  taking the template default at face value made every run sleep twice as
  long as a real EITT run (~37s per full-template pass). Now overridden.
  `PREP_UPDATE_FILE_PATH` is likewise the unfilled Windows placeholder;
  it points at the checked-in `tests/runtime/FWUUploadDummySmall.bin`
  (5120 bytes) that the SWU tests upload.

### Telegram-attribute audit (what we honour, what we don't)

Walking EITT's own Telegram Properties panel field by field caught a
silent behavioural gap (`Wait`) that had been costing correctness as well
as time, so the full audit is recorded here. Counts are active messages
across the whole template.

| Panel field | XML attr | Status |
|---|---|---|
| Message Type | `Type` | honoured — CON:1322 / NON:365 per telegram (we used to hardcode CON) |
| Method / Endpoint / Parameter | `Code`/`Path`/`URLParameter` | honoured |
| Destination | `IP` | honoured (see below — it is the PEER, not a direction) |
| Security Context | `SecContext` | honoured |
| Content Format | `CF` | honoured — sent as the request's Content-Format option (12); see the SWU note below for what omitting it cost |
| Content / Content (CBOR) | `<Content>`/`<Cbor>` | honoured |
| Accepted Format | `AcceptedCF` | honoured — becomes the CoAP Accept option (17) |
| Secured Message | `Secure` | equivalent-by-construction: verified it NEVER contradicts `SecContext`, so "secure iff a context resolves" matches EITT |
| Sequence Number / Offset | `SeqNoFn`/`SeqNo`/`SeqNoOffset` | honoured |
| Echo Option | `Echo`/`EchoValue` | honoured |
| Invalid Auth Tag | `InvalidAuthTag` | honoured |
| Critical Option Number/Value | `CritOptNo`/`CritOptValue` | honoured — one use (5.10.5.3: unknown critical option 65123 must draw 4.02) |
| Wait for Response Timeout | `AckChkTimeOut` | honoured where it applies (group-listener timeout); 9 uses, 10/15/20s |
| Use Random Kid Context | `RndKid` | honoured |
| Use Last RX Token | `UseLastRxToken` | honoured — the tool answers a request the DUT sent IT, quoting that request's token (see below) |
| Response Schema | `RspSchema` | vestigial: 0 occurrences anywhere |
| Depends on Script Function | `EvalFn`/`Eval*`/`Patch*Fn` | honoured — all 14 functions the template names are implemented in `hooks.py` |
| Observe Option | `Observe`/`ObserveValue` | not honoured; 6 uses, all in 5.10, and Observe is new/unstable device-side |
| Activation | `Activate` | honoured |
| Transmit Mode | `CWay` | honoured |
| Time to next telegram | `TimeToNext` | honoured, gated by `Wait` |
| Wait end time | `Wait` | honoured (see `_apply_wait`) |
| Cycles | `Cycles` | not honoured; 1 use (`*`) |
| Comment Command | `Text` | honoured — the `@@[pah...]` DSL, plus the `@[w`/`@@[w` waits described below |

### The two wait commands (`@[w` vs `@@[w`) are NOT the same

Both appear in `Comment` text and both name a timespan, but they mean
different things, and neither was implemented until section 5.2 was
finished (25 occurrences across the template — this was not SWU-only):

- **`@[w"<time>"`** — toolbar countdown. A genuine unattended delay:
  EITT sits out the whole timespan. We sleep it, scaled by
  `RUNTIME_TEST_TIME_SCALE` like every other wait.
- **`@@[w"<time>"`** — a MODAL DIALOG the operator dismisses. The
  declared time is only the ceiling before it self-closes; in practice
  the human clicks OK after a couple of seconds. Sleeping the declared
  value would be wrong (5.2.9.1 declares `00:02:00` twice), so this is
  treated as a manual-interaction step: pause a short fixed moment
  (`_MODAL_WAIT_SECONDS`) for the device-side work the click is really
  waiting on — `runtime_test_server` simulates its firmware upgrade with
  a 2s delayed callback.

The `@@[w` form previously fell into the generic `@@[` provisioning
branch and was silently dropped, so 5.2.9.1 read `/swu/state`
immediately after triggering the update and saw UPGRADING(3) where the
script expects IDLE(0).

### Sending the request's own Content-Format matters (the SWU 4.00 mystery)

`AcceptedCF` became the Accept option, but the request's own `CF` was
never sent as Content-Format (option 12) — so every payload looked like
CBOR to the device. `oc_ri.c:1083` only CBOR-parses when the format IS
CBOR/OSCORE, so with the option missing the device tried to parse raw
firmware bytes at `/a/swu` and answered **4.00** to any block that did
not happen to be valid CBOR.

The failure mode was wonderfully misleading: whether a block was accepted
depended on whether its bytes coincidentally parsed as CBOR. For
`FWUUploadDummySmall.bin` the first three blocks start `00 01 02…`,
`f4 f5 f6…`, `e8 e9 ea…` (all valid CBOR) and landed; `dc dd de…`
onwards did not, so the upload stalled at 3000/5120 bytes and the device
correctly stayed in DOWNLOADING. Only 2 messages in the whole template
use `application/octet-stream`, and both are the SWU upload blocks.

### The tool as CoAP server (`UseLastRxToken`)

Before the DUT can send a *unicast* s-mode telegram it has to learn the
individual address its recipient table names, so it multicasts
`GET /.well-known/core` and waits for someone to claim that IA. EITT
models this as two rows — `OUT GET /.well-known/core` (the DUT asking)
then `IN 2.05 ... UseLastRxToken="yes"` (our reply, a link-format
`<>;ep="knx://sn.… knx://ia.…"` body quoting the DUT's own CoAP token) —
and that is why 5.4.1.4 / 5.4.1.15 join `Multicast Link-Local`,
`Realm-Local` and `Site-Local` first: those joins are how the tool
receives the query at all.

Implemented as `CoapClient.arm_plain_listener()` / `PlainListener` (the
unencrypted twin of the group listener) plus
`CoapClient.send_plain_response()`, driven by `_expect_dut_request` and
`_answer_last_rx_request`. Two traps, both of which bit during
development:

- **The routing discriminator must be the `UseLastRxToken` PAIRING, not
  "this OUT's Code is a method".** Section 5.3 has 103 `OUT POST` rows
  (5.3.19.5's 100 group responses plus 5.3.17.6's 3) that are ordinary
  group telegrams; routing on the code alone hijacked all of them and
  broke 8 tests.
- **Nothing in the script provokes the query** — 5.4.1.15's preceding
  step is the comment "Trigger device manually" — so the step self-arms
  its listener and then triggers, exactly like the unsolicited group
  telegram in `_expect_group_telegram`. There is no earlier request to
  hang the arming off.

Also note `parse_coap_message` keeps only the LAST Uri-Path segment
(`GET /.well-known/core` parses back as `{11: b'core'}`), so the
listener's path filter matches on that final segment.

### EITT's runtime state model (its Destinations / Multicast Groups / Security Contexts dialogs)

Three GUI dialogs expose the state the `@@[pah...]` commands accumulate,
and they line up with our `provisioning.py` dataclasses:

- **Destinations** ≈ `Device`: name, IP address, base path, serial
  number, password, temporary master secret. Values are filled in at
  runtime (BDUT's address comes from CoAP/mDNS discovery, not config).
  There is also a "use as white list" toggle that would make EITT discard
  POSTs from endpoints not in the list — we have no equivalent and don't
  need one.
- **Multicast Groups** ≈ `MulticastGroup`: name, installation id, group
  id, scope, joined, and separate receive/send security contexts. The
  receive column can show SEVERAL aliases (`MulticastRx;MulticastSaltTest_...`)
  because EITT accumulates every `context=` ever set on that group; each
  TestCase sets `context=` before it joins, so a single current value is
  the right model per test (verified for all four 5.3 users, which also
  all leave with `join=false`).
- **Security Contexts** ≈ `OscoreContext`: sender/recipient/context id,
  master secret, salt, AEAD + HKDF algorithm — and **two** sequence
  counters, `SeqNr` and `SeqNr (RX)`. We track only the TX one. Per
  EITT's own grammar these are `pahctx`'s `txseq=` ("next TX sequence
  number") and `rxseq=` ("EXPECTED next RX sequence number"), i.e. the RX
  side is an assertion about the device's numbering, not just bookkeeping
  for decryption. In the observed state they move in lockstep, which is
  why a single counter has sufficed so far.

### `IP` / "Destination" is the PEER, not a direction

EITT's `IP` attribute is present on both directions (`OUT`:1528,
`IN`:1581 in the template) and names the party the telegram is exchanged
*with*, not where it is going. The GUI relabels the same field per
`Transmit Mode`: it reads **Destination** on an `in` row (tool→DUT, so
the DUT is the destination) and **Source** on an `out` row, and the
`Source Address` / `Group Address` / `Service Type` rows grey out on
whichever direction they don't apply to.

Mostly it just mirrors the preceding request (1393 OUT rows match), but
135 OUT rows name a DIFFERENT peer than the request they answer — the
group cases, where the tool sends to `BDUT-Multi` and the DUT answers
from `BDUT` or vice versa. We correlate responses by CoAP token rather
than by address, so this is informational for us today; it would matter
if the runner ever needed to distinguish two peers answering one request.

### Reading a trace: `DestinationAddress` is EITT's own view, not the wire

`DestinationAddress="BDUT"` on an `OUT` message does **not** mean the DUT
answered by unicast. Every DUT-originated message in `5_3.xml` carries
`BDUT`, including 5.3.19.5's 100 group responses and 5.3.17.6's sensor
telegram, which the device log proves went to the multicast group
address. EITT is recording "this came from the DUT", not the destination
of the datagram.

The fields that *do* show the wire address are `LocaIP` and
`LocaMCast="Y"` — e.g. TelNo 761 (5.3.17.6's sensor telegram) has
`LocaIP="[ff32:30:fd11:9988:7766:0:8000:1]:5683"`, `LocaMCast="Y"`.
Reading only `DestinationAddress` cost a full wrong implementation round
in this work (a unicast-response model that could never have received
the telegrams); when the direction of a group message matters, check
`LocaIP`/`LocaMCast`, and confirm against the device's own log
(`-ServerLogs`, look for `oc_knx_client.c` "Sending s-mode multicast").

## Manual-interaction steps & per-test exceptions (overrides.py)

Some EITT steps assume a human pushes a button or restarts the DUT —
the XML's only hint is things like `Cycles="*"` plus a narrative
"trigger manually" comment. `tests/eitt/interpreter/overrides.yaml`, keyed
by `TestCase.qualified_name`, supports three actions:

- `trigger` — call a DUT test-only control endpoint instead of waiting
  for a human (mirrors `test_group_comm.py`'s `_trigger_sensor`/
  `_roi_restart`, which already call `POST /test/trigger` and
  `POST /test/restart`).
- `skip` — excluded from collection with a visible reason.
- `xfail` — runs, but a failure doesn't fail the suite (mirrors the
  known device-side gaps).

`OverrideTable.load()` raises if PyYAML isn't installed, and every key
should correspond to a real `TestCase.qualified_name` — a typo here is
a silent no-op override, so cross-check new entries against
`EittProject.iter_test_cases()` before trusting them (there is no
automatic collection-time warning for this yet — see "Known gaps"
below).

## The sequence runner (runner.py)

`SequenceRunner.run(test_case)` walks `test_case.sequence` and, for
each item:

- **Comment**: dispatched to `provisioning.apply()` for `"@@["` text,
  or handled inline for `@#"..."` (inline TestCase reference —
  recurses via `PreparationRegistry`) and `@>w"...;unlimited"` (loop
  invocation — see below). Plain `@[t...` narrative text is a no-op.
- **CoAPMessage** (`CWay="IN"`): resolves `#VAR`s in path/query/body,
  applies `PatchParameterFn`/`PatchContentFn` if present, sends via
  `coap_client.CoapClient` (OSCORE-protected if `SecContext` resolves
  to a context, plain otherwise), runs `EvalFn` if present.
- **CoAPMessage** (`CWay="OUT"`): decodes the previous request's
  response using the previous request's *path* (an `OUT` message has
  no `Path` of its own — this was a real bug: the first version passed
  an empty path hint to the schema lookup, so every `/path: value`
  Content check silently failed to decode), then either runs
  `EvalResponseFn` or `content_match.match_content()`.
- **DnsMessage**: implemented — see "mDNS discovery (DnsMessage)" below.

### mDNS discovery (DnsMessage)

EITT models multicast-DNS discovery as `DnsMessage` items rather than
CoAP telegrams: `CWay="IN"` with `Type="dnssd"`/`"mdns"` is a query the
tool sends, `CWay="OUT"` with `Type="dnsrecord"` is a record the DUT must
answer with. Played by `runner._run_dns_message` over
`transport/mdns_client.py`.

**One query, one whole answer.** The DUT replies to a single query with
its ENTIRE record set, so the runner caches that answer and lets the
following `OUT` steps match out of it — it is a rolling record cache, not
a listener per query/answer pair. `data/traces/5_1.xml` TelNo 121→122
shows it directly: one PTR query for `_knx._udp.local` draws PTR + SRV +
AAAA + every subtype PTR, and the scripted OUT step then matches
`_00fa10020800._sub._knx._udp.local` out of that. In the trace, rows with
`TelNo=None` are simply captured traffic; only the records EITT was
waiting for get a `TelNo` and `EvalResult="OK"`.

**`@@[MDNS"rqcheck=off"` is load-bearing.** EITT's own grammar spells the
command `@@[pahmdns` and defines it as *"Check if the answered service is
equal to the requested service"*. With the check on, an answer only counts
when its owner name equals the queried name; 5.1.2.x turns it off exactly
because it queries one service and matches records for another out of the
same answer. Both spellings are accepted.

**Join the WILDCARD interface, not just the named one.** This cost hours:
joining `ff02::fb` on a single interface index works until the DUT
re-opens its own mDNS socket (a factory reset makes it do that), after
which its answers stop arriving on that membership — while the device log
still cheerfully logs `send_query_response`. Joining index 0 as well took
a listener from 0 records to 100. The symptom looks exactly like a dead
responder, so do not conclude "the device stopped answering" without
checking with a wildcard join first.

Queries are also re-sent while the listener is open: mDNS responders delay
their replies (RFC 6762 §6), and one that is mid-restart drops anything
that arrives in the gap.

**Unsolicited announcements (5.1.2.5a/b) — two separate traps, both fixed
2026-07-25; this is what took 5.1 from 39/42 to 42/42.** These TestCases
have a `DnsMessage` OUT with no query of its own: the DUT re-announces
because a `PUT dev/pm` or `POST .well-known/knx/ia` changed its state, so
`_arm_unsolicited_dns` must bind a listener BEFORE the request goes out
(the device announces from inside the PUT handler, `oc_knx_dev.c:558` →
`knx_dns_sd_update_service`).

1. **Look past the request's own paired response when scanning.** The
   shape is `PUT dev/pm` → `OUT 2.04` → `DNS OUT PTR`. The scan used to
   stop at the first non-Comment item, always found the 2.04, and so
   never armed anything — the step then fell through to a late listen
   that caught only the tail of the announcement (in practice the lone
   AAAA record), producing the misleading "expected PTR but received
   AAAA". Skip Comments AND non-request CoAPMessages; stop at the next
   real request.
2. **A state change emits TWO packets: goodbye then announce.** The
   device goodbyes the OLD advertisement (TTL=0) and only then announces
   the new one. A listener that breaks on the first packet — correct for
   query/answer, where one answer is the whole record set — captures the
   goodbye and misses the announcement that actually carries the new
   state. That is precisely what 5.1.2.5a Step 2 asserts on: enabling pm
   goodbyes with `pm=0` (no `_pm._sub` PTR) and announces with `pm=1`
   (PTR present). Hence `MdnsListener(await_live=True)`: keep reading
   while everything seen so far has `ttl == 0`. 5.1.2.5b is the same
   story for the IA change — goodbye for `_ia1199887766-1101`, announce
   for `_ia2dfdc1c3d-4d2`.

   (5.1.2.5b's expected name is hardcoded in the template, NOT a
   variable, and it is correct: the step first writes
   `{"ia":1234,"iid":12345678909}`, and `0x4d2`/`0x2dfdc1c3d` are exactly
   those values in hex. Don't "fix" it as a substitution bug.)

Joining every interface was also tried while chasing trap #2 and made no
difference — the wildcard membership already receives these. The narrow
`[0, DEVICE_IFACE]` join was kept.

**Discovery is proved end-to-end, not just parsed.** 5.1.2.x invalidates
the cached device address (`@@[pahdev"BDUT;ipaddress=invalid"`), resolves
SRV then AAAA — whose `EvalFn`s stash `mdns_port` and `mdns_ip` — and the
closing `GET /.well-known/core` carries
`PatchAddressFn="patch_address_5_1_2_1"`, which returns `[ip]:port` and
retargets that one request. If discovery had not really worked, the GET
would have nowhere to go.

### CoAP multicast discovery (`IP="Multicast <scope>"`)

An UNSECURED request whose `IP=` names one of EITT's well-known scope
entries — "Multicast Link-Local"/"Realm-Local"/"Site-Local" →
`ff02`/`ff03`/`ff05::fd` — is a REAL multicast, and
`_send_request_inner` routes it through `CoapClient.multicast_get()`
(see `_plain_multicast_scope`). Previously every unsecured request went
through `self.coap.get()`, i.e. unicast to the already-known
`DEVICE_HOST`, which is what made 5.1.1.1 "Multicast Discovery via
/.well-known/core" fail.

Worth stating plainly, because it is the whole point of the test: the
device answers a unicast `GET /.well-known/core` too, so unicasting that
step would have *passed* while never exercising the multicast path the
TestCase is named after. It happened to fail instead, which is luckier
than it sounds.

The device joins the CoAP multicast groups on the STANDARD port 5683
(`ipadapter.c` "Adding all CoAP nodes") even when its unicast port is
ephemeral, so scope-address + default port is what reaches it; the reply
comes back from the unicast port, carried in `source_addr`. This mirrors
`conftest.discover_dut()`, which is how a real (`EXTERNAL_DUT`) run finds
the device in the first place — the template always starts with a CoAP
discovery, and the runner now does the same even when the port is already
known from the harness, both to exercise the real path and as a sanity
check.

### `@@!` operator prompts (manual physical actions) — IMPLEMENTED

`@@!<text>` is EITT's *"display comment in toolbar, **suspend evaluation** and
beep"* command (`EITT_KnxCommentCommandsScheme.xml`) — a modal the operator
dismisses after doing something PHYSICAL. The single-`@` form `@!<text>` is
*"display comment in toolbar and beep"*, informational only, and stays a no-op.

We used to ignore `@@!` entirely, which was not harmless: **5.3.1.3** tests
SPAKE2+ across four reset types, escalating from software to physical —
`{"cmd":"restart"}`, `reset value 7`, `reset value 2` (all CoAP), then a **manual
power cycle** and a **Local Master Reset** (both `@@!`). With the prompts skipped,
the SPAKE2+ step after the "power cycle" ran against a device still in the
previous state, so the two hardest reset types the TestCase exists for were never
exercised — and it passed anyway.

Mapped by TEXT, via an ordered `manual_actions:` list in `overrides.yaml` (first
match wins), because `@@!` alone only means "a human does something":

| prompt text | endpoint | why |
|---|---|---|
| `power[- ]?cycle` | `/test/restart` | `oc_knx_device_restart()` re-inits contexts **from storage**, so configuration survives — which is what the template then asserts: SPAKE2+ must be **refused 4.00**, *"device is not in KNX Default State"* |
| `local master reset` | `/test/factory-reset` | wipes to factory state — after which the template expects SPAKE2+ to **succeed**, *"device is in KNX Default State"* |
| `check LAST UPDATE value` | *(none)* | 5.2.9.1 / 5.2.10.1b: a human reads the value off the GUI. In BOTH cases the prompt is the LAST meaningful step (only a factory reset follows), so nothing later re-checks it — a small genuine coverage gap, deliberately not papered over with an invented `/swu/lastupdate` assertion |

Two things that matter:

- **An unmapped `@@!` FAILS the step**, loudly, rather than passing silently. These
  steps gate on a physical action; continuing quietly is how a test "passes"
  without testing anything.
- **The factory-reset path must clear our materialized OSCORE contexts.** Without
  it every later TestCase keeps using a dead `TokenTest` and the collection
  collapses — measured 44 passed → **11**. Same invalidation the CoAP
  `{"cmd":"reset","value":2}` path already does; the template re-provisions right
  after (5.3.1.3 item 46 is `@#"Copy of AUTH PREPARATION"`).

On real certification hardware the `/test/...` endpoints may not exist, so a GUI
front-end needs to offer both "fire the endpoint" and "I did it manually" — see
`eitt-gui-plan.md`.

### DUT-originated UNICAST telegrams (5.4.1.4 / 5.4.1.15) — IMPLEMENTED

Some 5.4 steps expect an s-mode telegram the DUT sends by **unicast**
rather than to a group address. `traces/5_4_1.xml` shows the full chain
for 5.4.1.15 as TelNo **321 → 322 → 323**: the DUT cannot know our
address, so it multicasts
`GET /.well-known/core?ep=knx://ia.<iid>.<ia>`, we answer with our own
`ep` (the `OUT <method>` + `IN … UseLastRxToken` pair), and only then
does it send `CON POST /k` to us directly. The whole chain is kicked off
by the manual-trigger substitute, `POST /test/trigger`.

**There is nothing to "arm" for this, and EITT has no listen command.**
`EITT_KnxCommentCommandsScheme.xml` contains no listen/capture/receive
verb at all (checked: zero matches across every `Command="@@[…"`), and
neither 5.4.1.4 nor 5.4.1.15 contains anything that could start one.
EITT's CoAP endpoint is simply always up and an `OUT` step means "wait
for a matching telegram on it"; `pahmcast"…;join=true"` is about GROUP
MEMBERSHIP, not about starting a listener. Our client works the same way
— one socket, `bind(("", 0))` in `CoapClient.__init__`, alive for the
session — so `recv_unicast_telegram()` is just a timed read of that
already-open socket (plus the CON ACK and the decrypt).

Three things had to be true before either TestCase passed. Each was found
by comparing the trace against a real run, and each is easy to
reintroduce:

1. **The DUT must be in its own network namespace.** It learns our
   address from the SOURCE of our discovery reply
   (`knx_coap_discovery_response` stores
   `data->endpoint->addr.ipv6.address`). With both veth ends in ONE
   namespace every address is local, so the kernel answers "source for a
   packet to `::1`?" with `::1` itself, and the device records its OWN
   address — `CoAP discovery response: IPv6 fd11:9988:7766::1 (if=4)`,
   paired with our correct port — then sends the telegram to itself.
   `run-in-docker.ps1` now runs the DUT under `ip netns exec dut` (via
   `RUNTIME_TEST_SERVER_PREFIX`, honoured by `conftest.py`). Verified:
   `ip -6 route get` resolves via `veth-test`/`veth-dut` with src
   `::2`/`::1` instead of `dev lo` for both. **Distinct prefixes or
   `/128` + explicit host routes do NOT work — don't retry that.**
2. **No Uri-Path filter before decryption.** An OSCORE-protected request
   carries its path INSIDE the ciphertext; the outer options hold only
   the OSCORE option (9). Filtering on `options[11]` therefore rejected
   every protected telegram — 3 datagrams arrived, all decryptable, all
   dropped. The path is now checked after decryption for protected
   messages, and before it only for unprotected ones.
3. **Unsolicited requests must be BUFFERED, not discarded as stale.**
   This is what 5.4.1.4 needed, and the trace is what gave it away: its
   telegram (TelNo 101, mid 25636, token `27 99 1A 0D`) has a completely
   different mid and token from the ACK just before it (TelNo 98, mid
   9456, token `39 33 30 34`) and lands **0.1s** later — 22:25:20.6 →
   22:25:20.7. So it arrives while `_recv_oscore_response` is still
   reading, where the "skip messages with wrong token" branch threw it
   away. That branch now keeps any unsolicited REQUEST in
   `CoapClient._pending_requests`, and `UnicastTelegramListener` drains
   that buffer before touching the socket. This is precisely the property
   EITT gets for free from a permanently-open socket, and it is why its
   item 54 can use a plain `TimeToNext="00:00:02.0"` budget.

Note 5.4.1.4 has NO discovery pair to arm on — its items 52/53 are
`Activate="no"`, because the device already learned our address in an
earlier TestCase — so buffering is the only thing that can catch it.
And do NOT try arming the read ahead of every request in
`_prepare_for_request`: the armed read owns the shared client socket and
swallows ordinary request/response traffic, taking 5.4.1 from 14 passed
down to 12 (5.4.1.11 and 5.4.1.15 regress).

### The SWU block-upload loop (`"@>w"..."`)

`Cycles`/`"@>w"TestCaseName;unlimited"` is EITT's "repeat this named
TestCase N times, where N is computed at runtime" construct. The
runner's `_run_loop_invocation()` sets
`sequence_loop_count.<TestCaseName>` before each iteration and reads
`sequence_loop_limit.<TestCaseName>` after — exactly the dotted-key
convention the ported `swu_patch_content`/`swu_patch_content_continue`
hooks read/write (see `hooks.py`).

### Not implemented (raises, doesn't guess)

Where the interpreter cannot faithfully play a step it raises
`UnsupportedFeature` rather than sending an approximation — a silent
"close enough" would show up as a device bug. The live list is in
"Known gaps" below; `UseLastRxToken` was the last entry here and is now
implemented.

### SPAKE2+ handshake fixes (2026-07-24 session)

Validating 5.3 surfaced a cluster of real, generalizable bugs in the
SPAKE2+ special-case handler (`spake_flow.py`/`runner.py`), all found
by tracing real device server logs (`-ServerLogs` switch added to
`run-in-docker.ps1` for exactly this) rather than guessing from
client-side symptoms alone:

1. **Hardcoded password**: `_run_spake_exchange` always authenticated
   with the real `BDUT_DEVICE_PASSWORD`, ignoring a
   `@@[pahdev"<alias>;password=#WRONG_PASSWORD"` override — so
   "SPAKE2+ fails with incorrect password" TestCases were silently
   authenticating for real instead of testing failure. Fixed by
   resolving the password via the request's `IP=` device alias
   through `ProvisioningEngine.resolve_device()` first.
2. **Always-3-steps**: the handshake helper always drove the full
   3-step exchange regardless of how many steps the XML actually
   activates for a given TestCase. Fixed via a `max_steps` parameter
   derived from the actual activated-request count.
3. **ConfirmP override**: "sending an incorrect confirmP fails"
   TestCases write a literal wrong hex value in step 3's `<Content>`
   instead of the `<TOOL.CONFIRM_P>` placeholder; the handler always
   computed the mathematically-correct value instead. Fixed by
   detecting the placeholder's absence and threading the literal value
   through as `override_confirm_p`.
4. **Non-2.xx step responses**: any step response coming back
   non-2.xx was always raised as a hard failure, so TestCases that
   deliberately provoke e.g. a `4.00` at step 1 (device not in default
   config state) could never pass. Generalized: every step now returns
   a `SpakeStepFailure(step, response)` on non-2.xx instead of raising,
   which the runner matches against the XML's own expected-response
   CoAPMessage for that step — unifying "step should succeed" and
   "step should fail this specific way" under one path.
5. **Real handshake vs. isolated probe**: some TestCases (e.g.
   "5.3.1.3 ... across various device reset types") send a bare,
   single-shot SPAKE2+ POST with a literal/fixed `rnd` and no
   `<TOOL.*>` placeholder — not a real 3-step handshake at all. The
   dispatcher used to always hijack ANY spake-path POST into the
   crypto special-case, incorrectly trying to collect 2 more "steps"
   that don't exist and scanning into unrelated subsequent content.
   Fixed with `_is_real_spake_exchange()`: only enters the crypto path
   when the immediately-following response is itself a success (2.xx)
   — an isolated probe's response is the error code under test, so it
   falls through to ordinary `_send_request`/`_expect_response`
   dispatch like any other message. This needed a `.well-known/knx/
   spake` entry in `resource_schema.py` (`id`→0, `pw`→8, `shareP`→10,
   `confirmP`→14, `rnd`→15 — cross-checked against `knx_spake2plus.py`
   and `oc_knx.c`'s spake key enum) since isolated probes bypass the
   crypto client's hardcoded wire-key dicts entirely.
6. **OSCORE context re-provisioning reset `ssn` to 0 on every
   `pahctx`, even for identical re-provisioning**: `AUTH PREPARATION`'s
   `TokenTest` context (fixed `senderid`/`mastersecret`) gets re-run up
   to 3x within a single TestCase (5.3.1.3). `_apply_pahctx` always
   rebuilt a fresh `OscoreContext` (fresh `ssn=0`) on every invocation,
   even when the params were byte-for-byte identical to what was
   already materialized — but the DEVICE's own replay window for that
   kid persists across re-preparation (it isn't reset just because
   EITT re-runs the same pahctx params), so the client's SSN silently
   went backwards and got flagged as a replay
   (`oc_replay_check_client`: "inside window ... -> replay") on the
   very next request. Fixed: `_apply_pahctx` now only rebuilds when
   the context's identity (sender_id/recipient_id/master_secret/
   context_id/salt) actually changes; identical re-provisioning keeps
   the existing context object (and its `ssn`) untouched.
7. **OSCORE context not invalidated on a genuine factory reset**:
   the flip side of #6 — a REAL `{"cmd":"reset","value":2}`
   (`RESET_TO_DEFAULT_STATE`) does wipe the device's entire AT table
   and replay state (`oc_knx_dev.c`: `oc_delete_at_table()`), so the
   client-side context MUST be dropped and rebuilt (fresh `ssn=0`)
   after this specific command succeeds, or the next request reuses/
   exceeds a partial IV the device no longer recognizes. Deliberately
   scoped to exactly `{2: "reset", 1: 2}` — NOT a bare `{"cmd":
   "restart"}` (`oc_knx_device_restart()`'s own comment: "DON'T delete
   at table", only the transient PASE token is removed) and NOT
   `{"cmd":"reset","value":7}` (`RESET_TO_DEFAULT_WO_IA` calls
   `oc_delete_at_table_except_sec_scope_entries()` — an `if.sec`-scoped
   token like `TokenTest` survives it). Getting this scoping wrong in
   either direction breaks a different TestCase — confirmed by hitting
   both failure modes in sequence while narrowing it down.
8. **Hex-literal comparison format**: `Format="Hex"` Field defaults
   expand to space-separated, uppercase hex (`"54 6D 70 54 6F 6B"`),
   but actual byte values render via `bytes.hex()` as compact
   lowercase (`"546d70546f6b"`) — `_value_matches()` compared these as
   exact strings and always failed. Fixed by normalizing both sides
   (strip whitespace, lowercase) before comparing pure-hex `h'...'`
   literals specifically (an embedded-regex literal like
   `h'[0-9,a-f]{32}'` still falls through to the regex path
   unaffected).
9. **Wildcard `.*` not recognized as whole-body wildcard**: EITT's own
   literal `*` wildcard was recognized, but some TestCases (5.3.20.1)
   use `.*` instead — the regex form, but as the ENTIRE `<Content>`,
   not a continuation line inside an `all:` block (which was already
   handled). Fixed: `match_content()`'s top-level wildcard check now
   accepts both `*` and `.*`.

All of the above were found by cross-referencing the DEVICE's own
server-side debug logs (`run-in-docker.ps1 -ServerLogs`) against the
client-side symptom, not by guessing from the Python traceback alone —
several (`#6`/`#7` especially) would have been very easy to
mis-diagnose as a device bug without that evidence.

**Migration is complete.** The hand-written suite was deleted once the
interpreter covered its ground (commit `3a5e2d4a`); this is now the only
runtime conformance suite.

## Known gaps / next steps

0. **The veth pair needs a ULA address, and even then it is not the real
   transport** (2026-07-25). Two separate facts, both easy to trip over:

   - `KNX_MULTICAST_SCOPE` defaults to **5** on both sides
     (`CMakeLists.txt` line 84; `conftest.py` `MC_SCOPE`), but a veth pair
     comes up with link-local `fe80::/10` addresses only, which score 2
     under RFC 6724. `oc_init_read_next` (`oc_knx_fp.c`) refuses to send
     any device-originated s-mode message until
     `oc_connectivity_get_network_scope() >= SCOPE_SITE_LOCAL`, so it
     logged *"network not (yet)up with correct scope"* **651 times** in one
     run, gave up after 20 attempts, and sent nothing. `run-in-docker.ps1`
     now adds `fd11:9988:7766::1/2` — that count is 0 and the device really
     does send s-mode multicasts. Worth checking first whenever a
     device-originated telegram "never arrives".
   - The DUT now runs in its **own network namespace** (`ip netns exec
     dut`, set via `RUNTIME_TEST_SERVER_PREFIX`), which is what makes the
     veth pair the real transport and gives each side a distinct source
     address. That was required for 5.4.1.4/5.4.1.15 — see
     "DUT-originated UNICAST telegrams" above, including the dead ends
     not to retry.
1. **`DnsMessage` playback — IMPLEMENTED.** See "mDNS discovery
   (DnsMessage)" above for the model (one query, one whole answer, a
   rolling record cache) and the two traps worth remembering: the
   wildcard multicast join, and `@@[MDNS"rqcheck"` being a real
   instruction rather than narration.
2. **OSCORE replay/kid-negotiation attributes — IMPLEMENTED
   (2026-07-24 session).** Kept in this list as the reference for HOW
   they work and which pitfalls to preserve, not as an open gap; the
   only unresolved sub-item is `"asinrequest"`/`CheckRxKid` in #5.
   `InvalidAuthTag` (corrupt the AEAD tag →
   5.3.18.1 PASSES), `SeqNoFn="fix"`+`SeqNo` (pin the Partial IV), and
   `RndKid` (send an unknown kid) are now wired end-to-end through
   `OscoreContext.protect_request` → `CoapClient.oscore_request`
   (`seq_no_override`/`invalid_auth_tag`/`kid_override` params) →
   `runner._resolve_oscore_overrides`. The Echo-retry loop is disabled
   for any request carrying one of these (they are one-shot
   deliberately-malformed probes; retrying would resync the SSN and
   defeat the test).

   **`SeqNoOffset` replay-window walk — FIXED (5.3.17.1 PASSES).** The
   EITT-faithful model, confirmed from `traces/5_3.xml` (the OSCORE
   Partial IV literally IS EITT's per-context sequence counter: SeqNum
   103173 then 103142,103143,… = base+31 then base,base+1,…): a
   `SeqNoOffset=N` send still CONSUMES/advances the context's sequence
   counter by 1, but TRANSMITS that consumed number shifted by N.
   Consecutive offsets therefore accumulate exactly as EITT's do, and
   because `oc_replay.c`'s `oc_replay_check_client` only ever compares
   RELATIVE to the highest SSN seen (window size 32; +31/-1 inside →
   2.04, -32/-35/-64 outside → 4.01), the absolute base is irrelevant —
   only the offset pattern matters. Implemented as the `seq_no_offset`
   param on `protect_request` (distinct from the non-advancing
   `seq_no_override` used by `SeqNoFn="fix"`).

   **Echo-synchronisation — DONE (5.3.17.3 and 5.3.17.4 PASS; every
   `InvalidTag` is gone).** `CoapResponse` carries any captured Echo
   option (252) as `echo_value`; the runner stores the DUT's last
   challenge (`self._last_echo`) and an `Echo="sync"` telegram echoes it
   back via the `echo=` param on `oscore_request` (`Echo="fix"` sends
   the literal `EchoValue`). The adversarial `max_retries=1` path
   returns a `4.01`+Echo to the runner to capture rather than swallowing
   it in an auto-retry.

   The AEAD detail that blocked this, resolved by byte-comparing the
   client's inputs against the device's `-ServerLogs`: a response whose
   OSCORE option carries its OWN Partial IV is a fresh message the
   device signed with ITS sender context, so nonce, AAD and KEY all come
   from that context, not from the request —
     - nonce: built with an **EMPTY** sender id (a KNX group AT's
       device-side sender context has Sender ID ""), i.e. just
       `common_iv XOR resp_piv`. Verified arithmetically: with
       `common_iv=37ec5d919a6b04fa859b1892b9`, empty-id + `piv=00`
       reproduces the device nonce exactly, whereas the context's
       mirrored 7-byte `recipient_id` yields `30fc77aad6366a85859b1892b9`
       — precisely the wrong nonce that produced the `InvalidTag`.
     - AAD: uses the **response's** own PIV, not the request's.
     - key: derived with that same empty sender id
       (`_empty_sender_id_key()`, cached).

   **These three are NOT safely no-ops for other contexts — that
   assumption was wrong and cost a 22-test regression (2026-07-25).**
   `unprotect_response` now computes the ordinary RFC 8613 construction
   as a baseline and treats the empty-id one as an *alternative*,
   trying whichever the context suggests first and falling back:

   - Gating the empty-id construction on `resp_has_own_piv` alone breaks
     every Observe test: an observe registration response and every
     notification also carry their own Partial IV (§4.1.3.5.2 — that PIV
     is how the observe sequence is bound) yet use the ordinary
     construction, with the AAD on the REQUEST's PIV
     (`test_observe.py::obs_e2_notification_aad_binds_request_piv`
     pins exactly this).
   - Gating it on a non-empty `recipient_id` alone breaks
     `test_group_comm.py`: the per-response context it builds for a
     multicast echo challenge has the same shape but needs the baseline,
     because it is keyed on the device's random echo kid_ctx, not on the
     group's own context id.
   - `id_context` length separates those two, and principledly: the
     device marks an echo response with a kid_ctx of exactly
     `OC_ECHO_KID_CTX_LEN` (10) bytes — `oc_oscore_engine.c` keys its
     whole `is_inbound_response` branch on that — while a provisioned
     group context id is shorter.
   - The KEY alternative is broader than the nonce/AAD one: BOTH the
     Echo 4.01 and the 2.xx that follows the retry are signed with the
     group's empty-id sender key, so it applies whether or not the
     response carries its own PIV.

   AEAD authentication is the final arbiter — a wrong guess fails with
   `InvalidTag` rather than mis-decrypting, which is what makes the
   fallback safe. If you touch this, run BOTH
   `-k "Security::"` and `-k "obs_ or multicast_ or long_ga_"`: each
   alone will happily pass while the other is broken.

   (Note: the ordinary RFC 8613 Echo-challenge-and-retry flow every
   normal OSCORE exchange goes through is NOT part of this gap — that
   already works via `CoapClient.oscore_request()`, confirmed against
   real traces; this item is only about the *deliberately adversarial*
   negative-test attributes.)
3. **Multicast group RECEIVE — IMPLEMENTED (2026-07-25 session);
   section 5.3 is now 44 passed / 1 skipped / 0 failed.** Kept here for
   the mechanics and the traps.

   An `OUT` message on a `Multicast*Rx` SecContext is a telegram the DUT
   sent to the GROUP ADDRESS, and it comes in two shapes that need
   different handling — `_expect_group_telegram` branches on whether a
   listener is already armed:

   - **Solicited** (5.3.19.5's 100 `st: r` → `st: a` pairs): the DUT
     answers the tool's group read from *inside its own request handler*
     (`oc_knx_client.c` "Sending s-mode multicast"). Binding a listener
     after the read has been sent races the answer and usually loses it,
     so `_prepare_for_request` looks ahead one step and calls
     `CoapClient.arm_group_listener()` BEFORE sending. That split — arm,
     send, collect — is the whole reason `recv_group_telegram()` was not
     enough on its own.
   - **Unsolicited** (5.3.17.6): an `OUT` with no preceding request at
     all — the sensor-press telegram. Nothing armed a listener, so the
     step arms one itself and then pokes `/test/trigger` (the stack's
     stand-in for EITT's "press the sensor" manual step, same
     substitution `overrides.py`'s `action: trigger` makes). The
     datapoint to poke is the href of the `fp/g` entry written with
     `cflag & (1<<6)` (`OC_CFLAG_TRANSMISSION`) — `_remember_transmit_href`
     records it. Do NOT trigger on the solicited path: an early version
     did, which changed the very value the test was about to assert on.

   `GroupListener` stops at the first telegram that DECRYPTS under the
   bound context rather than the first datagram — a joined group also
   carries the tool's own echoed traffic — and stopping early rather
   than waiting out the timeout took 5.3.19.5 from 8m36s to ~11s.

   **A group telegram step may carry `SecContext=""`** — resolve the context
   from the JOINED GROUP's binding, not just the step. This was 5.4.1.9
   ("Set Init Flag Causes S-Mode Group Message on Startup"), the last 5.4.1
   failure, and the device was never at fault: it sends the init read
   correctly (`Sending s-mode multicast r` to
   `ff35:30:fd11:9988:7766:0:8000:1`, 0.4s after the restart; trace TelNo 185
   records it with `ctx=MulticastRx`, and the `fp/g` write is
   `08 18 20` = cflag **32** = `OC_CFLAG_INIT`, `1 << 5`).

   Our bug: `_prepare_for_request` only armed a listener when the next step
   had a NON-EMPTY `SecContext`, and 5.4.1.9's `OUT POST` step has none —
   while the telegram IS protected under `MulticastRx`, which `MCastGrp01`
   inherits from 5.4.1.3 onwards. Nothing was armed, and the telegram was
   gone 0.4s later while `_apply_wait` slept. Two parts to the fix:
   fall back to `joined_group_with_context()`, and honour the step's own
   `TimeToNext` (`#RESTART_TIME_TO_NEXT` = 20s) instead of a flat 5s, since
   the device has to come all the way back up first.

   **Trap:** checking this TestCase in isolation reports `ctx=None` for the
   group and sends you down a wrong path — the `MulticastRx` binding is only
   present once earlier TestCases in the collection have run. Another case of
   never diagnosing one TestCase alone.

   **The tool as Echo challenger** (5.3.17.6's `IN` step whose `Code` is
   `4.01`, not a method): `knx_oscore.protect_echo_challenge()` +
   `CoapClient.send_echo_challenge()`. The device validates this in
   `oc_oscore_receive_message`'s `is_inbound_response` branch and is
   strict about three things: `kid` must be the AT entry's sender id (it
   is NOT randomised); `kid_context` must be exactly
   `OC_ECHO_KID_CTX_LEN` = **10** random bytes, which is the device's
   marker for "this is an echo response" and is what `RndKid="yes"`
   randomises on this step; and the Partial IV must be the challenged
   telegram's own PIV (`SeqNoFn="asinrequest"`), because
   `oc_oscore_echo_check_and_consume` looks up `(kid, ssn)` in a
   whitelist the device anchored when it sent that telegram. The CoAP
   token must also be quoted back — the device correlates the echo to its
   pending s-mode message by mid+token.
4. **`resource_schema.py` only covers the foundational resources**
   validated so far (`.well-known/knx*`, `/a/lsm`, `/p/*`, `/dev/*`,
   `/a/swu`, `/auth/at` entries, `/test/trigger`). Every new section
   will surface `KeyError`s for unmapped paths — this is by design
   (loud, not silent), but expect real work per section. The traces in
   `tests/eitt/data/traces/` are the fastest way to cross-check a
   new schema entry's `RawContent` against real wire bytes before
   trusting it.
4b. **`@@[pahsnc` — WIRED UP (2026-07-25 session).** 21 uses. It was
   parsed into `seq_no_check_enabled` and read by nothing;
   `SequenceRunner._script_owns_echo_handshake()` now consumes it to
   suppress `oscore_request`'s automatic Echo-retry, alongside the
   pre-existing "next active OUT is a `4.01`" lookahead. Neither signal
   subsumes the other, so both are kept: an `off` region spans many
   steps, while adversarial one-shots outside any region (5.3.17.1's
   replays) rely on the lookahead alone.

   **What it actually means is broader than the Echo handshake.** EITT
   describes it as "switch automatic CoAP sequence number
   synchronisation on/off", and the template uses it in two shapes:

   - **Tight regions** (5.3.17.4/.5/.6/.7/.8, 1–2 requests) wrap exactly
     an explicitly-scripted challenge/retry: request → `4.01` OUT with
     `Echo="sync"` → replayed request, also `Echo="sync"`. Auto-retrying
     inside the first send would leave the `4.01` step with nothing to
     match and make the replay step a duplicate.
   - **Sweep regions** (5.3.17.1 = 137 requests, 5.3.17.3 = 138) wrap a
     whole anti-replay-window walk whose requests carry deliberate
     `SeqNoOffset="31"` / `"-1"`. Here the point is "do not resynchronise
     the SSN behind my back" — resync would destroy the very offsets the
     TestCase asserts on. The Echo handshake is one instance of that
     resync, not the whole of it.

   Two traps found while wiring it:

   - **The sweep regions never close.** 5.3.17.1 and .3 open `off` and
     have no matching `"on"` before the TestCase ends (verified by
     walking the parsed sequence). Since the `ProvisioningEngine` is
     shared by every TestCase in a collection, that leaked. `run()` now
     re-asserts `seq_no_check_enabled = True` per TestCase, which also
     stops a mid-region failure's early `return` from silently disabling
     auto-retry for every later TestCase.
   - Inside a sweep region roughly **half** the requests are innocent
     verification reads (`GET dev/pm`, `GET #TABLE_POINT_PATH_1`)
     interleaved between the adversarial writes, so `off` suppresses
     retry on those too. That is EITT's semantics, and it is fine on
     today's device — verified by run: 19 auto-retried challenges, all
     of them in the preparation phases *before* each region opens, and
     counts identical (19 retries, 8 passed, 1 skipped) with the change
     and against committed HEAD. Section 5.3 stays 44 passed/1 skipped
     and 5.1 was unaffected (39/42 at the time; now 42/42, see below).

   Found by grepping each `@`-command flag for a consumer; do that again
   after adding any new command (see the comment-command note above).

5. **`CheckRxKid`'s exact behavior remains unconfirmed** — the traced
   runs don't exercise it distinguishably, and the 5.3.17.x tests that
   name it pass without it being interpreted (so it may be a
   display-only hint, or matter only on a device that behaves
   differently). `SeqNoOffset`, `SeqNoFn="fix"`, `InvalidAuthTag` and
   `RndKid` ARE implemented and trace-verified — see gap #2.
   `SeqNoFn="asinrequest"` is now understood and implemented for the one
   place it matters, the tool-sent 4.01 Echo challenge ("reuse the
   challenged telegram's Partial IV" — see gap #3); whether it means
   anything elsewhere is still unknown.
6. **One failing TestCase can cascade-fail everything after it within
   the same collection bundle** — confirmed in "5.2. /.well-known/knx":
   `5.2.6.1`'s fixture-data mismatch left the shared runner in a state
   that made `5.2.7.1b`/`5.2.8.1`/`5.2.9.1`/`5.2.10.1b` also fail
   (their own prep references hit the same SPAKE2+-rejected-because-
   AT-already-exists 4.00 pattern). This mirrors what would happen on
   real hardware in the same scenario, so it may not need a fix per se
   — but it means a single fixture-data issue can currently mask
   several TestCases' real pass/fail status. Worth deciding whether the
   runner should attempt a defensive factory-reset-and-continue after
   an unexpected mid-collection failure, or whether "let it cascade,
   exactly like real EITT would" is the intended, accurate behavior.

   (The 5.2 example is now historical — that collection is 19/19. Note
   the masking was real but partial: once `5.2.6.1` was fixed, three of
   the four "cascade" failures cleared, while `5.2.9.1` turned out to
   have had four independent defects of its own all along. Treat a
   cascade as "these results are unknown", not "these will pass".)

**Resolved** — kept here only as a changelog note, not an open item:

- By real EITT trace-log validation (see "Trace-log validation" above):
  the `/auth/at` write envelope (always a bare CBOR array, never
  `{0: entry}`), `RspSchema` (confirmed genuinely unused, not just
  empty-by-coincidence), and confirmation that the existing OSCORE
  Echo-challenge retry logic is already correct and complete.
- `test_eitt.py`'s `_check_overrides_match_real_cases()` now
  raises at collection time if any `eitt/overrides.yaml` key doesn't
  match a real `TestCase.qualified_name` — verified against both a
  clean run (187 collected, no error) and a deliberately misspelled
  key (collection fails with the exact bad key named).
