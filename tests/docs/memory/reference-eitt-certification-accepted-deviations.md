---
name: reference-eitt-certification-accepted-deviations
description: "The official EITT 1.1.0 certification setup + per-section accepted deviations (telegram enable/disable, 4.00-vs-4.03, default-value corrections). Check BEFORE calling any EITT failure a bug."
metadata: 
  node_type: memory
  type: reference
  originSessionId: 4d0fc175-3a82-427c-bd95-25153f25e4ad
  modified: 2026-07-26T01:35:32.554Z
---

Source: Confluence "1.1.0 EITT Certification – Mandatory Setup & Configuration"
by Andre Hänel (updated 17 Jul 2026), pasted by the user 2026-07-25.
**BDUT (stack) v1.1.0 PASSES EITT certification with the deviations below** —
so any of these appearing as a failure in our interpreter is an EXPECTED
deviation, not a device bug. Check this list before diagnosing.

## Environment

- EITT v4.4.0096 (0103 also OK); project `PointAPI_KNX_IoT_Stack_Tests_1_1.eittproj` (v40)
- `EittConformanceTestVariableValues.json`; `FWUUploadDummySmall.bin` (for 5.2)
- BDUT executable `eitt_virtual_gui.exe` — Windows GUI with restart/reset/`test` button
  (the `test` button is what our `overrides.yaml` `action: trigger` substitutes)
- Stack v1.1.0 is NOT the API version from `/.well-known/knx`

## 5.2

- Upload first AND second half must be present; FWU file path set via config variables
- **Disable telegram 253, enable 254**; **disable 282, enable 283** — BDUT supports
  date/time instead of `:OSV`
- Default corrections: **256** EITT says 1, **0 is also correct** (BDUT: "Unicast CoAP
  with additional OSCORE support"); **258** EITT says 2, **1 is also correct**
  (BDUT: "Push Only")

## 5.3

- **Telegrams 65, 67, 150, 152, 158**: test asks "Unauthorized", **"Forbidden" (4.03)
  is correct and accepted** per Tests Spec / 2025.12.12 KSG Class "IoT" meeting
- **5.3.14.1 ignored** — replay window cannot be set (optional per spec).
  Already our documented skip.
- **Disable telegram 49, enable 50** — conclusion #20, "Open Points from Voting
  July 2024 v1.2.1"

## 5.4

- **Telegram 32**: test asks "Unauthorized", **"Forbidden" (4.03) is accepted**
  (same KSG meeting). Confirmed independently in `data/traces/5_4_1.xml`: TelNo 32
  in **5.4.1.1.b** records `EvalResult="Message code differs, planned: 4.00,
  received: 4.03 Message type differs, planned: NON, received: ACK"` and the run
  was still accepted.

## 5.5

- **Reset network ports** (GUI or serial console) before 5.5 when continuing straight
  from 5.4 — EITT bug under investigation where it sends an OSCORE-encrypted package
  when it shouldn't.
- **Does NOT apply to our interpreter** (verified 2026-07-26): that is an EITT *tool*
  bug, and every one of our runs starts a fresh server. 5.5 passes 35/35 with no
  override. Every `EvalResult` in `traces/5_5.xml` is `OK` — no real deviations here.

## 5.6 / 5.7 / 5.8

- **Nothing to accept.** 7/7, 2/2 and 5/5 pass; every `EvalResult` in
  `traces/5_6.xml`, `traces/5_7.xml` and `traces/5_8.xml` is `OK`, and 5.7/5.8
  have zero TelNo gaps (so nothing was deactivated in the cert run either).
- 5.8 DID need a device-side fix first, but that was a gap in our own headless
  mock (`runtime_test_server.c` didn't answer the `?m=` metadata query the way
  the certified `eitt_virtual_gui` does) — **not** an accepted deviation. See
  [[project-eitt-5-3-status]].

## 5.10

- Pagination IS supported → enable/disable the pagination telegrams accordingly
- **Telegrams 108–113** expect MULTIPLE IPv6 addresses on the BDUT. With one address
  (the common setup) these **fail, and that is expected and accepted** — agreed
  25 Sept 2025: a HW must have at least ONE IPv6 address, not necessarily more.

**APPLIED 2026-07-26 — both items are now handled in `eitt/overrides.yaml`:**

- "Pagination IS supported" = the `message_activation:` block, 32 entries
  (16 disable / 16 enable). Our template ships the **opposite** (the
  "NOT supported" 4.00 branches active). Full detail in
  [[project-eitt-5-3-status]] — including the trap that the guidance is spelled out
  in the template's own `Comment` elements, in the **`Text=` attribute**.
- The multi-IPv6 item = the 5.10.1.2 xfail. Result: **7 passed + 2 xfailed**.
- The SECOND 5.10 xfail (5.10.1.4, `</p>;total=5` vs 9) is **NOT** on this list and
  is **not** a certification deviation — it is our own test scaffolding
  (`/test/*` control endpoints) inflating the `GET /p` count.

**How to apply here:** a "Forbidden is accepted" item becomes an
`eitt/overrides.yaml` entry (or a content match accepting both codes) rather than a
chased failure. Status: [[project-eitt-5-3-status]].

**This list does NOT replace the `Activate` check** in
[[feedback-eitt-check-template-and-trace]] — they answer different questions:

- The `Activate` rule is structural: *"is this step live at all?"* Our template ships
  with **97 deactivated CoAPMessages** (5.10: 27, 5.2: 24, 5.1: 17, 5.5: 16, 5.3: 7,
  5.4: 2, …) that this list never mentions — they are simply how the template is
  authored. Ignoring that is how you end up inventing requirements from dead XML
  (5.3.1.4b/c have ZERO messages; 5.4.1.4's items 52/53 are `Activate="no"`).
- This list is about operator EDITS that may or may not already be applied.

## The telegram NUMBERS in this list will never match ours — don't try to map them

**RESOLVED 2026-07-26 (user confirmed + verified).** Do not spend time again
trying to map "disable telegram 253" to an element in our XML.

EITT numbers telegrams **per generated project**, sequentially over the ACTIVE
steps only. The numbering therefore depends on which TestCases were generated and
which steps are enabled — so a differently-generated project numbers differently,
and ours will essentially never agree with the certification body's.

Verified on `data/traces/5_4_1.xml`: `TelNo` runs **1 → 325** with exactly two
gaps, 99 and 100 — precisely where 5.4.1.9's `Activate="no"` discovery pair sits.
That is the mechanism in one observation: sequential over the run, and deactivated
steps still consume numbers.

**What to do instead:** extrapolate WHICH test/telegram is meant from the section
plus surrounding context, e.g.
- 5.2 "253/254, 282/283" → the `:OSV`-vs-date/time pair
- 5.3 "49/50" → the voting-conclusion-#20 pair
- 5.10 "108–113" → the multi-IPv6 pagination cases (expected to fail on a
  single-address device, and that failure is accepted)

**Corollary for any tooling** (see [[project-eitt-gui-plan]]): a telegram number
is DISPLAY ONLY, generated at load time. Anything durable — overrides, bookmarks,
the `manual_actions` map — must key on `TestCase.qualified_name` plus the
message's XML `ID` (a GUID), which survive regeneration. `overrides.yaml` already
does this correctly.
