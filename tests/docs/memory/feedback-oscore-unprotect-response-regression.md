---
name: feedback-oscore-unprotect-response-regression
description: "knx_oscore.unprotect_response's empty-sender-id branch is an ALTERNATIVE, not a replacement — how to change it without regressing observe/group-comm, and how to tell a real regression from a pre-existing failure."
metadata:
  node_type: memory
  type: feedback
  originSessionId: 0efacc70-def0-424f-a2b4-2abab6ff9f7a
  modified: 2026-07-25T01:13:50.334Z
---

In `tests/common/transport/knx_oscore.py`, `unprotect_response` must compute the ordinary RFC 8613 construction as the BASELINE and treat the KNX-group empty-sender-id construction as an alternative that is ordered first or second and fallen back from — never as an unconditional replacement.

**Why:** four different gating attempts each looked correct and each regressed a different set of tests.
- `resp_has_own_piv` alone → 22 observe/group-comm failures. Observe registration responses and notifications also carry their own PIV (it binds the observe sequence) but use the ordinary construction with the AAD on the REQUEST's PIV.
- non-empty `recipient_id` alone → 7 group-comm failures. the per-response multicast-echo context 5.4 group-comm builds has the same shape but needs the baseline (it is keyed on the device's random echo kid_ctx, not the group's context id).
- baseline-first unconditionally → 4 section-5.3 failures.
- Also: the KEY alternative is broader than the nonce/AAD one — BOTH the Echo 4.01 and the following 2.xx are signed with the group's empty-id sender key, so it applies even when the response has no PIV of its own.

`id_context` length is the principled discriminator: the device marks an echo response with a kid_ctx of exactly `OC_ECHO_KID_CTX_LEN` (10) bytes (`oc_oscore_engine.c` keys its whole `is_inbound_response` branch on that); a provisioned group context id is shorter. AEAD authentication is the final arbiter, so a wrong guess fails cleanly with `InvalidTag` instead of mis-decrypting — that is what makes the fallback safe.

**How to apply:** when touching this function, exercise MORE than one section — each construction can pass alone while another is broken. The hand-written `obs_`/`multicast_` tests that originally caught this are gone (commit `3a5e2d4a`); their coverage now has to come from the dynamic suite, so run at least `-k "Security:: or well-known/knx:: or Discovery::"`, and treat 5.4 Messaging (Observe + group comms) as the case most likely to expose a bad gating choice once it is migrated. More generally, before calling any failure pre-existing, get a real baseline: run the same `-k` selection WITHOUT `-IncludeUncommitted` (committed HEAD), and read the known-good implementation with `git show HEAD:<file>` rather than reasoning about whether a change "could" matter — I assumed additive-with-default-args changes were safe and was wrong. Note `run-in-docker.ps1` scans whole tier directories (`tests/eitt/ tests/runtime/` by default, or the one `-Tier` names), so passing file paths WIDENS the selection; use `-k` to narrow.

Related: [[project-eitt-5-3-status]], [[feedback-eitt-check-template-and-trace]].
