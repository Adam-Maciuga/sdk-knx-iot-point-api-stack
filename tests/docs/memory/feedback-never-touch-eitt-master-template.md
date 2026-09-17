---
name: feedback-never-touch-eitt-master-template
description: "NEVER edit the EITT master template XML — not even a temporary, backed-up, restored-immediately edit. Use a scratch copy instead."
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 4799ccc5-4c9c-4bdd-98d6-09eef940b849
  modified: 2026-07-26T00:33:27.398Z
---

**Never modify `tests/runtime/data/KnxConformanceTestTemplate-PointAPI.xml`.**
The user stated this flatly on 2026-07-26 ("never touch the master template")
after I twice made a deliberate one-line edit to it, ran a test to prove the
matcher was strict, then restored from a backup.

The prohibition covers the temporary case too. A backup-and-restore edit still
mutates the reference artifact on disk, and "I restored it afterwards" is not
the point — do not do it at all, and do not ask to do it as a one-off.

**Why:** it is the read-only source of truth for the whole conformance suite
(see [[reference-eitt-three-layer-variable-model]] — read-only template +
variable-values JSON + generated copy). A mid-run crash, a botched restore, or
an interrupted turn leaves a silently corrupted reference that every later
section is then measured against. The user reviews all changes before commit,
and a mutation here is exactly the kind that reads as noise and hides real work.

**How to apply:** when you need to prove an assertion is real rather than
vacuous — the legitimate goal that led me here, since an `all:` link-format rule
passes vacuously on an empty list — get the evidence WITHOUT touching the
master:

- copy the template to the scratchpad and point `XML_PATH` (in
  `tests/runtime/eitt_tests/test_eitt.py`) at the copy, or
- read the decoded response payload straight out of the runner / a debug hook,
  or
- cross-check the recorded payload in the matching `tests/runtime/data/traces/5_X.xml`.

Related: [[project-eitt-5-3-status]].
