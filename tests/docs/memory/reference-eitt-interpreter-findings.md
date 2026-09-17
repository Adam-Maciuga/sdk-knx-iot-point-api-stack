---
name: reference-eitt-interpreter-findings
description: "Tracked audit findings for the EITT interpreter — the live list lives in the repo at tests/docs/eitt-interpreter-findings.md; update an entry's status in the same commit that fixes it."
metadata: 
  node_type: memory
  type: reference
  originSessionId: 4799ccc5-4c9c-4bdd-98d6-09eef940b849
  modified: 2026-07-29T07:43:54.797Z
---

**The findings tracker is in the repo: `tests/docs/eitt-interpreter-findings.md`
(knx-iot-point-api-stack).** That file is the source of truth, not this memory.
Read it when resuming work on the interpreter.

Created 2026-07-26 from an adversarial audit run *because* the suite was fully
green — the user's framing: *"we dont have fixes just for the sake of passing
tests, rather have solid mechanisms to execute the conformance tests from the
xml"*. **Update an entry's status in the same commit that fixes it** (the user
asked for this explicitly, so progress survives across sessions).

**HARD RULE from the user: every finding gets documented, always.** Record it in
the tracker before or alongside any fix — never fix silently, never leave a
finding only in a chat reply.

**All four protocol layers have now been audited** (2026-07-26). Series:
**F** interpreter (F1-F14) · **T** CoAP transport (T1-T10) · **O** OSCORE (O1-O5)
· **S** SPAKE2+ (S1-S2) · **M** mDNS/DNS-SD (M1-M20). Status per entry:
OPEN / FIXED / ACCEPTED / WONTFIX.

**Verify every batch with `tools/run-in-docker.ps1 -IncludeUncommitted`** — a
host-native `pytest` run is silently meaningless, see
[[feedback-eitt-always-run-in-docker]]. This cost three 33-minute runs in
batch 4, nearly caused a correct fix to be reverted, and masked the real F14
bug behind 22 fake failures.

**FIXED so far** — batch 1 committed as `0901ba60`, batch 2 (T6, T10, O2, O4,
O5-wontfix, M2, M6, M20) committed as **`fa9d932e`** on
`feature/runtime_tests_v2`, 2026-07-27: F1 response
code · **F12 response TYPE** · F2 vacuous `all:` · F3 unanchored regex ·
T1 malformed-datagram crash · T2 CON-ACK-before-token-check · T4 token/MID
correlation · T8 stale buffered telegrams · T9 `recvfrom` truncation ·
**T10 timeout-vs-discarded** · **T6 offset clamp** ·
M1 truncated-AAAA crash · **M3 + M4 stale mDNS cache** · **M6 fallback
`await_live`** · **O1 nonce reuse** · **O2 sender-ID length guard** ·
**O3-partial (narrowed `except Exception` to `InvalidTag`)** ·
**O4 empty id_context**. Suite stayed **177 passed / 8 skipped /
2 xfailed** throughout — the counts never moved, the assertions just became real.
(Two independent full runs confirmed that count after M6+O4.)

**O5 is WONTFIX in the client — decided 2026-07-27, don't re-propose.** A
receive-side replay window would REJECT telegrams and that is the wrong verb
here: `unprotect_request` has one caller (the multicast `_decrypt_group_telegram`),
the DUT is the only other node on `veth-test` so no injector exists, the suite
DELIBERATELY replays PIVs (138 `SeqNoOffset` steps — same collision that broke 13
TestCases in O1), and dropping a genuine replay hides it behind a "no telegram"
timeout. Moved to the packet-capture plan as free *detection*: partition the
`(kid, piv)` set check 1 already builds by direction — tool→DUT repeats are O1,
DUT→tool repeats are O5.

**M20 is a NEW finding (2026-07-27), found while verifying M2** and confirmed
pre-existing at `0901ba60` by stashing — `MdnsListener._listen` built its socket
on the line BEFORE `try:`, so an `OSError` from `socket()` escaped the handler:
`_error` stayed None (records() returned an innocent `[]`), `_ready` was never
set (every construction burned the full 2.0s wait), and `finally` touched an
unbound `sock`. Fixed by `sock = None` + construct inside the try + guard the
finally. Constructor now returns in 0.15s instead of ~2.15s on failure.
**Lesson: when a fix's correctness depends on "the other half still behaves",
test that half too — that is the only reason M20 surfaced.**

