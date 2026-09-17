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
| eitt | template-driven interpreter, one pytest item per TestCase | `tests/runtime/eitt_tests/` (`test_eitt.py`) |
| runtime | hand-written tests for behaviour the template does NOT cover, ported onto the branch's shared `transport/`, `conftest.py`, `tools/` | `tests/runtime/runtime_tests/` (behaviour-named `test_*.py`) |
| harness | tests of the interpreter and GUI code itself, no DUT | `tests/runtime/harness_tests/` (`test_events.py`, `test_comment_scheme.py`, `test_project_copy.py`, `test_gui_*.py`) |

The shared infrastructure — `conftest.py`, `transport/`, `eitt/`, `gui/`,
`data/`, `tools/`, `runtime_test_server.c`, `pytest.ini` — stays at the
`tests/runtime/` level; only the test modules live in the tier directories.

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
    (hostname `knx-{serial}.local`).
  - **Diagnosed 2026-09-11. Expected EITT tier now: 176 passed / 1 FAILED
    (5.1.1.3e) / 8 skips / 2 xfailed.** Root cause of three of the four
    original failures was one bug: `ff269ddc` lowered `KNX_PAYLOAD_SIZE`
    1024 -> 768, flooring `OC_BLOCK_SIZE` at 512, so the 547-553 byte
    link-format bodies of `/.well-known/core` and `/dev` now split into two
    Block2 blocks and the interpreter kept only block 0. The device is
    RFC 7959-correct on **unicast**, and the interpreter now reassembles
    there (finding **T12**): `CoapClient.complete_oscore_block2` plus its
    call from `runner.py::_send_request_inner`. That fixed 5.2.5.1 and
    5.3.20.1. The fourth, 5.2.6.1, is a variable override: `94803394`'s FQDN
    `knx-{serial}.local` is certifiable (the variable-values JSON has another
    certified device answering `knx-02c912345678.local`), so
    `overrides.yaml` sets `5_2_6_1_HOSTNAME: knx-00fa10020800.local`.
  - **5.1.1.3e stays FAILED on purpose — device finding `tests/FINDINGS.md`
    F-021, deliberately not masked.** The same reassembly was written for
    `multicast_get` (unicast continuation to each responder) and reverted: it
    did not fix 5.1.1.3e and regressed 5.1.1.3, but the real reason is that
    the actual EITT tool evaluates whatever arrives on a multicast discovery
    query, so a certification run sees the truncated first block and fails
    5.1.1.3e too. Masking it client-side would hide a certification risk. The
    certified reference trace (`data/traces/5_1.xml` TelNo 28) shows the whole
    547-byte body in one datagram. Fix is the maintainer's call (payload
    budget, block size, or discovery body) — do not "repair" this test.
- **Directory split DONE 2026-09-11**, commit "Split tests/runtime into eitt,
  runtime and harness test tiers": 31 test modules `git mv`d into
  `eitt_tests/` (1), `runtime_tests/` (19) and `harness_tests/` (11), test
  bodies unchanged. Imports resolve via `pythonpath = .` in
  `tests/runtime/pytest.ini` (pytest >= 7), which keeps `tests/runtime` on
  sys.path whatever the cwd or the selected tier — no `__init__.py` files were
  added. The seven modules that resolve `data/` against `__file__` gained one
  extra `dirname`/`.parent`. `tools/run-in-docker.ps1` grew
  `-Tier all|eitt|runtime|harness` (default `all`), and the CI
  `linux-runtime-test-asan` job now runs both device tiers as two pytest
  invocations with separate JUnit reports. Collection after the move is
  unchanged: 766 total = 187 eitt + 385 runtime + 194 harness.
