---
name: project-eitt-5-3-status
description: "Current state of the EITT dynamic-interpreter conformance suite — per-section status, layout, and what's next. Read at the start of any session continuing this work."
metadata:
  node_type: memory
  type: project
  originSessionId: 0efacc70-def0-424f-a2b4-2abab6ff9f7a
  modified: 2026-07-26T10:16:18.333Z
---

**CURRENT STATUS (2026-09-17)** — supersedes the 2026-07-26 numbers below, which are kept as
history. Layout since 2026-09-14: `tests/eitt/` (test_eitt.py, `interpreter/` package, data/,
harness_tests/), `tests/runtime/`, `tests/common/` — see [[project-runtime-test-tiers]] for
everything that moved. Run it in Docker only: `tests/common/tools/run-in-docker.ps1 -Tier eitt
[-Section 5.x] [-IncludeUncommitted]` (never host-native pytest, see
[[feedback-eitt-always-run-in-docker]]).

Full tier, Docker and GitLab CI (`eitt-gcc`): **177 passed / 0 failed / 8 skipped / 2 xfailed**,
~13 min. Skips = the seven 5.4.2 cases (Observe not implemented in the interpreter's request path;
5.4.2.2 also has a template flaw, read provoked where a write is expected) + 5.3.14.1 (optional
replwdo). Xfails = the 5.10.1.2 / 5.10.1.4 multi-IPv6 pagination deviation. 5.1.1.3e passes since the
client reassembles multicast Block2 answers over unicast (F-021 closed as a test-client gap).
Interpreter facts that matter when resuming: `IP="BDUT-Multi"` rows go out as real OSCORE NON
multicast to `[ff35:30:fd11:9988:7766:0:8000:1]:5683` (T13; the alias comes from the template's
"NEW PROJECT SETUP", applied per collection runner by `test_eitt.py::_seed_project_setup`); the
device's 4.01 Echo challenge for a group write is decrypted with a sibling context derived from the
option's random kid context and kid (`OscoreContext.with_id_context`); the interpreter fails closed
on unevaluated attributes and malformed XML values (F6/F9) and corrects template data corruption via
`overrides.yaml` `message_attributes:`; the tracker `tests/docs/eitt-interpreter-findings.md` has no
OPEN item, only three PARTIALLY FIXED (F8 `.*` matcher line, T3 listener last-segment compare, O3
both key constructions accepted).

---

The EITT XML dynamic interpreter (see [[eitt-xml-interpreter-project]]) lives on branch `feature/runtime_tests_v2` in `d:\Repos\knx-iot-point-api-stack`. **Do NOT commit or push unless asked** — the user reviews first.

**SUPERSEDED 2026-09-10 — see [[project-runtime-test-tiers]].** The hand-written `test_5_*.py` suite was deleted by commit `3a5e2d4a`, which the user later called a mistake; upstream's 349 hand-written tests are restored (verbatim, not yet importable) and will become the "runtime" tier next to this interpreter tier. `tests/eitt/test_eitt.py` still generates one pytest item per EITT TestCase from the template at collection time.

**Layout** (the eitt and runtime tiers became siblings on 2026-09-14; the
shared infrastructure is its own package, `tests/common/`, which holds no
tests):

```
tests/
  pytest.ini      pythonpath = . common eitt ; testpaths = eitt runtime
  conftest.py     thin: re-exports everything common/fixtures.py defines
  common/
    fixtures.py   the fixtures, constants, helpers and pytest hooks
    transport/    coap_client, knx_oscore, knx_spake2plus, mdns_client
    tools/        run-in-docker.ps1, run-local-ci*.sh, spake utils
    runtime_test_server.c ; requirements.txt
  eitt/
    test_eitt.py    (187 items, needs a DUT)
    interpreter/    the interpreter + overrides.yaml (package RENAMED from
                    `eitt` so it does not shadow the directory it lives in)
    gui/            the PySide6 runner front-end
    data/           template, EITT key maps + command scheme, variable-values
                    JSON, FWUUploadDummySmall.bin, traces/ (9 reference runs)
    harness_tests/  test_events.py, test_comment_scheme.py,
                    test_project_copy.py, test_gui_*.py  (194 items, no DUT)
  runtime/          behaviour-named test_*.py  (245 items, needs a DUT)
  unit/  fuzz/
```