**T6 fix detail worth not re-deriving:** the two `max(0,...)` sites needed
DIFFERENT treatment. `seq_no_offset` raises when `consumed+offset < 0` (the
distance from the counter is the assertion); `seq_no_override` raises on any
negative pin (it is the template's `SeqNo=` sent verbatim, so a negative means a
misparse). **PIV 0 is legal** — `-1` at ssn 1 and `-32` at ssn 32 must NOT raise;
verified. At ssn 65 the six real offsets stay distinct (64/33/30/25/1/96) with
the counter advancing by exactly one, which is the O1 invariant.

F1 + F12 together now make BOTH halves of the evaluation EITT treats as one:
*"Message code differs... Message type differs, planned: NON, received: ACK"*
(`traces/5_4_1.xml`). That pairing matters most on the OSCORE path, where the
OUTER code is always 2.04 and the type is nearly the only other outer-layer fact
left to assert.

**O1 (AEAD nonce reuse) is also FIXED** — `OscoreContext._reserve_ssn()` advances
the counter past a pinned PIV so no later ordinary send can re-emit it. Two
things that are easy to get wrong and cost a full run each:

- **Reserve, NOT clamp**: the transmitted PIV must stay exactly as asked, since
  the negative tests assert on the precise wire value (5.3.18.1 pins
  100000/100001).
- **Apply it to `seq_no_override` ONLY, never `seq_no_offset`.** I applied it to
  both and broke **13 TestCases** across 5.3.17-5.3.20 with
  `planned: 2.04, received: 4.01`. `SeqNoOffset` probes the device's replay
  WINDOW: it transmits a shifted value while the counter advances by exactly
  one, so the test can jump and then continue from where the counter really was.
  Reserving pushed the counter to the shifted value (ssn 5, +31 -> counter 37
  instead of 6) and every later send landed outside the window, cascading
  through AUTH PREPARATION.

**M3 is FIXED — and NO KNOWN FALSE PASSES REMAIN.** `run()` now clears
`_dns_records` + `_dns_query_name` at the TestCase boundary (outermost call only;
`run()` recurses for `@#` preparations and clearing on the inner call would
discard an answer the enclosing case legitimately cached). Reachability was
confirmed exactly: only 5.1.2.5a/5.1.2.5b open with an OUT DnsMessage, and
**5.1.2.4 runs immediately before them querying the SAME `_pm._sub` service**, so
5.1.2.5a could match out of 5.1.2.4's stale answer. Fixes M4 too.

**M5 was the other false-pass candidate and is DISPROVED — do not re-propose
it.** The claim was that a goodbye (TTL=0) must not satisfy a step asserting a
record exists. Implemented, and it FAILED 5.1.2.5a: that TestCase uses the same
step shape for both directions of a state change — Step 1 sets `pm=false` and
then expects a `_pm._sub` PTR, where the **goodbye IS the correct answer** — and
Step 2 sets `pm=true` and expects the announce. Decisive evidence:
`traces/5_1.xml` has `TTL="0"` on EVERY DnsMessage row including the ones EITT
evaluated OK, so EITT does not discriminate on TTL at all; the attribute is a
display default, not a wire value. Reverted. **M6** (`await_live` missing on the
fallback listener) survived as the real, capture-layer half — now FIXED in
`fa9d932e`.

## Batch 5 = `a7382ba4` (2026-07-29) — ⚠ NOT DOCKER-VERIFIED

Found by running real sequences against a LIVE virtual device rather than the
Docker DUT, so a different set of defects surfaced. **`a7382ba4` touches
`conftest.py`, `runner.py`, `coap_client.py` and `hooks.py` — all shared with
the 177-case suite — and F15 CHANGES WHAT GOES ON THE WIRE.** Nine TestCases
that previously emitted no discovery traffic now send six telegrams each, and
5.1.1.x is *about* discovery. **Run Docker before trusting any of it or
building further.**

