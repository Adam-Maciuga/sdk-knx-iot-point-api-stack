---
name: feedback-eitt-never-diagnose-single-testcase
description: "Never debug one EITT TestCase with -k <single case> — preparation lives in sibling TestCases, so isolation invents fake failures. Run the whole collection."
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 4d0fc175-3a82-427c-bd95-25153f25e4ad
  modified: 2026-07-25T19:34:41.358Z
---

When debugging a failing EITT TestCase, do **not** narrow pytest to that one
case (`-k "5.1.2.5a"`). Run the whole collection (`-k "Discovery::"`).

**Why:** the `SequenceRunner`/`ProvisioningEngine` is shared per
TestCollection, and `AUTH PREPARATION` / `IA PREPARATION` run as earlier
TestCases in that collection. Selecting one case skips them, so
`resolve_context("TokenTest")` returns `None` and the runner silently sends
the request **unsecured** — the device then answers `access denied for:
/dev/pm` (`oc_knx_sec_check_acl`: `flags = IPV6` with no
`OSCORE|OSCORE_DECRYPTED`).

That fabricated symptom cost real time twice: it got written into
[[project-eitt-5-3-status]] as "5.1.2.5a/b fail because the device refuses
PUT /dev/pm — an authorisation question", which was wrong. The real causes
were two of our own mDNS bugs (listener not armed; goodbye-vs-announce),
and in the full collection there are **zero** access-denied lines.

**How to apply:** reproduce in the full collection first and confirm the
failure reason there before theorising. If a symptom appears only under
`-k`, treat the `-k` run itself as the bug. When a memory or comment blames
the device for an authorisation failure, re-check whether the evidence came
from an isolated run. Related: [[feedback-eitt-check-template-and-trace]].
