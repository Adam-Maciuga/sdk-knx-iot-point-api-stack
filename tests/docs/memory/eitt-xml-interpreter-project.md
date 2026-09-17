---
name: eitt-xml-interpreter-project
description: Ongoing work building a dynamic EITT XML test interpreter for knx-iot-point-api-stack
metadata: 
  node_type: memory
  type: project
  originSessionId: 0efacc70-def0-424f-a2b4-2abab6ff9f7a
  modified: 2026-07-23T13:24:54.580Z
---

Daniel is building a from-scratch interpreter (`d:\Repos\knx-iot-point-api-stack\tests\runtime\eitt\`) that reads `tests/eitt/data/KnxConformanceTestTemplate-PointAPI.xml` directly and generates pytest test items dynamically at collection time via `pytest_generate_tests` (in `test_eitt.py`), instead of hand-porting each EITT XML revision into the existing 176-test hand-written suite (`test_5_*.py`, tracked in `EITT_TEST_COVERAGE.md`).

**Full design and status live in the repo, not here**: `tests/docs/eitt-xml-interpreter.md` — read that file first in any future session, since it is the cross-machine source of truth (this memory file is local to one machine only). It documents the module breakdown, every real bug caught during development (variable-name regex, comment-command argument-shape parsing, hex-vs-ASCII sender IDs, the non-contiguous SPAKE2+ exchange, the `"h'<hex>'"` literal-string CBOR bug, the missing per-response-path in `_expect_response`), migration status, and known gaps/next steps.

**The trace logs are the ground truth.** 9 real EITT-produced traces (`tests/eitt/data/traces/5_1.xml` … `5_10.xml`, one per section) plus a Fields-default dump are committed in-repo. They have settled nearly every ambiguity in this work — the `/auth/at` envelope (always a bare CBOR array), `RspSchema` (genuinely unused), the mDNS query/answer model, the SWU defaults. Read them before inferring anything; see [[feedback-eitt-check-template-and-trace]].

**Migration is COMPLETE** (commit `3a5e2d4a`): the hand-written `test_5_*.py` suite has been deleted and this interpreter is the only runtime conformance suite. Live per-section status, directory layout and next steps are in [[project-eitt-5-3-status]], not here.

Related: [[knx-iot-runtime-tests]] (if that memory exists — general runtime test conventions).