Run: `.\tests\common\tools\run-in-docker.ps1 -IncludeUncommitted -PytestArgs "-q","--tb=line","-k","Security::"`. Useful switches: `-ServerLogs -OscoreDebug -CoapDebug -GroupDebug` (GroupDebug also enables `[mdns]`), `-TimeScale`, `-NoCache`. Pick one tier with `-Tier eitt|runtime|harness` (default `all`).

**Status (verified 2026-07-26, FULL SUITE, no filter): 177 passed, 8 skipped,
2 xfailed, 0 FAILED — exit 0, 8m45s.** (Was 178/1/2/6 before the seven 5.4.2
TestCases were skipped; the 6 failures became skips and 5.4.2.2d went
passed -> skipped, hence 178 -> 177 and 1 -> 8.)
Per section: 5.1 = 42/42, 5.2 = 19/19, 5.3 = 44/45 + 1 documented skip (5.3.14.1,
optional `replwdo` — no PUT handler on the device; also absent from the trace),
**5.4.1 = 16/16**, **5.5 = 35/35**, **5.6 = 7/7**, **5.7 = 2/2**, **5.8 = 5/5**,
**5.10 = 7 passed + 2 xfailed**. 5.9 does not exist as a section in this template.

**All 7 of 5.4.2 are now `action: skip` in `overrides.yaml`** ("GET & OBSERVE
/.knx", template-labelled *Preliminary - Not for Certification*). 6 of them
(5.4.2.1, .2.2, .2.2b, .2.2c, .2.2e, .2.3) were failing "response body could not be
decoded"; out of scope, since /.knx Observe is not implemented in the stack yet.
NOT a regression — they only became visible on the first UNFILTERED full-suite run.
Confirmed harmless to the `<Cbor>` fix below: 5.4.2 contains zero `<Cbor>` elements.

**`skip`, not `xfail`, and the reason matters:** the 7th case **5.4.2.2d PASSES**
(only 3 active messages, no real Observe). Marking the suite `xfail` would report it
as XPASS — re-adding clutter in a new form — and would still spend ~2 min running
Observe timeouts. Skip states the real situation (section out of scope) and covers
all 7 uniformly. **Remove these once /.knx Observe is implemented.**

**Select a section with `-Section 5.8` / `--section 5.8`, NOT `-k`** (added
2026-07-26, see the `-k` trap note below). Repeatable: `-Section 5.5,5.6`. An
unknown section fails loudly and lists the valid ones. Implemented in
`common/fixtures.py` (`pytest_addoption`), `test_eitt.py` (`_in_section` compares NUMERIC
components of the TestCollection name), and `run-in-docker.ps1`.

**`<Cbor>` payloads were NEVER SENT — real interpreter bug, fixed 2026-07-26.**
`runner._resolve_body` only looked at `msg.content` and bailed out with `return b""`
on a falsy one. A CoAPMessage can instead carry its body as PRE-ENCODED CBOR hex in
a `<Cbor>` ELEMENT, with an EMPTY `<Content>` — so those requests went out with a
**zero-length body** and the DUT silently did nothing. 16 uses template-wide, 14 of
them in 5.10.5.2, which is why it surfaced only there. That is how 5.10.5.2's
`POST /k` group write (`{5:{7:1,6:"w",1:true}, 4:4367}`) failed to set `/p/1` and the
next step read `False` instead of `true`. Fix: check `msg.cbor` BEFORE the
`not msg.content` bail-out and `bytes.fromhex` it.

**5.10 = the section where our template diverges most from the base** — see the
dedicated note below. 7 passed + 2 documented xfails.

**Select a section with `-Section 5.8` / `--section 5.8`, NOT `-k`** (added
2026-07-26, see the `-k` trap note below). Repeatable: `-Section 5.5,5.6`. An
unknown section fails loudly and lists the valid ones. Implemented in
`common/fixtures.py` (`pytest_addoption`), `test_eitt.py` (`_in_section` compares NUMERIC
components of the TestCollection name), and `run-in-docker.ps1`.

**5.7 = 2/2, 5.8 = 5/5** (2026-07-26). 5.7 needed no changes. **5.8 needed a real
fix in `tests/common/runtime_test_server.c`** — 5.8.3.2 ("Read meta data")
failed with `field '/rt' not present in response`.

**The fix came from the demos repo, not from first principles** — the user's
instruction: *"before doing that, compare to eitt_virtual_gui in demos repo. is
this done there aswell?"* It was, and copying it corrected 4 things my own draft
had wrong. `apps/knx_iot_datapoint.c`'s `knx_iot_get_handler()` is the certified
BDUT's version of this handler; `get_int_dp` in our headless server was a much
thinner mock. Port from there, don't reinvent:

1. **ITERATE the query keys** (`oc_init_query_iterator` / `oc_iterate_query`).
   5.8.3.2 sends a REPEATED key, `?m=id&m=value&m=rt&m=if&m=dpt`; a single
   `oc_get_query_value()` only ever sees the first.
2. **Independent `if (... || wildcard)` per field, NOT an else-if chain** — that
   is what makes `?m=*` emit everything.
3. **Length-exact `strncmp(m_value, "..", m_value_len)`** — `"id"` and `"if"` are
   both 2 chars, so a fixed-length compare silently matches the wrong field.
4. **`error_state` -> 4.04** via `oc_prepare_no_format_response_no_payload`.
   This is what the template's last two steps test: `?m=id&m=unknown` still
   returns id, `?m=unknown1&m=unknown2` matches nothing and must 4.04.

`rt` = `oc_string_array_get_item(types, 0) + 7` (skips `urn:knx`, giving
`:dpa.65500.201`); `if` = `add_all_interface_short_urns_for_a_resource()`, an
ARRAY (`/if/0: :if.p`), not a scalar. **`rt` and `if` are absent from EITT's CBOR
key map**, so they go on the wire as TEXT keys — `ResourceSchema.decode()` passes
unknown text keys through unchanged, so **no `resource_schema.py` change was
needed**. Verified no regression: 5.2+5.3+5.5+5.6+5.7+5.8 = 112 passed, 1 skipped.

## 5.10 — our template diverges from the base template MOST here

The user's warning: *"here you have to be very careful with whats activated and
what not. we differ from the base template quite alot"* and *"always check the
comments in the template, it says if pagination supported ..."*.

**Read the `Comment` elements — the answer is written there.** Gotcha: the text is
in the **`Text=` attribute**, NOT `Command=` and not the element body. Querying the
wrong field returns nothing and makes 5.10 look comment-free (it has 242). In
5.10.1.2 every `4.00` is tagged `@[t if pagination is NOT supported` and every
`2.05` `@[t if pagination is supported`.

**Our template ships 5.10 as "pagination NOT supported"; the device DOES support
it.** EITT encodes a device CAPABILITY as alternative response branches after one
request, and the operator activates the matching one. Rule, per the user:
*"for pagination cases we deselect the 4.00 and select the 2.05; for the ipv6 we do
the same."*

Implemented as a NEW `message_activation:` layer in `overrides.yaml`
(`OverrideTable.apply_message_activation`, called from `test_eitt.py` at load;
unmatched keys are a HARD error). **32 entries, 16 disable / 16 enable.** Keyed by
case name + message XML `id` (GUID) — `index:` disambiguates 5.10.1.2's two `auth`
alternatives, which literally SHARE one GUID.

- 15 pairs in 5.10.1.2. NOT flipped, and correct as shipped: every `?pn=0&ps=0`,
  every `?pn=5&ps=5`, and `fp/g?pn=2&ps=2` — those really are 4.00 (page past the
  end / ps=0), confirmed in the trace.
- 1 pair in **5.10.1.1** `auth?pn=0`, which is NOT a pagination flip: both
  alternatives are 2.05 and they differ in how many content types `/auth/at`
  advertises. Trace shows `</auth/at>;if=":if.ll :if.sec";ct=40` — a SINGLE ct — so
  the `ct=60 40` variant the template ships active is wrong. Must agree with the
  `auth?pn=0&ps=5` pick in 5.10.1.2 (`index: 1`).
- `auth/at?pn=0&ps=5` has a THIRD option labelled `Alternative Response` (quoted
  `ct`); the unquoted `E847490F` is right per the trace.

**Two documented xfails in 5.10, both test-environment artifacts, not device bugs:**

1. **5.10.1.2** — `dev/ipv6` `ps=2`/`ps=3` want a 2nd/3rd IPv6 address; our DUT has
   one (the veth ULA). The accepted multi-IPv6 deviation. The cert run hit it from
   the other side: `traces/5_10.xml` records `Cannot find JSON pointer: /2/value`,
   the ONLY non-OK eval in all of 5.10. **The template comment says "select only one
   of the three" — the user overruled that: treat all three like the other
   pagination pairs and let ps=2/ps=3 fail.**
2. **5.10.1.4** — asserts `</p>;total=5`; device answers **9**. `GET /p` counts
   EVERY entry on the application-resource chain (`oc_knx_p.c`), and this build adds
   the four `/test/*` endpoints from `api/oc_test_control.c`. A filter in
   `oc_knx_p.c` (skip `urn:knx:test.ctrl`) was written and **deliberately REVERTED**
   — the user: *"we leave the stack as is and only enable those test endpoints for
   our automated testing"*. Proper fix = keep `/test/*` off the app-resource chain.
   Note `/fp/g`, `/fp/r`, `/fp/p` `l=total` all PASS: they count their own tables.

**Trace-vs-template correlation for 5.10 does NOT work by ID or by position.** The
trace carries no template GUIDs (`ID` is absent, `PlanID` is one run id for all
rows), and positional alignment fails because the trace also contains AUTH/IA/TABLE
PREPARATION that the section itself does not. What DOES work: match on
`Path` + the `URLParameter` children, and read `RawContent` (hex) for the body.
Every trace row is `Active="Y"` — deactivated steps leave NO row, they only consume
a TelNo.

**5.5 + 5.6 both passed on the FIRST run — zero interpreter changes needed**
(2026-07-26). Nothing was added to `overrides.yaml` for either. Verification done
rather than taking the green at face value:

- **5.5** = 35 TestCases / 12 suites (fp/g, fp/r, fp/p), 472 active CoAPMessages
  (236 req + 236 resp); 179 of the responses carry real expected content
  (JSON-pointer asserts, link-format regexes), so the pass is not vacuous. The
  matcher was proven strict by injecting `/ga/0: 1` -> `4242` into a scratch copy
  of the template — it failed with the exact pointer and both values. Template
  restored, `git diff` clean.
- **The 16 `Activate="no"` messages in 5.5 match the cert run exactly**:
  `traces/5_5.xml` runs TelNo 1->488 with exactly **16 gaps**, clustered in
  183–271 where the 5.5.5.x recipient cases sit. Deactivated steps consume numbers
  but aren't sent, so the certification body had the same steps disabled — we are
  not skipping work they exercised.
- Every `EvalResult` in `traces/5_5.xml` and `traces/5_6.xml` is `OK`. **No 5.5/5.6
  entries needed in [[reference-eitt-certification-accepted-deviations]]** — that
  list's 5.5 item ("reset network ports before 5.5 when continuing from 5.4") is an
  EITT *tool* bug and does not apply to us: every run starts a fresh server.
- **5.6** = 7 TestCases / 4 suites (a/lsm, ap, ap/pv), 115 active messages. Its 149
  comments are all display-only `@[t` plus three `@#"Copy of … PREPARATION"`
  references — both kinds already handled, no `@@!` prompts.

**`-k` matches by SUBSTRING — "5.6" also selects 5.5.5.6 and 5.5.6.1** (cost time
2026-07-26). Those two then run WITHOUT their sibling preparation TestCases and
fail with "expected '/path: value' content but response body could not be decoded"
— a fabricated failure, the same trap as
[[feedback-eitt-never-diagnose-single-testcase]] but triggered by an innocent-looking
section filter. Both "5.6 failures" were really orphaned 5.5 cases; confirm by
looking up the failing CoAPMessage `ID` in the template to see which TestCase owns
it. Also note pytest `-k` treats bare spaces as operators, so a suite title like
`5.6. Application Program Object Resource` cannot be passed through the
`run-in-docker.ps1` wrapper. Use `-k "5.5 or 5.6"` (superset, all preparation
present) and read the counts.

**Only ONE override remains** in `overrides.yaml` (the 5.3.14.1 skip). The three
`BLOCKED:timer` SPAKE2+ xfails were REMOVED 2026-07-26 as wrong — 5.3.1.4b/c are
empty placeholders (2 title comments, 0 CoAPMessages) and .4d is only a factory
reset, so none tests handover; the trace recorded 0 messages for b/c too. Proven
harmless: 5.3 stays 44 passed without them.

**5.4.1.4 + 5.4.1.15 are FIXED** (DUT-originated unicast telegrams). Three things were needed, all trace-derived:

1. **DUT in its OWN netns** — `run-in-docker.ps1` uses `ip netns exec dut` via the new `RUNTIME_TEST_SERVER_PREFIX` env var (honoured by conftest.py). The device learns our address from the SOURCE of our discovery reply; with both veth ends in one netns the kernel picks the device's own address, so it sent the telegram to itself. **Distinct prefixes / /128 + host routes do NOT work** (`ip -6 route get` says `dev lo` for both) — don't retry.
2. **No Uri-Path filter before decryption** — a protected request's path is inside the ciphertext; filtering `options[11]` rejected every telegram (3 arrived, all decryptable, all dropped).
3. **Buffer unsolicited requests instead of discarding as stale** — this is what 5.4.1.4 needed. Its telegram has a DIFFERENT mid+token from the preceding ACK and lands 0.1s later (trace TelNo 98 → 101, 22:25:20.6 → .7), so `_recv_oscore_response`'s "wrong token" branch threw it away. It now keeps requests in `CoapClient._pending_requests`, drained by the listener first. This is what EITT gets free from a permanently-open socket.

**Do NOT arm the unicast read in `_prepare_for_request`** — the armed read owns the shared socket and swallows normal traffic: 14 passed → 12 (5.4.1.11 + .15 regress).

**5.4.1.9 is FIXED (2026-07-26).** The device was NEVER at fault — it sends the init read correctly (`Sending s-mode multicast r` to the group address, 0.4s after the restart; trace TelNo 185 confirms). OUR bug: `_prepare_for_request` only armed a group listener when the next step had a NON-EMPTY `SecContext`, and 5.4.1.9's telegram step carries `SecContext=""` while the telegram is protected under `MulticastRx` (which `MCastGrp01` inherits from 5.4.1.3). Fixed by falling back to the joined group's binding, and by honouring the step's own `#RESTART_TIME_TO_NEXT` (20s) instead of a flat 5s. Note an isolated `-k` check reported `ctx=None` and sent me down a wrong path — only the full collection shows the inheritance.

**`@@!` operator prompts are now honoured (2026-07-26).** `@@!` is EITT's "display comment in toolbar, SUSPEND EVALUATION and beep" = blocking modal for a PHYSICAL action; we had been ignoring all 4 uses. Mapped by TEXT via a new ordered `manual_actions:` list in `overrides.yaml` (`@@!` alone does NOT mean restart): `power[- ]?cycle` -> `/test/restart` (preserves config), `local master reset` -> `/test/factory-reset` (+ clears our materialized OSCORE contexts, else the collection collapses 44 -> 11 passed), `check LAST UPDATE value` -> no endpoint (human observation). An UNMAPPED `@@!` now FAILS loudly. This made 5.3.1.3 actually exercise the power cycle and Local Master Reset instead of silently skipping them.

**veth needs a ULA address** (2026-07-25): `KNX_MULTICAST_SCOPE` is 5 on both sides but a veth pair has only link-local `fe80::` (RFC 6724 score 2), and `oc_init_read_next` refuses to send ANY device-originated s-mode until scope >= SITE_LOCAL — 651× "network not (yet)up with correct scope", then silence. `run-in-docker.ps1` now adds `fd11:9988:7766::1/2`; count went to 0. **Check this first when a device-originated telegram never arrives.**

**EITT has NO listen/capture command** — verified zero matches across every `Command="@@[…"` in the scheme file. Its CoAP socket is simply always open, and an `OUT` step means "wait for a matching telegram on it". `pahmcast join=true` is group MEMBERSHIP only. Our client matches this (one socket, `bind(("", 0))`, session-lived), so `coap_client.recv_unicast_telegram()` is just a timed read of it.

**"45" = TestCases, not telegrams.** 5.3 is 15 TestSuites > 45 TestCases > 1269 CoAPMessages. EITT's GUI shows the telegram count.

**Section 5.1** (committed as `8409ac7b`). DnsMessage playback implemented in `runner._run_dns_message`. Model, from `data/traces/5_1.xml` TelNo 121→122: ONE query draws the DUT's WHOLE record set, and each scripted `OUT` step matches out of that single cached answer (trace rows with `TelNo=None` are just captured traffic; only awaited records get a TelNo + `EvalResult=OK`). So it is a rolling record cache, not a listener per query/answer pair. `PatchAddressFn` is now honoured too — 5.1.2.x invalidates the cached address, resolves SRV+AAAA into `mdns_port`/`mdns_ip`, and `patch_address_5_1_2_1` returns `[ip]:port` for the closing GET, which is what proves discovery worked.

**mDNS listener gotcha (cost hours):** joining the multicast group on ONE interface index works until the DUT re-opens its own mDNS socket (a factory reset makes it do that), after which its answers stop arriving on that membership — while the device log still shows `send_query_response`. Join the WILDCARD index 0 as well: 0 records became 100. I wrongly concluded "the device's responder is permanently dead"; the user's `eitt_virtual_gui`-on-Windows counter-example disproved it.

**The 3 former 5.1 failures are FIXED (2026-07-25) — all three were OUR bugs, not device bugs.** The old note here blamed `access denied for: /dev/pm`; that was wrong — it only appears when you run 5.1.2.5a with `-k` in isolation, because AUTH PREPARATION lives in earlier TestCases of the same collection, so `TokenTest` is never provisioned and the request goes out unsecured. **Never diagnose a single EITT TestCase with `-k <one case>`; run the whole collection.**

- **5.1.1.1**: unsecured requests all went through `coap.get()` = unicast to the known DEVICE_HOST. `IP="Multicast <scope>"` now routes via `multicast_get()` (`_plain_multicast_scope` in runner.py). Note the device answers a unicast `/.well-known/core` too, so this would have silently PASSED while never testing multicast.
- **5.1.2.5a/b, trap 1**: `_arm_unsolicited_dns` stopped its scan at the first non-Comment item and always hit the request's own `OUT 2.04`, so it never armed. Shape is `PUT dev/pm` → `OUT 2.04` → `DNS OUT PTR`.
- **5.1.2.5a/b, trap 2**: a state change emits **goodbye (ttl=0) THEN announce**; the listener broke on the first packet and kept only the goodbye. Fixed with `MdnsListener(await_live=True)` — keep reading while all records have `ttl == 0`. Enabling pm goodbyes with `pm=0` (no `_pm._sub`) and announces with `pm=1`.
- 5.1.2.5b's `_ia2dfdc1c3d-4d2` is **hardcoded in the template and correct** (the step writes `ia:1234, iid:12345678909`; those are the hex forms). Not a substitution bug.
- Joining all interfaces was tried and was NOT the fix; narrow `[0, DEVICE_IFACE]` join kept.

**Never treat an `@`/`@@[` comment command as a no-op** — see [[feedback-eitt-comment-commands-are-behavioural]].

**`unprotect_response` is a minefield** — see [[feedback-oscore-unprotect-response-regression]].

**Comment commands are now DONE** (2026-07-25 session) — both remaining items closed, verified 102 passed / 1 skipped / 3 known 5.1 failures:

- `@@[pahsnc` (21 uses) is now consumed by `runner._script_owns_echo_handshake()`, which suppresses `oscore_request`'s auto-Echo-retry. Kept alongside the older "next active OUT is a 4.01" lookahead — neither signal subsumes the other. It means MORE than the Echo handshake: 5.3.17.1/.3 wrap 137/138 requests carrying deliberate `SeqNoOffset`, where the point is "don't resync the SSN behind my back". Those two regions NEVER close (no matching `"on"`), and the engine is shared per collection, so `run()` now re-asserts the default per TestCase.
- `pahctx` `activate=`/`updatesession=`/`destination=` (3 uses) — **ignoring them is CORRECT, not a gap.** They're EITT's device↔context association commands (scheme Pos=10..12), no crypto material, no telegram in `traces/5_4_1.xml`. We resolve context per step from each telegram's own `SecContext=`. `txseq=`/`rxseq=` appear nowhere in the Point-API template.
- **Don't confuse `pahctx` param `activate=` (1 use) with the XML attribute `Activate="yes"/"no"` (3614 uses)** — different namespaces; the attribute is long since implemented. This tripped up a session; the user caught it.

**Still deferred:** `CheckRxKid` semantics unconfirmed; EITT tracks a `SeqNr (RX)` counter we do not; 5.4 needs Observe (kept in `coap_client` for that reason) and will surface `resource_schema` KeyErrors by design.

**Full technical detail is in the repo at `tests/docs/eitt-xml-interpreter.md`** (Migration status, Known Gaps, telegram-attribute audit) — that file, not this memory, is the source of truth.
