---
name: reference-eitt-three-layer-variable-model
description: "How EITT turns its read-only conformance template into a runnable test — the three variable layers (template defaults / variable-values JSON / generated copy) and why template defaults must not be trusted."
metadata:
  node_type: memory
  type: reference
  originSessionId: 0efacc70-def0-424f-a2b4-2abab6ff9f7a
  modified: 2026-07-25T10:54:51.262Z
---

General EITT knowledge (from the tool's owner, 2026-07-25). A runnable EITT test is built in THREE layers, not one:

1. **Conformance test template** (`tests/runtime/data/KnxConformanceTestTemplate-PointAPI.xml`) — ships with EITT, **READ-ONLY**. Its `#VAR` tokens and `Fields`/`DefaultValue` entries are only the SLOTS, and the defaults are conservative placeholders.
2. **Variable-values JSON** (`tests/runtime/data/2026-02-25_09-36_EittConformanceTestVariableValues.json`) — itself layered: `GLOBAL` block → test-suite block (keyed GUID + Name + **Version**; ours is V40 of "8/10/5 - KNX IoT Point API Tests", matching the traces' `PlanSource="Copy of 5.3. Security V40"`) → that suite's per-`Collections` blocks. Later layers win. Most string values are space-separated hex of the ASCII (`"70 2F 31"` = `p/1`); many are deliberately EMPTY awaiting the operator.
3. **The generated copy** — selecting a subset of the test tree produces a working copy with values resolved in. That copy runs, and an edited parameter can be pushed into an already-generated copy.

**Do NOT trust a template default.** Diffing all 112 variables: 10 disagree. 4 are case/format noise; 4 are unfilled operator placeholders where OUR template is more correct (`{Datasheet Hostname}`, `C:/path/to/file.bin`, `Windows` vs `Windows|Linux`); **2 are substantive** — `GENERAL_TIME_TO_NEXT` is 1000 in the template but 500 in the JSON (a V31-refactor leftover), and it is referenced by **1489** messages, so trusting the template made every run sleep twice as long as real EITT.

In our interpreter this is `VariableTable.for_collection(project, name, overrides=...)`, with layers 2/3 living in the `variables:` section of `tests/runtime/eitt/overrides.yaml`. `PREP_UPDATE_FILE_PATH` is set there too, pointing at the checked-in `tests/runtime/data/FWUUploadDummySmall.bin` (5120 bytes) that the SWU upload-block preparations read.

Related: [[project-eitt-5-3-status]], [[eitt-xml-interpreter-project]].
