---
name: project-runtime-test-tiers
description: "2026-09-10 pivot on feature/runtime_tests_v2: deleting the hand-written runtime suite was a MISTAKE; target is three tiers (unit / EITT interpreter / runtime), upstream's 349 tests restored verbatim after the rebase and awaiting per-test classification + port onto tests/runtime/transport"
metadata:
  node_type: memory
  type: project
  modified: 2026-09-10T00:00:00.000Z
---

**Decision (user, 2026-09-10):** the branch's commit "Replace hand-written
runtime suite with the EITT XML interpreter" (`3a5e2d4a` pre-rebase) went too
far. The interpreter replaces only the tests whose requirement the official
template covers. Everything else the hand-written suite tested — and upstream
`work_in_progress` kept adding to it, 176 → 349 tests in 20 files by
2026-09-10 (CoAP option/payload hardening, SWU state-machine edge cases,
observe, stricter AT validation, ...) — must survive.

**Target layout: three tiers.**

| Tier | What | Where |
| --- | --- | --- |
| unit | Google Test, untouched by this work | `tests/unit/` |
| eitt | template-driven interpreter, one pytest item per TestCase | `tests/runtime/eitt/`, `test_eitt.py` (exact final directory TBD with the user) |
| runtime | hand-written tests for behaviour the template does NOT cover, ported onto the branch's shared `transport/`, `conftest.py`, `tools/` | `tests/runtime/` |

A hand-written test is dropped only when a template TestCase exercises the
same requirement, and the drop records the EITT case id so it is traceable.

**State after 2026-09-10:**

- Branch rebased onto `knx-org/work_in_progress` (`b6271257`): 25 commits
  replayed + 1 restore commit `14c8e231` "Restore the hand-written runtime
  suite from work_in_progress" = 26 ahead. NOT pushed.
- Backups (do not delete until the user says so):
  `backup/runtime_tests_v2-pre-rebase-2026-09-10` and
  `backup/runtime_tests_v2-2026-09-10-b`, both = old tip `8434a58b`.
- The 19 restored `test_5_*.py` / `test_coap_*.py` files are VERBATIM
  upstream and **do not import** yet: they use the pre-move paths
  `tests/runtime/coap_client.py` / `knx_oscore.py`, which the branch moved to
  `tests/runtime/transport/`. Expected; the port is the next step.
- Rebase unions worth knowing: `transport/coap_client.py` and
  `transport/knx_oscore.py` now carry BOTH upstream's `block2` /
  `oscore_get` / `oscore_put` / `_reassemble_block2` AND the branch's
  adversarial parameters. `tools/run-in-docker.ps1` has upstream's
  `-RunSlowTests` next to the branch switches; `COAP_TIMEOUT` stayed at the
  branch's 5 (upstream: 15) — the knob if Docker runs show sporadic timeouts.
- Upstream's old Tk runner GUI (`test_runner_gui.py`, on the branch as
  `tools/runner_gui.py`) was DELETED 2026-09-10 on the user's decision; the
  PySide6 GUI under `tests/runtime/gui/` is its replacement. Do not restore it.
- `.gitlab-ci.yml` is the ONLY non-test file that differs from
  `work_in_progress`, and only in the runtime-test job (runs `test_eitt.py`
  with a section filter). Once the runtime tier is ported, that job must run
  both tiers.
- Classification report is in the repo: `tests/docs/runtime-test-inventory.md`
  (135 COVERED / 95 COVERED-WEAK / 119 KEEP of 349 functions, 385 items). It is
  the driver for the split.
- Port DONE and Docker-verified 2026-09-10 (commit `27465bd0`): five transport
  methods added verbatim from upstream, 29 import rewrites, test bodies
  untouched. Collection = 766 items (385 hand-written + 187 EITT + 194
  headless).
  - Restored tier alone: **377 passed, 0 failed**, 8 env skips (veth/discovery/
    RUN_SLOW_TESTS) — upstream's own record was 380/5. 13m57s.
  - EITT tier alone: **173 passed, 4 FAILED** (new vs the 177 baseline), 8
    skips, 2 xfailed, 12m18s. Failures: 5.1.1.3e (if= wildcard), 5.2.5.1 and
    5.3.20.1 (/dev/mid rt pattern), 5.2.6.1 (hostname now `…local`). All four
    line up with upstream device commits `80d3cd77` (include_if) and `94803394`
    (hostname `knx-{serial}.local`); diagnosis pending, NOT yet fixed — do not
    paper over them with overrides before the template + trace check.
- Running the Docker script from Git Bash: `-PytestArgs "a","b"` arrives as one
  string and `-IncludeUncommitted` breaks because Git Bash's `tar` shadows
  Windows `tar.exe`. Run it from PowerShell (or `powershell -Command "& ..."`),
  and if the tree is clean just test committed HEAD.

**Why:** the interpreter's purpose is to stop hand-porting each EITT template
revision, not to lose device coverage that KNX never wrote a TestCase for.

**How to apply:**

- Never delete a hand-written test without naming the EITT case that covers it.
- Port in file-sized, reviewable commits; keep upstream's test bodies, change
  imports/fixtures only, and run the ported file in Docker before the next.
- Rebase first, restructure second (done in that order on 2026-09-10 for a
  reason: 107 upstream commits changed device behaviour both tiers depend on).
- Coding is delegated to Opus subagents; the orchestrating session reviews.
- `git rebase` autostash silently swallowed the untracked
  `docs/proposal-resource-caps.md` once; check untracked files after any
  rebase in this repo.

Related: [[eitt-xml-interpreter-project]], [[project-eitt-5-3-status]] (the
"suite is GONE" statement there is superseded by this note),
[[project-eitt-gui-plan]], [[feedback-eitt-always-run-in-docker]].