- **Cleanup DONE 2026-09-11: 135 functions / 143 items deleted, 102 annotated,
  runtime tier = 245 items; mapping in commit `c6f05dc6` "runtime tier: drop
  template-covered tests, mark overlaps"** (one commit, per-file sections in
  the body). Two deviations from the estimate, both arithmetic rather than
  classification: `test_device_resources.py` drops 13 items not 5 because
  `test_5_2_7_1b_put_readonly_rejected` is parametrised x9, and
  `test_discovery_mdns.py` drops 5 not 8 because the inventory
  contradicts itself there — its per-file summary row says 8 C / 8 CW, its
  itemised section 3.3 table says 5 C / 11 CW, and section 3 wins. Net 245,
  not the 242 predicted from the summary table. Docs updated in a second
  commit; `EITT_TEST_COVERAGE.md` is now a pointer, not a map.
  Docker-verified: runtime tier **235 passed / 0 failed / 8 env skips**
  (= 245), eitt tier unchanged. The first Docker run caught two defects the
  host-side collect could not: (a) a removed `DUT_SERIAL_NUMBER` import that
  was still used inside a test body — counting `grep` hits is NOT a safe
  unused-import check, use an AST undefined-name scan; and (b) a hidden
  inter-test dependency, `test_full_chain_via_programming_mode_subtype` relied on the deleted
  `test_5_1_2_4_pm_subtype_enabled` having just switched programming mode on.
  **Deleting a test can break a surviving sibling that never had its own
  setup** — after any such cleanup, run the tier in Docker before believing
  the collect count.
- **Renamed 2026-09-11 off the EITT numbering**, commit "runtime tier: name
  tests by behaviour instead of EITT id" (complete old->new mapping in the
  commit body). Rule going forward: **runtime-tier files and functions are
  named for the behaviour they test, never for an EITT id**; template overlap
  is recorded only by `@pytest.mark.eitt_overlap("5.x.y.z")` and, where it
  helps, a docstring starting "Overlaps EITT 5.x.y.z: ...". The 900-series
  pseudo-ids (`5_1_900_*`, `5_4_900_*`, ...) were never template ids and are
  gone entirely. Two consequences worth knowing:
  - `conftest._eitt_sort_key` can no longer order the runtime tier by name, so
    `conftest.RUNTIME_MODULE_ORDER` now declares the module run order
    explicitly (it reproduces the pre-rename sequence). A new runtime module
    must be added to that list.
  - Within a module, order is now definition order, and `test_swu.py` shows how
    sharp that edge is. The old numeric sort satisfied TWO opposite constraints
    at once (5.2.9.0 mfg-date < 5.2.9.1 update < the rest of the lifecycle
    class). Moving the whole `TestSwuLastUpdateLifecycle` class above
    `TestSwuFirmwareUpdate` fixed the first and broke the second: the class also
    holds `test_lastupdate_changes_after_swu_update`, which runs an update and
    bumps `dev/fwv` 0.0.1 -> 0.0.2, so the firmware test's "Initial FWV should
    be [0, 0, 1]" failed (Docker: 1 failed / 236 passed / 8 skipped). Fixed in
    the follow-up commit by splitting the class into `_SwuLastUpdateBase` +
    `TestSwuLastUpdateBeforeUpdate` (before) and `TestSwuLastUpdateAfterUpdate`
    (after). **A class is not the unit of ordering — an individual test is.**
  `.gitlab-ci.yml` and both `run-local-ci*.sh` now run the whole
  `runtime_tests/` directory instead of an explicit file list.
- Running the Docker script from Git Bash: `-PytestArgs "a","b"` arrives as one
  string and `-IncludeUncommitted` breaks because Git Bash's `tar` shadows
  Windows `tar.exe`. Run it from PowerShell (or `powershell -Command "& ..."`),
  and if the tree is clean just test committed HEAD.

**Why:** the interpreter's purpose is to stop hand-porting each EITT template
revision, not to lose device coverage that KNX never wrote a TestCase for.

**How to apply:**

- Never delete a hand-written test without naming the EITT case that covers it.
- Before adding a hand-written test, check the template for a covering
  TestCase; if one exists, do not add the test.
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
