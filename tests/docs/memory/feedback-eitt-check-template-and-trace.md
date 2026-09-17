---
name: feedback-eitt-check-template-and-trace
description: "HARD RULE for EITT work — check in order: (1) template XML incl. Activate state, (2) the matching data/traces/5_X.xml, (3) EVERY comment command (@@[pah..., @@!, @>w). Never skip comment lines. When STARTING a new test category, do this check and PLAN first — never run blindly and fix errors reactively."
metadata:
  node_type: memory
  type: feedback
  originSessionId: 0efacc70-def0-424f-a2b4-2abab6ff9f7a
  modified: 2026-07-25T23:28:23.655Z
---

**The user has asked for this to be remembered HARD.** Applies to the EITT
dynamic interpreter (`knx-iot-point-api-stack`, `tests/runtime/eitt/`, see
[[eitt-xml-interpreter-project]]) in TWO situations:

- **Starting a new test category** (a new 5.x collection/suite) — do the whole
  check below and **PLAN what is needed BEFORE running anything**. See
  "Starting a new category" at the end.
- **Diagnosing any individual failure** — work the three steps in order before
  theorising and before calling anything a device gap.

## 1. The template XML
`tests/runtime/data/KnxConformanceTestTemplate-PointAPI.xml`. Read the exact
TestCase. Check `Activate="yes"` vs `"no"` on EVERY CoAPMessage/Comment — a
`Activate="no"` or `<!-- -->` block is invisible to real EITT and to the parser.
Never assume XML text is "live".

## 2. The corresponding trace
`tests/runtime/data/traces/5_X.xml` — recorded real EITT runs against real
hardware. This is GROUND TRUTH for wire bytes (`RawContent`), addressing
(`DestinationAddress`/`LocaIP`), `Mid`/`Token`, timing, and `EvalResult`.
Decode `RawContent` rather than guessing the CBOR.

## 3. EVERY comment command in that TestCase
**Never skip the comment lines — they are often the important part.** They are
behavioural instructions, not narration ([[feedback-eitt-comment-commands-are-behavioural]]).

- **`@@[pah...`** — enumerate every `pahctx` / `pahmcast` / `pahdev` /
  `pahmdns` / `pahsnc` / `pahdiscover` param the TestCase uses, then confirm the
  interpreter actually CONSUMES each one (grep `provisioning.py` — a param can
  be parsed and then read by nothing).
- **`@@!<text>`** — "display comment in toolbar, SUSPEND EVALUATION and beep":
  a blocking operator modal for a PHYSICAL action. Mapped by TEXT in
  `overrides.yaml`'s `manual_actions:`; `@@!` alone does NOT mean "restart".
  (`@!` single-@ is informational only.)
- **`@[w` vs `@@[w`** — countdown vs modal dialog; different semantics.
- **`@#"..."`** inline preparation reference, **`@>w"...;unlimited"`** loop.

Look every one up in `data/EITT_KnxCommentCommandsScheme.xml` — EITT's own
grammar — for the real semantics; do not infer them from the surrounding text.
Remember state from these commands PERSISTS ACROSS TestCases in a collection
(the engine is shared), so a TestCase may inherit group ids, contexts and
bindings it never sets itself.

## Why (each of these actually bit)
- Skipped `Activate`: a commented-out confirmP POST led to inventing a
  "3 single-shot probes" SPAKE2+ requirement that never existed.
- Skipped trace: 5.3.20.1 sat as "not root-caused" until `traces/5_3.xml`
  showed `{0: "knx://sn:..."}` — a missing `id` key in the `/p` schema.
- Trace decode solved `a`=97 in the `/k` schema (`18 61` = uint8 97), fixing
  three 5.4.1 TestCases at once.
- Trace `Mid`/`Token` + a 0.1s gap (TelNo 98 → 101) revealed 5.4.1.4's telegram
  was being discarded as a "stale token" — the actual bug.
- Skipped comment commands: `@@[pahsnc` sat parsed-but-unconsumed for 21 uses,
  and `pahmcast"...;context="` is what makes a context-less `OUT` telegram
  resolvable at all.

## Starting a new test category — PLAN, don't run blindly
**The user asked for this explicitly: do NOT just run the section and fix
whatever errors come back.** Reacting to failures one at a time wastes runs
(each full section is 2–5 minutes) and leads to wrong conclusions — a failure's
message often describes a symptom three layers from the cause.

Before the first run of a new collection/suite:

1. **Enumerate the TestCases** and their `Activate` counts. A TestCase with 0
   active messages is an empty placeholder, not a failure waiting to happen
   (5.3.1.4b/c cost an hour of wrong xfails for exactly this).
2. **Read the trace** for that section (`data/traces/5_X.xml`) — does it even
   cover these TestCases? Tally `EvalResult` values. A section that is all
   "OK" in the trace means every failure is OURS; a TestCase absent from the
   trace was never executed by the certified run either.
3. **Enumerate every comment command used in the section** and check each has a
   consumer. This is where whole features hide (`pahsnc` unconsumed for 21
   uses; `@@!` ignored for all 4).
4. **Check the accepted-deviations list**
   ([[reference-eitt-certification-accepted-deviations]]) for that section —
   some "failures" are officially accepted (e.g. 4.00-vs-4.03 "Forbidden").
5. **Check which resource paths are new** — `resource_schema.py` raises loudly
   on unmapped paths by design, so expect real work there and grab the field
   mappings from the trace's `RawContent`, not by guessing.
6. **Then write down what needs implementing**, and only then run.

**Also:** never diagnose one TestCase with `-k <single case>` — preparation
lives in sibling TestCases and isolation fabricates failures
([[feedback-eitt-never-diagnose-single-testcase]]). And don't start a second
docker run while one is still going — they fight over the same veth/netns
names; check `docker ps` first.
