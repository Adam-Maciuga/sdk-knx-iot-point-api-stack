---
name: feedback-eitt-always-run-in-docker
description: EITT runtime suite MUST run via tools/run-in-docker.ps1; a host-native pytest run silently gives 23 failed/155 passed instead of 177 passed
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 4799ccc5-4c9c-4bdd-98d6-09eef940b849
  modified: 2026-07-27T09:58:10.834Z
---

Always run the EITT runtime suite with
`.\tests\runtime\tools\run-in-docker.ps1` (add `-IncludeUncommitted` to
include working-tree changes). **Never** run `pytest test_eitt.py` directly
on the Windows host and never draw a conclusion from such a run.

**Why:** the suite needs a veth pair and a `dut` network namespace that only
the Docker harness creates. On the bare Windows host there is no veth, no
`ip` command, and no container — multicast/group traffic has nowhere to go.
Nothing warns you: collection, execution and reporting all look normal.

Measured 2026-07-27 at commit 94bb8080, identical tree:

| environment | result | runtime |
| --- | --- | --- |
| Docker (correct) | 177 passed / 8 skipped / 2 xfailed | ~8.7 min |
| host-native (wrong) | 23 failed / 155 passed / 8 skipped / 1 xfailed | ~33 min |

Failures cluster in 5.4.1.x (group messaging) and drag 5.3.19/5.3.20 down
with them via polluted state; the ~4x runtime is multicast listeners timing
out.

**How to apply:** the **wall clock is the fastest tell** — a genuine full run
is ~8.7 min. If it takes ~33 min, the DUT is unreachable no matter what the
pass count says; check `docker ps` before reading the failures at all. This
cost three 33-minute runs and nearly caused a correct F7 fix to be reverted
as a "regression"; the giveaway was that the run *with* the fix and the
clean-HEAD baseline were both exactly 23/155.

The host-native noise also **masks real bugs**: a genuine defect of mine in
5.4.1.8 was buried among the 22 environment-induced failures for three runs
and only became obvious when a correct Docker run isolated it as the single
failure.

Section subsetting is separately unreliable — see
[[feedback-eitt-never-diagnose-single-testcase]]. Only a full Docker run is
a valid baseline. Related: [[project-eitt-5-3-status]],
[[reference-eitt-interpreter-findings]].
