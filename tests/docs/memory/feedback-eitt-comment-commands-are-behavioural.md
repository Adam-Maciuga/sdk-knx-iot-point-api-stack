---
name: feedback-eitt-comment-commands-are-behavioural
description: "EITT's @/@@[ comment commands are instructions to the tool, not narration — never treat one as a no-op just because it looks cosmetic."
metadata:
  node_type: memory
  type: feedback
  originSessionId: 0efacc70-def0-424f-a2b4-2abab6ff9f7a
  modified: 2026-07-25T15:15:32.676Z
---

User, 2026-07-25: *"never treat those @ commands as a noop. sometimes they are but they also contain very important commands to the testing."*

**Why:** parsing a command and then not acting on it fails silently and looks like a device bug. Two caught this way:

- **`@@[MDNS"rqcheck=on|off"`** — EITT's own grammar spells it `@@[pahmdns` and defines it as *"Check if the answered service is equal to the requested service"*. With the check ON an mDNS answer only counts when its owner name equals the QUERIED name; 5.1.2.x turns it OFF precisely because it queries `_knx._udp.local` and then matches `_00fa10020800._sub._knx._udp.local` out of that same answer. We had it recorded and ignored.
- **`@[w` vs `@@[w`** — NOT the same command. `@[w` is a toolbar countdown, a real unattended delay. `@@[w` is a MODAL the operator dismisses after a couple of seconds, so its declared time is a ceiling, not a delay. `@@[w` was being swallowed by the generic `@@[` provisioning branch, which is why 5.2.9.1 read `/swu/state` while the device was still UPGRADING.

**How to apply:** when a command appears in a failing sequence, look it up in `tests/eitt/data/EITT_KnxCommentCommandsScheme.xml` (EITT's own grammar, authoritative) before assuming it is narration. Only `@[t...` prefixes are genuinely narrative text. Audit what is parsed-but-unconsumed by grepping the flag name across `tests/eitt/interpreter/` — that is how `@@[pahsnc`'s `seq_no_check_enabled` (21 uses, still unconsumed) was found.

Related: [[project-eitt-5-3-status]], [[feedback-eitt-check-template-and-trace]].