- **F15 FIXED** — `@@[pahdiscover` was a **silent no-op**:
  `_apply_pahdiscover` calls `self.on_discover()`, a callback NOTHING ever
  assigned (not the GUI, not `test_eitt.py`). 10 uses / 9 TestCases emitting
  nothing, where `traces/5_2.xml` shows 33 discovery telegrams. Found only
  because the user asked why an exported trace had no discovery output.
- **F16 OPEN** — `AckChkTimeOut` is declared on **0 of 1457 active OUT rows**,
  so every one falls back to the client's hardcoded 5 s. Fine on loopback;
  against slow hardware it reports a telegram MISSING that simply had not
  arrived. NOT the same as `TimeToNext=500 ms` — that is certification-accurate
  pacing from the V40 variable-values JSON and must stay.
- **T11 FIXED** — Windows queues an ICMP unreachable against the SOCKET and
  delivers it on the next `recvfrom()` as WinError 10054, aborting an
  UNRELATED request (this client shares one socket by design). Also: ACKing a
  CON to `addr` unconditionally hit WinError 10049 on some sources.
- **`eval_ok` was never set** — the field existed on `Telegram` from phase 0
  and nothing populated it, so every verdict read "0 telegrams failed" over a
  screen of 4.03s. Ordering trap: a telegram is emitted when it ARRIVES but
  judged by the step that FOLLOWS, so read verdicts at the end, not on arrival.
- **`content_format` int vs name** — `CoapResponse.content_format` is an int on
  a parsed response, a name on a scripted one; `_decode_body` assumed a string
  and raised `AttributeError: 'int' object has no attribute 'lower'` — but ONLY
  with a sink attached, which is why the headless suite never saw it.
- **Discovery returned `::`** — the unspecified address, unusable as a
  destination, when a wildcard-bound device answered from it. Now substitutes
  loopback. Separately, naming an interface pinned the scope, so an explicit
  `DEVICE_IFACE` tried only scope 5 (the one that fails on Windows) and gave up.
- **SWU hooks used a cwd-relative path** — `PREP_UPDATE_FILE_PATH` is relative
  to the REPO ROOT; anything running from `tests/runtime/` got
  `FileNotFoundError` then a confusing `KeyError: 'swu_cursor'`.

**Open set as of `b8b71aef` — 14 findings:** F6, F9, M7, M8, M9, M11-M19. Also
still partial: F8 (`content_match` `.*` only), T3, O3. SPAKE2+ fully closed;
F7/F10/F11/F13/F14 closed in batch 4. **Batch 5 adds F16 → 15 open.**
**T5 and T7 were DUPLICATES filed twice** (T5 = O1, T7 = O5) whose headings said
OPEN long after the originals were resolved — corrected 2026-07-27.
**Always re-derive this list, never quote it from memory:**
`grep -E "^## [FTOSM][0-9]+.*OPEN" tests/docs/eitt-interpreter-findings.md`

**Batch 4 = `b8b71aef`** (F7, F8-partial, F10, F11, F13, F14), verified
177/8/2 in 521.93s **in Docker**. Two NEW findings surfaced during the work:
- **F13** — two receive paths passed the RAW `TimeToNext` to
  `_parse_time_to_next()`, which returns 0.0 for a `#VAR` token, collapsing
  `a or b or 5.0` to the 5s fallback. TimeToNext is almost always a variable
  (1489/71/10 uses), so `#RESTART_TIME_TO_NEXT` gave the group listener 5s
  instead of 20s — precisely 5.4.1.9's post-restart init read, which the
  adjacent comment says the code exists to honour.
- **F14** — `_run_manual_prompt`'s observation-only branch passes a `Comment`
  to `_apply_wait`, which reads `msg.wait`; Comments have no such attribute.
  Latent before F7 (both pre-existing observation-only prompts are
  `Activate=0`); F7 routed the first ACTIVE one there and it crashed.

**F7 trap worth not re-deriving:** mapping 5.4.1.8's "Trigger sensor during…"
prompt onto `POST /test/trigger` (as `_trigger_sensor` does for 5.4.1.15) is
WRONG. `traces/5_4_1.xml` records ONE empty comment and NO telegram between
`join=true` and `Set LSM to 'loaded'` — the TestCase asserts the device does
NOT transmit in loading state, so triggering tests the opposite. Both 5.4.1.8
prompts are acknowledge-only.

