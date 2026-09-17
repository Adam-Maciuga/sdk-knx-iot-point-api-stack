---
name: feedback-check-reference-libs-then-knx-deviations
description: "When fixing tests/common/transport/, check what aiocoap / zeroconf / the reference OSCORE lib does FIRST — then check knx-protocol-deviations.md for where KNX deliberately differs."
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 4799ccc5-4c9c-4bdd-98d6-09eef940b849
  modified: 2026-07-26T19:54:28.787Z
---

**Workflow the user asked for (2026-07-26) when touching
`tests/common/transport/` in knx-iot-point-api-stack:**

1. **Check what the reference library does.** For `coap_client.py` that is
   **aiocoap**; for `knx_oscore.py` aiocoap's OSCORE module; for
   `mdns_client.py` **python-zeroconf**. They are mature and usually right about
   framing, option encoding, and message semantics.
2. **Then check `tests/docs/knx-protocol-deviations.md`** — the deviation
   reference, written so this comparison does not have to be re-derived every
   time. If the area is listed there, KNX deliberately differs and the library
   must NOT be copied 1:1.
3. **If the device's C and the library disagree, the C wins**, and the reason
   gets added to that document. Authority lives in `messaging/coap/`,
   `security/`, `port/dns-sd_mdns.c` — NOT in the RFCs and not in recollection.

**Why:** KNX-IoT is close to the base protocols but deviates in load-bearing
places — the fixed POST/2.04 outer codes on protected messages, the 7-byte
sender-ID cap, AAD always reusing the request's kid+PIV, the empty-sender-ID
group key, the 10-byte kid_context echo marker, empty-vs-absent ID context,
SPAKE2+'s mbedTLS-compatible `w0` encoding, and the `_sub` subtype scheme.
Copying a correct library into any of those breaks conformance. **This is also
why we cannot simply adopt the libraries** — see
[[reference-eitt-interpreter-findings]] for the measurements behind that
decision (175 template steps require deliberately non-conformant behaviour).

**Also in that doc:** a rule that fixes in this layer must be verified with the
FULL suite, not in isolation — two fixes in one session passed unit-level checks
and still broke things (F12 hit a legitimate separate-response; O1 broke 13
TestCases by reserving the SSN on `seq_no_offset`).