**F8 scoping lesson:** a blanket "unknown `@`-command raises" would have failed
**2481** legitimate display-only comments. It had to be a whitelist
(`^(@\[[a-zA-Z]|@!)`). Measure the blast radius before making a fail-open path
fail closed.

**Batch 3 = `94bb8080`** (S1, S2, F5), verified 177/8/2:
- **S1**: validate peer points the way the device does — parse-to-reject BEFORE
  arithmetic (`spake2plus.c` does exactly this with `shareP`). Needed TWO
  guards: `_validate_peer_point` up front, AND `_p256_point_add` raising on
  `(None, None)`. The infinity case (`shareV == w0*N`) crashes INSIDE the
  helper, so a caller-side `if base is None` is dead code — I wrote that first
  and had to move it. Always re-check that valid points still pass.
- **F5 measurement trap, do not repeat:** a raw grep of the template for
  `#([A-Za-z0-9_]+)` reports hundreds of "unresolved variables" — they are XML
  character references (`&#10;`=LF, `&#13;`=CR, `&#9;`=TAB) plus errata numbers
  `#42`/`#50`, all decoded by the parser BEFORE `substitute` runs. Fields are
  also collection-scoped, so testing every token against every collection is
  meaningless. Measured properly on the PARSED model per collection: **zero
  unresolved across 366 `#`-containing strings.**

**The remaining ones to act on first:**

1. **F6 `Secure`** — 897 uses / 82 TestCases, never asserted. **Attempted and
   REVERTED — read the entry before retrying.** The obvious model (compare
   against whether OUR REQUEST went out protected) is WRONG and fails AUTH
   PREPARATION immediately. `Secure` on an OUT row asserts whether the
   **RESPONSE** arrived protected. The two agree on 419 of 427 rows; all 8
   conflicts are NEGATIVE tests (5.3.18.1's corrupted auth tag, 5.2.2.1/5.2.2.3
   after reset/restart) where the device CANNOT decrypt our protected request and
   answers in the clear. Correct fix: have `_recv_oscore_response` report whether
   the reply actually decrypted. Also: `SecContext` on an OUT row names the
   context the response ESTABLISHES (the SPAKE 2.04 has `ctx='Temporary Key'`
   with `Secure="no"`), not one it is protected under.
2. **O3** — `unprotect_response` tries BOTH key constructions and accepts
   either, so a device-side regression would stay green. Same shape as the
   original F1 defect.
   Both F6 and O3's remaining half are **blocked on the packet capture** — each
   needs to know what actually arrived on the wire, which is the one thing every
   in-process check cannot tell us.
3. ~~**M6**~~ — FIXED 2026-07-27. The fallback `MdnsListener` now passes
   `await_live=True`, matching the armed one; both wait for the same unsolicited
   announce. Read loop confirmed deadline-bounded first, so a goodbye-only case
   times out rather than hangs. Note `mdns_client.query()` has the same gap but
   **no callers** (dead code) — left alone deliberately, do not "fix" it blind.

**Two decisions recorded 2026-07-26, don't re-litigate without new evidence:**

- **NO LIBRARIES.** CoAP/OSCORE stay hand-rolled because **175 active steps
  require deliberately non-conformant behaviour** (`SeqNoOffset` x138, `Echo`
  x15, `SeqNoFn` x14, `InvalidAuthTag` x4, `RxKid`/`RndKid` x3, `CritOptNo` x1)
  — preventing exactly that is a conformance library's job. I first recommended
  `zeroconf` for mDNS and then **withdrew it**: 16 of 41 `DnsMessage` steps
  target `_sub` subtypes, and zeroconf NORMALISES a subtype away
  (`_pm._sub._knx._udp.local.` -> `_knx._udp.local.`) because it returns resolved
  *services*, while 5.1 asserts on raw PTR records, on dynamic subtypes that
  change mid-run (`_ia<iid>-<ia>`), and on ttl=0 goodbyes zeroconf consumes as
  cache evictions. Narrow option still open: zeroconf's `DNSIncoming` as a
  wire parser only.
- **Packet capture as an independent oracle — PLANNED, see the repo at
  `tests/docs/eitt-packet-capture-plan.md`.** The user owns the CI image and
  asked for a plan doc to tackle later. Key point: every check today runs
  THROUGH the code under audit and there are ZERO unit tests for 3,311 lines of
  protocol code, so a wrong parser validates itself. `veth-test` is the only
  link between tool and DUT = 100% of traffic, zero noise. Would settle O1 by
  measurement instead of argument.

**Why a green suite coexists with this many findings** (the user asked twice —
answer precisely, don't reassure): only ONE finding changed a result when fixed
(F1, which surfaced a real accepted device deviation). The rest split into
(a) unreachable with this template+DUT — measured, e.g. 5 of O1's 6 uses pin PIVs
of 100000/100001 while the biggest TestCase sends 145 requests, so they cannot
collide; (b) fail-safe, i.e. they crash or false-FAIL rather than false-pass
(T1, T2, M1); and (c) genuine latent false passes — M3, M5, M12 — which is the
group that actually justifies the audit. **Positive-path tests were policed
indirectly**: 65% of the 1169 success rows also assert a BODY, and the suite is
stateful so a failure cascades. **The 176 error rows had NO such protection** —
all have empty/wildcard Content, so the code was their only possible assertion,
and a timed-out request (payload -> b"") satisfied them. A dead device passed all
176 negative tests.

**Good news worth not re-deriving:** SPAKE2+ **does** verify `confirmV` in
constant time (`hmac.compare_digest`) and its transcript is correctly ordered and
fully length-prefixed — the classic SPAKE2+ bug is absent. The OSCORE AAD/nonce
KNX deviations were checked byte-for-byte against the device's C in `security/`
(NOT `api/`) and are correct — do not "fix" them.

**F1 (critical) is FIXED**: the CoAP RESPONSE CODE was never compared against
the XML's expected `Code`. 176 of 1457 active `OUT` steps expect a 4.xx/5.xx and
ALL of them have empty/wildcard `<Content>` — so they asserted nothing, and a
timed-out request (payload `None` -> `b""`) satisfied them too. **A dead device
passed all 176 negative tests.** Also added: the 36 rows listing SEVERAL
acceptable codes (`"4.00,4.04"`) had no parser at all — relevant to the
certification body's "4.03 Forbidden is accepted" deviations.

Still OPEN in the F series (F2/F3/F5/F7/F10/F11 are now FIXED; re-derive with
the grep above rather than trusting this list):

- **F4** `opt:` returns True without evaluating (35 lines) — ACCEPTED for now.
- **F6** parsed-but-ignored attributes: `Secure` (897 uses / 82 TestCases!),
  `SeqNumMethod`, `RxKid`/`CheckRxKid`, `Cycles`, `Observe`.
- **F8 (rest)** `content_match` skips a standalone `.*` position-independently,
  so `/value: 1` + `.*` silently loses the second line.
- **F9** malformed attribute values fail OPEN. **One instance is LIVE**
  (measured 2026-07-27): three ACTIVE messages carry `AcceptedCF="a"`/`"r"` —
  corrupt fragments of `application/...` — so the Accept option is silently
  dropped (5.3.1.2b item 12; 5.5.4.1 items 12 and 15). The reference traces
  contain no such values. Deliberately NOT fixed: the corruption is in
  template data that must not be edited, and a hard raise would fail three
  passing tests. The likely right shape is a loud warning plus an
  `overrides.yaml` correction, not a raise. Everything else measured clean
  (159 `_safe_int` values, 1 `CritOpt`, 1517 of 1520 CF values).

**Verified CLEAN — do not "fix":** no hardcoded section/case/GUID knowledge in
engine code (two independent audits); `overrides.yaml` is legitimate operator
configuration and is hard-error policed; `match_content` on an empty
`<Content>` asserts an EMPTY BODY rather than skipping; `Type` (CON/NON) is
genuinely honoured.

**To prove an assertion really bites, never edit the master template** (see
[[feedback-never-touch-eitt-master-template]]). `test_eitt.py` now honours
`RUNTIME_TEST_EITT_XML` — point it at a mutated scratch copy instead.

Status of the suite itself: [[project-eitt-5-3-status]].
