# EITT interpreter — audit findings tracker

Living record of known correctness gaps in the EITT XML interpreter
(`tests/runtime/eitt/`, `tests/runtime/transport/`). **Update the status of an
entry in the same commit that fixes it** — this file is how a later session
knows what is real, what is deliberate, and what is still outstanding.

Origin: adversarial audit on 2026-07-26, after sections 5.5–5.10 were completed
and the suite was fully green. The premise of the audit was that a green suite
is exactly when "passes" hides "passes for the wrong reason" — the user's
framing was *"we dont have fixes just for the sake of passing tests, rather have
solid mechanisms to execute the conformance tests from the xml"*.

Status values: **OPEN** · **FIXED** (with commit) · **ACCEPTED** (deliberate,
with rationale) · **WONTFIX**.

## Status at a glance

Last **verified** full-suite run, covering **every FIXED item** (F1–F3, F12,
T1–T2, T4, T8, T9, O1, O3-partial, M1, M3, M4): **177 passed, 8 skipped,
2 xfailed, 0 failed** (8m40s, exit 0).

The counts are identical to the pre-audit baseline and have not moved once
across sixteen fixes — the hardening regressed nothing, and the assertions are
simply real now where they previously were not.

**No known false passes remain.** M3 was the last one; M5, the other candidate,
was investigated and disproved.

### Added 2026-07-28, while building the GUI runner — NOT yet Docker-verified

Three findings came out of running real sequences against a live device
rather than the Docker DUT. **F15 changes what the interpreter puts on the
wire** (nine TestCases that previously emitted no discovery traffic now do),
so the suite must be re-run before these are trusted:

- **F15** `@@[pahdiscover` was a silent no-op — `on_discover` was never
  assigned by anyone. FIXED.
- **F16** every OUT row falls back to a hardcoded 5 s timeout; the template
  declares `AckChkTimeOut` on none of its 1457. OPEN.
- **T11** Windows delivers a queued ICMP error on an unrelated request and
  aborted it. FIXED.

Also fixed in the GUI's own runner (not interpreter findings, but the same
class of mistake): it passed an EMPTY `OverrideTable`, discarding the four
variable corrections and the manual-action map that `test_eitt.py` loads;
and it exported `DEVICE_HOST` for a live profile, which makes `conftest`
skip discovery entirely.

Two fixes needed a second attempt, both caught only by the full suite:

- **F12** first failed 5.3.1.2b on a legitimate separate-response (`planned ACK,
  received CON`) — resolved by allowing that specific, spec-permitted pair.
- **O1** first broke 13 TestCases by reserving the SSN on `seq_no_offset` as
  well as `seq_no_override` — resolved by scoping it to the override only.

Neither was visible from unit-level verification of the changed function; both
needed the protocol context. Worth remembering before trusting an isolated check
on anything in this layer.

### Run the suite in Docker, or the numbers are meaningless

**Always** `.\tests\runtime\tools\run-in-docker.ps1` (add `-IncludeUncommitted`
for working-tree changes). Running `pytest test_eitt.py` directly on the
Windows host **silently produces a completely different result**: there is no
veth pair and no `dut` netns, so multicast/group traffic has nowhere to go.

Measured 2026-07-27 — the host-native result is **23 failed / 155 passed in
~33 minutes**, versus **177 passed / 8 skipped / 2 xfailed in ~8.7 minutes**
in Docker, *at the same commit with an identical tree*. The failures cluster in
5.4.1.x (group messaging) and drag 5.3.19/5.3.20 down with them through
polluted state, and the ~4x runtime is multicast listeners timing out.

The 8-and-a-half-minute figure is also the tell that a run is genuine: a
~33-minute wall clock means the DUT is unreachable, whatever the pass count
says.

Nothing warns you. The suite collects, runs and reports normally. This cost
three 33-minute runs and very nearly caused a **correct** fix to be reverted as
a "regression" — the run with the fix and the clean-HEAD baseline were both
23/155, which is what finally gave it away.

Do not compare a host-native run against a Docker-recorded baseline. Do not
conclude anything from a host-native run at all.

All four protocol layers have now been audited. Series: **F** interpreter,
**T** CoAP transport, **O** OSCORE, **S** SPAKE2+, **M** mDNS/DNS-SD.

| Layer | Fixed | Open |
| --- | --- | --- |
| Interpreter (F1–F14) | F1 response code · F2 vacuous `all:` · F3 unanchored regex · **F12 response TYPE** · **F5 `#VAR` reported** · **F7 bare `@@` prompts** · **F10 narrowed `except`** · **F11 stale docstrings** · **F8 (partial) unknown input** · **F13 unsubstituted `#VAR` timeout** · **F14 `_apply_wait` on a Comment** | F6 ignored attrs · F8 (rest) `content_match` `.*` · F9 malformed values fail open |
| CoAP (T1–T10) | T1 malformed-datagram crash · T2 CON-ACK before token check · **T4 MID/token correlation** · **T8 stale buffered telegrams** · **T9 `recvfrom` truncation** · **T10 timeout vs discarded** · **T6 offset clamp** · *T5 (= O1)* | T3 (partial) repeated options · *(T7 = O5, WONTFIX)* |
| OSCORE (O1–O5) | **O1 nonce reuse** · **O2 sender-ID length guard** · **O3 (partial) narrow exceptions** · **O4 empty id_context** | O3 (rest) try-both-keys · *(O5 WONTFIX — replay detection moved to the capture plan)* |
| SPAKE2+ (S1–S2) | **S1 `shareV` validation** · **S2 warning documented** | — |
| mDNS (M1–M20) | M1 truncated-AAAA crash · **M2 per-packet parse** · **M3 stale cache (false pass)** · M4 stale query name · **M6 fallback `await_live`** · **M20 `socket()` outside `try`** | M7–M19 · *(M5 disproved — not a defect)* |

**Accepted, do not "fix":** F4 `opt:` permissive (tightening needs heuristic
regex parsing); CoAP no-retransmission *for adversarial sends* (`max_retries`
drops from 3 to 1 whenever a `SeqNo`/`SeqNoOffset`/`InvalidAuthTag`/`kid`
override or an expected echo challenge is in play, because a resend would break
the 5.3.17.x replay tests); and the documented KNX deviations in OSCORE and SPAKE2+ listed in those
sections — those are correct and verified against the device's C code.

### Why does the suite pass with this many findings?

A fair challenge, and worth answering precisely rather than reassuringly. The
findings fall into four groups, and **only the first group can affect a result
today**:

1. **Actually fired when switched on — 1 group.** F1 (response code never
   asserted) is the only finding that changed the outcome the moment it was
   fixed: 18 tests went red, tracing to one real device deviation
   (`4.01`→`4.03`) that the certification traces show was recorded and accepted.
   Everything else was already behaving correctly; the assertions simply were
   not checking.

2. **Unreachable with this template + this DUT — the largest group.** Measured,
   not assumed: O1's collision needs a pinned PIV inside the reachable SSN range,
   and 5 of its 6 uses pin 100000/100001 while the biggest TestCase sends 145
   requests. O2 needs a sender ID > 8 bytes; every ID here is ≤ 7. O4 needs
   `contextid=""` in the XML; no step has it. M11's TXT edge cases need repeated
   or valueless keys; `dns-sd_mdns.c` only ever emits a single `SP=<n>`. T9 needs
   a datagram > 4096 bytes; the largest observed is ~829.

3. **Fail-safe, not fail-open.** T1 crashed the run rather than passing wrongly.
   M1 raised and discarded records — a false *fail*. T2 suppressed a
   retransmission — a false *fail*. These make the suite noisy or brittle, never
   falsely green.

4. **Genuinely latent false passes — the ones that matter.** M3, M5 and M12 can
   pass a test that should fail, and they do not need malformed input to do it.
   M5 survives today only because 5.1.2.5a happens to assert on a PTR the goodbye
   omits entirely; **one template edit and it fires.** These are the findings
   that justify the whole audit.

The honest summary: a green suite meant "nothing detected", and for group 1 that
was because nothing was looking. It did **not** mean the transport was flawless —
it meant the defects were mostly in code paths this template and this device do
not exercise. That is luck plus a narrow test surface, not correctness, which is
why the group-4 items should not be left open indefinitely.

### Structural gap: nothing independently witnesses the transport

Every check today runs *through* the code being audited, and there are **zero
unit tests for 3,311 lines of protocol code**. If `_parse_options` is wrong, the
sending and the checking are wrong in the same way and the suite still goes
green. That circularity — not any single finding — is the real weakness.

**See `eitt-packet-capture-plan.md`** for the proposed fix: capture on
`veth-test` (the only link between tool and DUT, so 100% of traffic and zero
noise) and assert against the wire independently. It would settle O1 by
measurement rather than argument, and would have made T2 — invisible today except
as flakiness — obvious immediately. Planned, not implemented.

**Library replacement was considered and rejected** — see the same document's
"Related" section for the measurements behind that decision, including why
`zeroconf` does not fit despite mDNS being our weakest layer.

**Before fixing anything in `transport/`, read
`knx-protocol-deviations.md`.** Checking what aiocoap / its OSCORE module /
python-zeroconf do is a good instinct and they are usually right about framing —
but KNX deviates deliberately in specific places, and copying a library there
breaks conformance. That document lists every known deviation, read out of the
device's own C, so the comparison does not have to be re-derived each time.

### Act on these first

1. **F6 `Secure`** — 897 uses across 82 TestCases, and we never assert that a
   message actually was OSCORE-protected as the script demanded. The largest
   remaining unasserted attribute.
2. **O3 (remaining half)** — `unprotect_response` still tries both key
   constructions. Needs the packet-capture oracle to settle empirically which
   the device uses per case; every previous gating attempt regressed real tests.
3. **M6** — `await_live` missing on the fallback listener, so a spontaneous
   announce can be missed entirely (false fail).

**No known false passes remain.** M3 was the last one and is fixed; M5, the
other candidate, was disproved.

F12 (response type), T4 (token/MID correlation) and O1 (nonce reuse) were
previously on this list and are now FIXED.

**Two good-news results worth keeping visible:** SPAKE2+ **does** verify
`confirmV` in constant time, and its transcript is correctly ordered and fully
length-prefixed — the classic implementation bug is absent.

---

## F1 — Response code was never asserted · **FIXED**

**Severity: critical.** The single biggest gap found.

`_send_request_inner` stored only `resp.payload` and discarded
`resp.code_class`/`code_detail`; `_expect_response` never compared the XML's
expected `Code` against what the device actually answered.

Measured on the template: **1457 active `OUT` steps, of which 176 expect a
4.xx/5.xx error — and all 176 have empty or wildcard `<Content>`.** Those 176
therefore asserted *nothing at all*. Worst case: a request that times out
yields `None` → `b""`, which satisfies an empty-`<Content>` expectation, so a
completely dead device passed all 176 negative tests.

Concentrated in exactly the adversarial areas: 5.10.1 pagination (50),
5.3.17 Replay Protection (40), 5.3.8 auth/at (21), 5.1.1 discovery (8),
5.3.18 Integrity Protection (4).

Also unhandled: **36 active rows list SEVERAL acceptable codes**
(`"4.00,4.04"`, `"4.01,4.05"`, `"4.00,4.01,4.03,4.05"`). The old
`_response_code_tuple` regex rejected comma lists outright. This matters for
the certification body's documented "Forbidden (4.03) is accepted where the
test says Unauthorized" deviations.

**Fix:** `_remember_response()` records `(code_class, code_detail)` alongside
the payload — `None` meaning "nothing arrived", kept deliberately distinct from
an empty body. `_expected_response_codes()` parses the comma form.
`_expect_response` asserts before any body matching, and reports in EITT's own
wording: `"Message code differs, planned: 4.00, received: 4.03"`. A
method-named `Code` (a DUT-originated telegram) yields no expectation, and the
two DUT-telegram paths clear the stored code so a stale one is never re-checked.

---

## F2 — `all:` passes vacuously on an empty body · **FIXED**

`content_match.py` `_match_line_against_entries`. `_split_link_format_entries("")`
returns `[]`, so `non_matching` is `[]` and the rule passes. Verified:

```
all: .*;rt=":fb\..*   vs   b""   ->   ok=True
```

**43 active `all:` lines.** A device answering 2.05 with an empty body satisfies
every one. Classic vacuous universal quantification.

**Fix:** an `all:` rule over an empty entry list now FAILS. EITT's `all:` means
"the response is a list of links and every one of them looks like this" — a
response with no links does not satisfy that, it fails to be the thing being
described. Verified: empty body → False, real links → True, wrong `rt=` → False.

---

## F3 — `_value_matches` falls back to UNANCHORED regex · **FIXED**

`content_match.py`. Any expected literal that fails equality is retried as a
regex via `.search()`, unanchored. Verified:

```
expected '1'    vs actual 21       -> True
expected '0'    vs actual 304      -> True
expected 'true' vs actual 'untrue' -> True
expected '2.05' vs actual '12.056' -> True   ('.' is a wildcard too)
```

So `/value: 1` accepts `21`. The fallback is legitimate for the template's real
regex conventions (`h'[0-9a-f]{32}'`, `^[1-9][0-9]*$`) but must be gated.

**Fix:** `fullmatch` instead of `search`. Every expected value in this template
describes the WHOLE field, so an unanchored search turned each into a substring
test. Validated against all **105 distinct expected values**: every genuine
regex among them (`[0-9]{1,5}`, `\d*[1-9]\d*`, `h'[0-9a-fA-F]{32}'`, the
`^\[(?:,\s*|(?:1)|...)*\]` array forms, `\["if.sec"\]`) is self-contained and
still matches. Alternation (`0|4`) and boolean spelling (`true`/`false`) verified
unaffected.

---

## F4 — `opt:` returns True without evaluating · **ACCEPTED (for now)**

`content_match.py` `_match_line_against_entries`: `if mode == "opt": return
MatchResult(True)` — returns before using `matches`. **35 active `opt:` lines.**

`opt:` means "manufacturer-optional resource — if present it must have this
shape". EITT distinguishes *absent* (ok) from *present-and-malformed* (fail);
we accept both.

**Attempted and deliberately backed out 2026-07-26.** Tightening this needs the
resource's IDENTITY so "absent" can be told from "present but wrong", and the
only identity available is the pattern itself. The real patterns embed the link
target behind arbitrary lookaheads:

```
<.*\/dev/hwv>(;[^=]*=[^;]*)*
^(?=.*;ct=40)<.*\/auth>.*
^(?=.*;rt="urn:knx:fb\..*")(?=.*;ct=40)<.*\/f\/.*>.*
<.*\/swu\/maxdefer>;(?=.*if="[^"]*")(?=.*((?:60 50)|(?:50 60)|(?:60))).*
```

Extracting the target means heuristically parsing a regex — fragile enough to
risk inventing failures the template never asked for (my first extraction regex
did not even compile). Left permissive on purpose and documented in the code,
rather than replaced with a guess. Revisit only with a non-guessing way to
identify the target.

---

## F12 — Response message TYPE (ACK/NON/CON) is never asserted · **FIXED**

The direct sibling of F1, found by the same question ("what else does an `OUT`
row promise that we never check?"). `_expect_response` never compares
`msg.type`, and `resp.msg_type` is parsed onto `CoapResponse` and **never read
by the runner** — zero hits.

Scale: **1324 of 1457 active response rows are `Type="ACK"`** — piggybacked is
the dominant pattern, not an edge case — plus 130 NON and 2 CON. The traces
confirm the device genuinely answers that way (5_3.xml: 887 ACK vs 221 NON).

Consequence: a device answering a CON request with a **NON** instead of a
piggybacked ACK passes every one of those 1324 steps. That is real protocol
behaviour being unchecked — an ACK also discharges the request's reliability
obligation; a NON does not.

**EITT itself asserts both together**, which is the clearest evidence this is
in scope. From `traces/5_4_1.xml`:

```text
Message code differs, planned: 4.00, received: 4.03
Message type differs, planned: NON, received: ACK
```

Having fixed F1 we implement half of that evaluation. Note the template contains
one `Type="ACk"` typo, which a case-insensitive compare must absorb (as
`_coap_msg_type` already does on the send side).

**Receiving is correct and needs no change** — an empty ACK (code `0.00`) is
correctly treated as the separate-response signal, a non-empty ACK as
piggybacked, per RFC 7252 §5.2.1. The gap was purely the missing assertion.

**Fix:** `_remember_response` now records `resp.msg_type` alongside the code
(and the two DUT-telegram paths clear it, so a stale type is never re-checked),
and `_expect_response` asserts it right after the code check. Wording mirrors
EITT's own: *"Message type differs, planned: ACK, received: NON"*.

Design choices, both deliberate:

- **Case-insensitive lookup.** The template contains one `Type="ACk"` typo;
  `_coap_msg_type` already absorbs it on the send side, so the receive side
  matches. Verified `ACK`/`ACk` both map to ACK.
- **Unrecognised or empty `Type` ⇒ no assertion.** A future spelling degrades to
  the previous behaviour rather than inventing a failure. Same for "no response
  received", which the code check has already reported more precisely.

Verified: 5.1 + 5.7 + 5.8 = 49 passed, and the decision logic was exercised
directly — `planned ACK / received NON` and `planned NON / received ACK` both
fail with the right message, matching cases pass, empty `Type` asserts nothing.

**This closes the pair EITT treats as one evaluation.** Together with F1 we now
make both halves of `traces/5_4_1.xml`'s
*"Message code differs… Message type differs…"*. It matters most on the OSCORE
path, where the outer code is **always** 2.04 and the outer header therefore
carries almost nothing else worth checking.

### One deliberate allowance: planned ACK, received CON

The first full-suite run with F12 active failed exactly one step —
**5.3.1.2b** (invalid SPAKE2+ confirmP) — with *"planned: ACK, received: CON"*.
Investigated rather than assumed:

- The template writes `ACK` on all three SPAKE responses in that case.
- `traces/5_3.xml` shows the certified device answering **ACK on every one of
  100+ SPAKE responses**, never a CON. So the template is right and the CON came
  from our run.
- The explanation is the **separate-response pattern** (RFC 7252 §5.2.2): the
  device could not answer within the ACK timeout, so it sent an empty ACK first
  and the real response later as its own CON. Our client already handles this
  correctly — empty ACK skipped, CON taken and ACKed. Plausible here because
  5.3.1.2b makes the device do the full SPAKE2+ verification before rejecting.

So `expected ACK / actual CON` is now **explicitly allowed**. It is a
spec-permitted way to answer a CON request, and the template says "ACK" because
that is what the reference device happened to do, not because the separate form
is a defect. Every other type mismatch still fails — including the reverse
(planned CON, received ACK) and anything involving NON or RST.

---

## F14 — `_apply_wait` crashes on a Comment (latent until F7) · **FIXED (2026-07-27)**

`_run_manual_prompt`'s observation-only branch calls
`self._apply_wait(comment)`, but `_apply_wait` starts with `if not msg.wait:`
and a `Comment` has neither `wait` nor `time_to_next` — only `CoAPMessage`
does. Result: `AttributeError: 'Comment' object has no attribute 'wait'`.

Pre-existing, but **unreachable before F7**: the only observation-only prompts
were 5.2.9.1's and 5.2.10.1b's "Please check LAST UPDATE value", and both are
`Activate=0`. F7 routed the first *active* observation-only prompt
(5.4.1.8's bare `@@`) into that branch and the latent crash fired
immediately — the single failure in an otherwise green Docker run
(`1 failed, 176 passed, 8 skipped, 2 xfailed`).

**Fix.** `getattr(msg, "wait", None)`, so a Comment is accepted and ignored.
A prompt's pause is not its own attribute anyway — it is the separate
`@[w"..."` step that follows (5.4.1.8 item 9).

**Verified** all three branches: a `Comment` no longer raises and sleeps
nothing; a message with `Wait=yes` + `TimeToNext="00:00:02.0"` still sleeps
2.0 s; `Wait=no` still skips.

**Worth noting how this was nearly missed.** The same TestCase had already
"failed" in three earlier host-native runs, where it was indistinguishable
from the 22 environment-induced failures around it. Only a correct Docker run
isolated it as the *single* failure — which is what made it obviously mine.

---

## F13 — Listener timeouts parse an UNSUBSTITUTED `#VAR` and collapse to 5 s · **FIXED (2026-07-27)**

Found while measuring F9. Two receive paths in `runner.py` passed the **raw**
`TimeToNext` attribute to `_parse_time_to_next()` instead of substituting it
first:

- `_arm_group_listener` (~line 1697) — the group-telegram listener
- the unicast s-mode telegram wait (~line 2172)

`TimeToNext` is almost always a variable reference, not a literal: **1489**
`#GENERAL_TIME_TO_NEXT`, **71** `#FACTORY_RESET_TIME_TO_NEXT`, **10**
`#RESTART_TIME_TO_NEXT`. `_parse_time_to_next("#RESTART_TIME_TO_NEXT")`
matches neither `HH:MM:SS.f` nor `float()`, so it returns `0.0` — falsy — and
the whole `a or b or 5.0` expression collapsed to the **5.0 fallback**.

| variable | intended | actual before fix |
| --- | --- | --- |
| `#RESTART_TIME_TO_NEXT` | 20.0 s | 5.0 s |
| `#FACTORY_RESET_TIME_TO_NEXT` | 2.0 s | 5.0 s |
| `#GENERAL_TIME_TO_NEXT` | 0.5 s | 5.0 s |

The 20 s case is the damaging one, and the code comment sitting directly above
the bug names it: *"5.4.1.9's telegram follows a device RESTART and its
TimeToNext is `#RESTART_TIME_TO_NEXT` (20s), because the device has to come
all the way back up before it can send the init read."* The listener was
waiting 5 s for something the script budgets 20 s for — a device slower than
5 s reads as "never sent the telegram", a flaky-looking failure with a real
cause. (`max(timeout, 5.0)` at the call site hides the two *shorter* values,
so only the restart case changes behaviour in practice.)

**Fix.** Wrap both in `self.variables.substitute(...)`, matching the other two
call sites (lines 974 and 2375) which already did this. Verified `substitute`
passes `None` and `""` through unharmed (both → `0.0`, as before), that
`#RESTART_TIME_TO_NEXT` now yields 20.0 s, and that literal `00:00:05.0` /
`1000` forms are unchanged.

**Measurement note.** My first pass flagged 1592 "bad" `TimeToNext` values by
scanning the raw XML — the same trap F5 hit. Those are simply unsubstituted
variables; the real question was what the parser receives *at runtime*, which
is what exposed the two call sites that never substitute.

---

## F5 — Unresolved `#VAR` degrades into a self-satisfying assertion · **FIXED (2026-07-27)**

`variables.py` returns the token unchanged when a variable is missing. Combined
with F3, the unresolved token then *matches itself*:

```
substitute('/value: #MISSING_VAR') -> '/value: #MISSING_VAR'
match vs {'value': '#MISSING_VAR'} -> ok=True
```

A request body/path containing a missing variable also sends the literal text
`#MISSING_VAR` to the device. A renamed Field in a future XML revision would
silently degrade tests rather than error. Should raise, or at minimum be
counted and reported.

**Fix: counted and reported, deliberately NOT raising.** `substitute` has 25
call sites spanning paths, bodies, CBOR, match text and timing, and they do not
share one policy — aborting a whole run inside a low-level string helper is too
blunt. `VariableTable.unresolved` (a set) records every name that resolved to
nothing; `test_eitt` snapshots it before `runner.run()` and diffs afterwards,
because **the table is shared by every TestCase in a collection** and without
the diff one case's missing name would be re-reported against every case after
it. A case that otherwise passes but used an unresolved name now FAILS — that
"pass" is exactly the tautology the finding describes and cannot be trusted.
Existing failures get the names appended as context instead.

**Measured before enabling it: zero unresolved names across all 366
`#`-containing strings, in every collection, using each collection's own
scope.** So this reports nothing today and cannot turn a green test red — it
arms the trap for a future XML revision.

**Trap worth not re-deriving — `#10`, `#13`, `#9`, `#42`, `#50` are NOT
variables.** A raw grep of the template for `#([A-Za-z0-9_]+)` "finds" them and
makes it look like hundreds of variables fail to resolve. They are XML numeric
character references (`&#10;` = LF, `&#13;` = CR, `&#9;` = TAB) plus two errata
numbers in comments. **The XML parser decodes them before `substitute` ever
runs** — verified by inspecting a parsed `Content`, which holds a real `\n` and
no literal `#10`. Always measure this on the PARSED model, never on the raw
file. (The regex accepting bare digits is nonetheless a latent hazard: a genuine
`#10` in post-decode text would be read as a variable named `10`.)

---

## F6 — Ignored attributes that are parsed into the model · **OPEN**

Parsed but never read at execution time — silent gaps, all in *active*
(non-skipped) TestCases unless noted:

| Attribute | Uses | TestCases | Note |
|---|---|---|---|
| `Secure` | 897 | 82 | Never asserted that a message was/wasn't OSCORE-protected as scripted. Encryption is decided solely by whether `SecContext` resolves. 233 `OUT Secure="yes"` rows with an empty `SecContext` are contradictions we never detect. |
| `SeqNumMethod` | 70 | 35 | All values `"0"`, likely the default — probably benign but unverified. |
| `RxKid` / `CheckRxKid` | 2 / 2 | 5.3.17.3 | Group Message Anti-Replay, an active test. |
| `Cycles="*"` | 1 | 5.4.1.15 | Active test. |
| `Observe` / `ObserveValue` | 6 / 6 | 5.4.2 | Only in the skipped 5.4.2 suite — invisible from results. |

`RspSchema` (2391 uses) is not parsed at all, but every value is an empty
string, so it is genuinely inert.

### `Secure`: attempted, reverted — what it actually means

The obvious implementation is wrong, and the trap is worth recording.

**First model (wrong):** compare `OUT Secure` against whether *our request* was
sent OSCORE-protected. Implemented, and it failed AUTH PREPARATION immediately
with *"planned: unsecured, actual: secured"*, cascading through 5.3.

**What `Secure` on an OUT row actually asserts** is whether the **RESPONSE**
arrived protected — which is *not* the same as whether the request was. Across
427 OUT rows with the attribute the two agree 419 times, and the 8 exceptions
are precisely the interesting ones:

| TestCase | request ctx | OUT `Secure` | why |
| --- | --- | --- | --- |
| 5.3.18.1 (×4) | `TokenTest` | `no` | the auth tag is deliberately corrupted, so the device **cannot decrypt** and answers unprotected |
| 5.2.2.1 | `TokenTest` | `no` | request follows a factory reset — context no longer exists device-side |
| 5.2.2.3 | `Temporary Key` | `no` | same, after restart |
| 5.4.1.15 | `TokenTest` | `no` | |
| 5.4.1.15 | *(none)* | `yes` | a DUT-originated telegram we answer |

So **every conflict is a negative test**, and in each the whole point is that the
device *fails* to process a protected request and replies in the clear.
Asserting against the request's context inverts exactly those cases.

**Also note `SecContext` on an OUT row names the context the response
ESTABLISHES, not one it is protected under** — the SPAKE 2.04 that completes the
handshake carries `ctx='Temporary Key'` with `Secure="no"`, because that response
creates the context and is itself unprotected.

**To implement correctly**, `_recv_oscore_response` must report whether the reply
actually decrypted, and that flag — not the request's context — is what `Secure`
compares against. Left OPEN. `_last_request_secured` is recorded in the runner
already but is deliberately *not* used for an assertion, with a comment saying
why.

---

## F7 — Bare `@@` operator prompts silently no-op · **FIXED (2026-07-27)**

`_MANUAL_PROMPT_RE` requires `@@!`. Two comments in **5.4.1.8** use bare `@@`
(`@@Trigger sensor during next 10 seconds after pressing OK`,
`@@Test Finished, Press OK`). Per `EITT_KnxCommentCommandsScheme.xml`, `@@`
means "display and **suspend evaluation**" — an operator prompt. These fall
through to the unconditional no-op pass.

Same bug class the `@@!` handling was written to fix, one variant short. See
the memory note *"@ commands are behavioural"*.

**Fix.** Widened the pattern to `^@@(?!\[)!?\s*(.+)$` so bare `@@` routes
through the *same* `manual_actions` mechanism as `@@!` (`!` only adds a beep;
both are "display and suspend evaluation"). The `(?!\[)` keeps `@@[<cmd>`
— the unrelated provisioning-command prefix — out, so the pattern is correct
on its own rather than relying on dispatch order in `_run_comment`.

Measured against the parsed model (not raw grep): the template has exactly
**2** bare `@@`, both `Activate=1`, both in 5.4.1.8, and both previously
matched *nothing*. Two `manual_actions` entries added:

| prompt | mapping |
| --- | --- |
| `Trigger sensor during…` | acknowledgement only, **no endpoint** |
| `Test Finished, Press OK` | acknowledgement only, no endpoint |

**A wrong first attempt, caught by the trace.** The obvious reading — map
"Trigger sensor during…" onto `POST /test/trigger`, as `_trigger_sensor`
already does for the analogous 5.4.1.15 — is **wrong**.

(An earlier draft of this entry blamed that mapping for a 23-failure run.
That was a misattribution: the run had been executed on the Windows host
instead of in Docker, and clean HEAD produces the identical 23/155 there. See
"Run the suite in Docker" at the top. The mapping is still wrong, but for the
reason below, not because of any measured regression.)

`traces/5_4_1.xml` settles it: between `pahmcast"…join=true"` (element 1591)
and `Setup: Set LSM to 'loaded'` (1593) there is exactly **one empty comment
and no telegram at all**. The real operator triggered nothing. 5.4.1.8
asserts the device does **not** transmit while LSM is `loading`, so injecting
a trigger tests the opposite of what the TestCase exists to check. The
element after the second prompt carries `TimeToNext=22:27:29.8` — the
operator left the modal open overnight — confirming nothing downstream
depends on that window's duration either.

This is the check-template-**and**-trace rule doing real work: the template
alone reads as "a human presses the sensor here", and only the recorded
certification run shows that no traffic resulted.

**Verified**: all 6 template prompts (4 × `@@!` + 2 × `@@`) resolve, 0
unmapped, and the 4 pre-existing `@@!` extractions are byte-identical. A
10-case regex table confirms the 4 forms that must NOT match (`@@[pahmcast`,
`@@[w`, `@!`, `@#`) still don't, and that a bare `@@` with no text doesn't
either.

**Also noted while measuring** (not a defect): the prefix histogram showed 2
bare `@` comments in 5.2.9.1, which turned out to be `@>w"…"` loop
invocations already handled by `_LOOP_INVOKE_RE` — my classifier just lacked
a `@>` bucket. Full histogram over 6559 sequence items: `@[` 2504, plain 461,
`@@[` 305, `@#` 131, `@@!` 4, `@>w` 2, `@@` 2.

---

## F8 — Unknown input is skipped instead of raising · **PARTIALLY FIXED (2026-07-27)**

Three places contradict the stated design intent ("each raises or skips loudly
rather than guessing", `runner.py` module docstring):

- `model.py` `_parse_sequence` — `if/elif/elif` over `Comment`/`CoAPMessage`/
  `DnsMessage` with **no `else`**. A new element type in a future revision is
  silently dropped and the TestCase passes having never executed it. Latent
  today (the template contains only those three). — **FIXED**
- `runner.py` `_run_comment` — any unrecognised `@`-command returns
  `ok=True`. A typo'd `@@[pahctx` becomes a silent no-op. Note the asymmetry:
  `provisioning.apply` and `hooks.get_hook` both correctly *raise*. — **FIXED**
- `content_match.py` — a standalone `.*` line is skipped position-independently,
  so `/value: 1` + `.*` silently loses the second line. — **still OPEN**

### `_parse_sequence` (fixed)

Added an `else` raising a new `UnsupportedSequenceItem`, at PARSE time so it
surfaces during collection naming the offending tag. Measured first: the only
children of `<Sequence>` in this template are 3409 `Comment` / 3109
`CoAPMessage` / 41 `DnsMessage` (6559 total, nothing else), so the raise
cannot fire on today's XML.

**Verified both directions** on a scratch copy (master template untouched):
the real template still parses to exactly 6559 items, and a copy with an
injected `<FutureThing/>` raises instead of silently dropping the step.

### `_run_comment` (fixed)

A blanket "unknown `@` raises" would have been badly wrong — measured, **2481
active `@`-comments** match no handler, and they are overwhelmingly legitimate
display-only text. Broken down: 2463 `@[t`, plus 18 that are `@[` + prose with
the `t` omitted by the author (`@[Preparation: …`, `@[Recipient Multicast
GRPID (id=5) …`, `@[Check that the device rest was successful …`), several
appearing verbatim elsewhere *with* the `t`.

So the fix is a whitelist, `_DISPLAY_ONLY_RE = ^(@\[[a-zA-Z]|@!)`, with
everything else starting with `@` failing the step and naming the text. `@[w`
also matches that pattern but is claimed by `_WAIT_RE` earlier in the dispatch
chain, so wait commands are unaffected.

**Verified** by replaying the real dispatch order over all 3409 comments in
the template: 2942 display/plain no-op · 303 provisioning · 131 inline ref ·
25 wait · 6 operator prompt · 2 loop · **0 that would newly fail**. An 8-case
table confirms `@%bogus`, `@zUnknownCmd` and `@[9numeric` now fail while
`@[tnarrative`, `@[Preparation: …`, `@!beep`, `@[w"00:00:10"` and plain text
still pass.

---

## F9 — Malformed XML values fail OPEN · **OPEN**

A consistent pattern of dropping the adversarial modifier on bad input:
`_safe_int` → `0` (and `0` also means "don't care" for the mDNS port);
`_parse_time_to_next` → `0.0` (no wait); `SeqNo`/`InvalidAuthTag`/`EchoValue`
→ `None` (no override); `_content_format_code`/`_resolve_crit_option` → `None`
(option dropped). For 5.3.18.1 a typo'd `InvalidAuthTag` turns a negative test
into a positive one.

### Measured 2026-07-27 — one instance is LIVE, not hypothetical

Checked every relevant value in the template through the actual parsers:

| parser | values checked | silently degraded |
| --- | --- | --- |
| `_safe_int` (`SeqNo`/`SeqNoOffset`/`Port`) | 159 | **0** |
| `_resolve_crit_option` | 1 | 0 — resolves to `(65123, b'\xab\xcd')` |
| `_content_format_code` (`AcceptedCF`) | 1520 | **3** |

The three are real template corruption: `AcceptedCF="a"` (×2) and
`AcceptedCF="r"` (×1) — single-character fragments of `application/...`,
against 1242 `application/cbor` and 275 `application/link-format` well-formed.
All three rows are `Activate=1`:

- 5.3.1.2b item 12 (`2.04`)
- 5.5.4.1 item 12 (`2.05`) and item 15 (`GET fp/g/1`)

The reference traces contain **no** such values — `traces/5_3.xml` and
`traces/5_5.xml` hold only `application/cbor`, `application/link-format` and
the explicit string `'not set'` — so EITT itself never emitted `a`/`r`. The
Accept option is currently dropped from these three requests without a word.

**Deliberately not fixed in this batch.** Making `_content_format_code` raise
would fail three TestCases that pass today, and the defect is in the template
*data*, which must not be edited. The right shape is probably a loud warning
plus an `overrides.yaml` correction entry (the same three-layer variable model
already used for `GENERAL_TIME_TO_NEXT`), not a hard raise — that needs a
decision, so it is left open rather than guessed at.

Note also that EITT distinguishes "unset" (`'not set'`, 1457 rows in
`traces/5_3.xml`) from empty, while the template uses `""` for 997 rows. Any
future fix has to keep those two apart.

---

## F10 — `_decode_body` catches bare `Exception` · **FIXED (2026-07-27)**

`runner.py`. Should narrow to `(cbor2.CBORDecodeError, UnicodeDecodeError)` so a
genuine bug in `resource_schema` (e.g. a `TypeError` in `decode`) cannot
masquerade as "the device sent something undecodable".

**Fix.** Narrowed to `(cbor2.CBORDecodeError, UnicodeDecodeError, ValueError)`.
`cbor2` is now imported at the top of the function rather than inside the CBOR
branch — otherwise naming `cbor2.CBORDecodeError` in the `except` clause raises
`NameError` on the link-format path, which never executes that import. Worth
noting `CBORDecodeError` is **not** a `ValueError` subclass (checked), so
listing it explicitly is necessary, not redundant.

Also removed the dead `except KeyError` around `get_schema()` inside the same
`try` — see F11: `get_schema()` does not raise.

**Verified** both directions, since a narrowing that stops absorbing real
garbage would be a regression:

- still returns `None` for truncated CBOR and for genuinely undecodable input
- still decodes valid CBOR (`{11:"/p/3"}` → `{'href': '/p/3'}`) and link-format
- a simulated `TypeError` injected into `ResourceSchema.decode` now
  **propagates** instead of being laundered into `None`

Two probe results that looked like failures but were not: a `UnicodeEncodeError`
came from the test's own `print` on the Windows console codepage (the decode
itself uses `errors="replace"` and cannot raise), and `b"\xff\xff\xff\xff"`
returning an opaque object is cbor2's `undefined` sentinel — decoded, not
crashed.

---

## F11 — Docstrings assert a `KeyError` the code does not raise · **FIXED (2026-07-27)**

`resource_schema.py` module docstring says `get_schema()` "raises rather than
guessing"; it returns `_DEFAULT_SCHEMA`. `test_eitt.py` repeats the claim. The
fallback itself is defensible (EITT really does have a `Default="yes"` table) —
the *documentation* is wrong in three places.

Related: `transport/coap_client.py`'s module docstring still describes a
"synchronous and stateless… single UDP send/recv pair with no retransmission"
client and cites replacing aiocoap over a Context-exhaustion bug. The module is
now ~1300 lines with CON/NON/OSCORE/echo-challenge builders, a multicast group
listener, an unsolicited-request buffer and observe support. The *real*
justification today is that KNX-IoT OSCORE deviates from RFC 8613, and that
conformance testing must be able to send deliberately malformed frames
(`InvalidAuthTag`, `SeqNoOffset`, unknown critical options) — things a
correctness-enforcing library actively prevents.

**Fix.** All three docstrings corrected.

1. `resource_schema.py` — now states that an unmapped **path** falls back to
   `_DEFAULT_SCHEMA` (mirroring EITT's own `Default="yes"` table) and that what
   *does* raise is an unknown **field** name in `ResourceSchema.encode()`.
   Verified both: `get_schema("/nope/xyz")` returns the default schema, and
   `encode({"bogusfield": 1})` raises `KeyError` listing the known fields —
   including on the default schema.
2. `test_eitt.py` — same claim ("an unmapped path fails loudly (KeyError)"),
   corrected the same way.
3. `transport/coap_client.py` — the file is **2026 lines**, not the "synchronous
   and stateless… single UDP send/recv pair with no retransmission" client the
   docstring described. Both claims are false: it retries (`max_retries = 3`,
   dropped to 1 for adversarial sends where a retry would corrupt the test) and
   holds MID/token counters plus a pending-request buffer. Rewritten to lead
   with the deliberate-non-conformance rationale, keeping the aiocoap
   Context-exhaustion history as secondary. Each capability named in the new
   text was grep-verified before writing it: observe register/deregister,
   `IPV6_JOIN_GROUP`, `send_echo_challenge`, `oscore_request`, separate-response
   handling.

---

---

# Transport layer (`tests/runtime/transport/coap_client.py`)

## F15 — `@@[pahdiscover` is a silent no-op · **FIXED (2026-07-28)**

`ProvisioningEngine._apply_pahdiscover` calls `self.on_discover()` — a
callback **nothing ever assigned**. Neither the GUI nor `test_eitt.py` set
it, so the command did nothing at all: 10 uses across 9 TestCases (5.2.1.1,
5.2.2.1, 5.2.2.3, 5.3.1.1/.2/.3, 5.3.17.8, 5.4.2.1) emitting no traffic
whatsoever.

Found by the user asking why an exported trace showed no discovery output.
The recorded certification run answers what it should look like: **33
discovery telegrams**, six per invocation — `GET /.well-known/core` to
`ff02::fd`, `ff03::fd` and `ff05::fd` with a reply each, bracketed by
`pahdiscover, suspend plan evaluation` / `resume plan evaluation`
(traces/5_2.xml, elements 10-17).

**Fix.** `SequenceRunner.__init__` now sets
`provisioning.on_discover = self._run_pahdiscover`, which sweeps the three
scopes and emits the telegrams.

Two properties taken from the recorded run rather than invented:

- **A failing scope is not an error.** EITT brackets the sweep with
  "suspend plan evaluation" — the telegrams are logged but not judged, and
  the TestCase that follows addresses the device by its configured alias
  regardless. On Windows one of the three scopes routinely has no route
  out; that must not fail a run.
- **Replies are emitted UNNUMBERED.** They were captured, not awaited, so
  numbering them would shift every later telegram out of step with the
  recorded trace.

**Verified** against a live device: 5 telegrams, real replies from
`[fe80::4aec:a409:2973:2a2c]:49887`, where the count was previously 0.

## F16 — Timeouts are hardcoded, and no OUT row overrides them · **OPEN**

`AckChkTimeOut` is declared on **0 of the template's 1457 active OUT rows**,
so every one falls back to the CoAP client's default (5 s, 3 retries). That
is fine over a loopback and against the Docker DUT, but it is not something
the template chose — it is the value that happened to be in the client.

For real hardware the risk is one-directional and bad: a device that
answers in 6 s is recorded as **NOT RECEIVED**, i.e. a conformance failure
that did not happen. The GUI now exposes the timeout in its connection
dialog (default 5 s, raise for slow hardware), but the headless harness
still has only `COAP_TIMEOUT`.

**Not** to be confused with `TimeToNext`, which resolves to 0.5 s on 1348
OUT rows via `GENERAL_TIME_TO_NEXT=500`. That looks like a loopback
speed-up and is not: 500 is the value in the V40 variable-values JSON the
traces were recorded against, so it is certification-accurate pacing and
must stay.

Open question for a real-hardware run: whether 5 s is the right default, or
whether the harness should derive it from the DUT's observed response time.

## T1 — Malformed datagram crashes the run · **FIXED**

`_parse_options` had no bounds checks. Verified by fuzzing:

```
truncated after delta=13 nibble  -> RAISED IndexError
truncated after length=14        -> RAISED struct.error
option length runs past buffer   -> {0: b'AB'}  payload_off=6  (buffer is 3 bytes)
reserved delta=15                -> {15: b''}   (RFC 7252: MUST be a format error)
```

None of the five call sites caught anything, so **one malformed datagram — from
the device or any stray traffic on the shared socket — aborted the whole run
with a traceback** instead of being skipped. The over-long option was worse than
a crash: it silently truncated and returned a `payload_offset` past the end of
the buffer, i.e. a plausible-looking wrong parse.

**Fix:** bounds-check every extension read and the option value; reject the
reserved 15 nibble and a payload marker with no payload; raise a new
`CoapFormatError`. All five receive loops now catch it and skip the datagram.

## T2 — CON was ACKed BEFORE the token check, and to the wrong address · **FIXED**

Both `_recv_response` and `_recv_oscore_response` did:

```python
if msg_type == CON:                      # ACK first...
    self._sock.sendto(ack, (self.host, self.port, 0, 0))
if expected_token is not None and token != expected_token:
    continue                             # ...then decide it wasn't ours
```

Two defects:

- **Ordering.** A CON carrying someone else's token is very often a telegram the
  DUT originated *and is waiting on*. 5.4.1 asserts on the DUT's
  **retransmission** of an unACKed telegram. ACKing it here satisfies the
  device, the retransmission never arrives, and the test fails "no telegram
  arrived" — a false FAIL that appears only when a response read overlaps a
  DUT-originated telegram, i.e. as **flakiness**.
- **Destination.** The ACK went to `(self.host, self.port)`, never to the `addr`
  the datagram actually came from. The DUT answers from an ephemeral source port
  (noted in `runner.py`), so the ACK could miss entirely; the device then
  retransmits and those copies land on the shared socket during a *later*
  exchange.

**Fix:** token check first, then ACK, and send it to `addr`.

## T3 — `_parse_options` collapses repeated options · **PARTIALLY FIXED**

`options[opt_num] = opt_val` overwrites, so `Uri-Path` (11) and `Uri-Query` (15)
keep only their **last** segment: `GET /fp/g/1` parses back as `{11: b'1'}`.

The code already knew and worked around it — `PlainListener` and
`UnicastTelegramListener` both match on the *final path segment only*, with
comments saying so. That means a telegram to `/p/1` and one to `/x/1` are
indistinguishable to those filters.

**Partial fix:** `_parse_options` now takes an optional `repeated` dict that
collects every instance, so a caller can recover the full path. The flat return
value is unchanged, deliberately — the ~20 existing readers all want
non-repeatable options (Content-Format 12, OSCORE 9, Observe 6, Echo 252) and
changing the shape under them is a separate, riskier change.

**Still open:** the two listeners still compare last-segment-only. Fixing that
means threading `repeated` through `parse_coap_message` and the listener
`_accept` paths.

## T4 — No MID correlation; tokens are a predictable per-instance counter · **FIXED**

Correlation was token-only; the MID was parsed and never compared. Tokens were a
2-byte counter **starting at 0 for every new `CoapClient`**, so a delayed reply
from a previous client instance bearing token `0x0000` would be accepted as the
answer to the new client's first request. RFC 7252 §5.3.1 wants both MID
matching for piggybacked ACKs and non-trivially-guessable tokens.

**Fix, both halves:**

1. **Tokens are now 8 bytes: 4 random + a 4-byte process-wide counter.** The
   random half makes collision with an in-flight stale response negligible; the
   counter half guarantees no two tokens from this process are ever equal.
   Verified that a fresh client instance no longer reproduces the first
   client's token sequence — the actual bug. Checked that nothing in the tree
   assumes a 2-byte token, and that the template's two `Token=` uses are the
   boolean `Token="yes"` flag, not a pinned value.
2. **Piggybacked ACKs must carry the request's Message ID.** Applied to `ACK`
   only, deliberately: a *separate* response (RFC 7252 §5.2.1) arrives later as
   its own CON with a fresh MID, so checking it there would reject exactly the
   case the empty-ACK branch sets up. 1324 of 1457 active response rows are
   piggybacked, so this covers the overwhelming majority.

Verified: 5.1 + 5.7 + 5.8 = 49 passed (5.1 is the discovery section, the one
most sensitive to correlation).

**Still open, tracked separately:** `request()` returns `None` for *timeout*,
*deadline expiry* and *"every datagram had the wrong token"* alike, so a
correlation bug still presents as a device conformance failure. See T10.

## T10 — Timeout is indistinguishable from "discarded everything" · **FIXED (2026-07-27)**

Split out of T4. `request()` and `_recv_response` both return `None` for a socket
timeout, for deadline expiry, and for "every datagram I saw had the wrong
token/MID". `runner` maps all of them to `_last_response_code = None`, which F1
now reports as *"no response was received (request timed out)"* — accurate for
the first two, misleading for the third. A correlation bug therefore looks like a
device that went silent. Wants a distinct sentinel or a counter of discarded
datagrams in the failure message.

**Fix.** `CoapClient._last_discards` — a list reset at the top of each
`_recv_response`, appended to at all four discard sites (runt `< 4` bytes, token
mismatch, ACK MID mismatch, malformed options), each entry naming the reason and
the observed code. `runner`'s F1 diagnostic now branches on it: empty list keeps
*"no response was received (request timed out)"*, a non-empty one reports
*"no USABLE response: N datagram(s) arrived but were discarded [...]"* with up to
four reasons inline.

Chose a diagnostic list over a distinct sentinel deliberately: a new sentinel
would change the `None` return contract at ~7 call sites for no behavioural gain,
whereas the actual need is only ever to explain a failure after the fact.

**Diagnostics only — cannot change any pass/fail result.** Both branches verified
by execution against a stub device: one answering with token `deadbeef` against
expected `0bbe03aa00000001` yields
`token mismatch (got deadbeef, expected 0bbe03aa00000001, code 2.05)`; a device
that never answers yields `[]` and the unchanged timeout wording. That second
check matters as much as the first — reporting a phantom discard for a genuinely
silent device would be worse than the original bug.

## T5 — OSCORE Partial IV can be reused under one key · **FIXED by O1 (status corrected 2026-07-27)**

**This is the same defect as O1, filed twice from the CoAP and OSCORE audits.**
The heading said OPEN long after `_reserve_ssn` fixed it — corrected here after
re-running the exact scenario below.

`knx_oscore.py`: `seq_no_override` pins the PIV **without consuming** a sequence
number, so the next normal request reuses it:

```text
protect_request(seq_no_override=3) -> piv 03, ssn stays 0
next 4 normal requests             -> piv 00, 01, 02, 03   <-- 03 again
```

Two plaintexts, same key, same nonce — for AES-CCM that is a catastrophic crypto
failure in general. Bounded here: only reachable via `SeqNoFn="fix"` (5.3.18.1),
whose point is a deliberately-invalid message the device rejects anyway. But the
device then rejects it as a **replay** rather than for the corrupted auth tag
under test — the test passes for the wrong reason. Should advance the counter
past the pinned value.

**Fixed by O1's `_reserve_ssn` — read that entry for the two traps (reserve, do
NOT clamp; apply to `seq_no_override` ONLY, never `seq_no_offset`).** The
documented scenario above was re-executed on 2026-07-27 and no longer
reproduces:

```text
protect_request(seq_no_override=3) -> piv 03, counter advances to 4
next 4 normal requests             -> piv 04, 05, 06, 07   (03 NOT re-emitted)
```

## T6 — `max(0, ...)` clamp collapses distinct replay-window offsets · **FIXED (2026-07-27)**

`knx_oscore.py`: on a fresh context (`ssn = 0`) offsets `-1`, `-32`, `-35`,
`-40`, `-64` all clamp to PIV `0`, making "outside the window" and "just inside"
the same wire value. Currently **dormant** — traced 5.3.17.1 and the large
negative offsets land at OUT#66+ where `ssn` is already 65+. One reordering away
from a silent vacuous pass. Should assert rather than clamp.

**Fix — took the finding's own advice: assert, don't clamp.** Both `max(0, ...)`
sites are gone, and they needed *different* treatment:

- **`seq_no_offset`** now raises when `consumed + offset < 0`, with a message
  naming the offset, the SSN, and the fact that isolation/reordering is the
  likely cause. Measured the template first: **133 of 139 `SeqNoOffset` uses are
  negative** (`-1`×69, `-32`×64, `-35`×2, `-64`, `-40`) and each exists to land a
  chosen *distance* from the counter — the distance IS the assertion, so
  substituting 0 tests nothing.
- **`seq_no_override`** raises on any negative pin. Different failure entirely:
  that value is the template's `SeqNo=` transmitted verbatim, so a negative one
  means a misparse, and clamping would put a different number on the wire than
  the test names (the F9 fail-open class).

Verified by execution. At a realistic `ssn=65` the six real offsets stay
**distinct** — PIV 64 / 33 / 30 / 25 / 1 / 96 — and the counter still advances by
exactly one (66), preserving the O1 constraint that must not regress. At `ssn=0`
all five negatives that previously collapsed to PIV 0 now raise.

**Boundary checked explicitly, because PIV 0 is legal:** `-1` at `ssn=1` and
`-32` at `ssn=32` both correctly produce PIV `00`; only a genuinely negative
result raises. `seq_no_override` still accepts `0` and `100000` and rejects `-1`.

Dormant today, so no result should move — the value is that a future reordering
now fails loudly instead of passing vacuously.

## T7 — No dedup; no receive-side replay check · **SUPERSEDED by O5 (status corrected 2026-07-27)**

No MID-seen set anywhere. `unprotect_response` reads the response PIV but never
compares it against anything seen before, and `_decrypt_group_telegram` returns
the first message that decrypts with no PIV bookkeeping — so a replayed telegram
is accepted twice. `collect_notifications` would count a retransmitted CON
notification as two, though it is **not used by the EITT runner** (only by the
older hand-written observe tests).

**Same defect as O5**, filed from the CoAP audit before the OSCORE one; O5's
entry already said it "supersedes/absorbs T7" but this heading was never
updated. **Resolution is O5's: WONTFIX as client-side rejection, moved to
`eitt-packet-capture-plan.md` as wire-level detection.** Read O5 for the
reasoning — briefly, no injector exists on `veth-test`, the suite deliberately
replays PIVs across 138 steps, and dropping a real replay hides it behind a
timeout.

## T8 — `_pending_requests` is unbounded and never aged out · **FIXED**

Appended on every unsolicited DUT request, drained only by
`UnicastTelegramListener`. Entries carry no timestamp, so a stale telegram from
TestCase N was indistinguishable from a fresh one and could be handed to
TestCase N+5's listener. `drain_socket` drained the **socket** but left this
buffer untouched — so the one call that means "forget anything the device said
before now" did not actually forget a telegram already lifted off the socket.

**Fix:** `drain_socket` now clears `_pending_requests` as well, and reports both
counts in the debug line.

**Why this is safe for 5.4.1**, which *depends* on buffered telegrams surviving:
both call sites are outside a telegram wait — one after a manual restart/reset
prompt (`runner`, where the device is mid-reboot and anything buffered is by
definition stale), one at collection start after a factory reset
(`test_eitt.py`). Neither runs inside the request/telegram window that
`UnicastTelegramListener` drains.

Not fixed: the buffer is still unbounded and still has no age-out. Bounded in
practice by test length, and now flushed at the two natural boundaries.

## T9 — `recvfrom(4096)` silently truncates · **FIXED**

Linux discards the remainder of an oversized UDP datagram with no error, so an
undersized buffer truncates silently and the failure then presents as a content
mismatch rather than as truncation.

Not reachable with the current template (max observed payload ≈ 829 bytes; 5.2's
firmware upload uses application-level `?ps=`/`?po=` chunking with
`PREP_BLOCK_SIZE=500`, **not** RFC 7959 blockwise, which is not implemented at
all). A `GET /.well-known/core` from a resource-rich device is the realistic way
to exceed it.

**Fix:** a single `_RECV_BUF = 65535` constant applied at all **7** call sites —
64 KiB is the largest a UDP datagram can be, so nothing can be lost. `mdns_client`
already used 9000, which is above the practical mDNS limit, and was left alone.

## Transport: wrong per RFC, harmless here

Checked and deliberately left alone — real RFC 7252 violations this environment
cannot reach: TKL is never bounds-checked (9–15 are invalid); the version field
is never validated; an empty URI segment is dropped; percent-encoding is not
decoded before building Uri-Path; Content-Format/Accept are packed as a single
byte (crashes for a code ≥ 256, none exist here); `_encode_option` cannot express
a value ≥ 65805 bytes. **No retransmission at all** is also deliberate and should
stay: retransmitting would actively break the adversarial replay tests in
5.3.17.x, where a resend is a *different* telegram.

---

## T11 — Windows ICMP errors abort an unrelated request · **FIXED (2026-07-28)**

Windows queues an ICMP port/host-unreachable **against the socket** and
delivers it on the next `recvfrom()` as `ConnectionResetError` (WinError
10054) — even though the datagram that provoked it was a different, earlier
send. Because this client deliberately shares ONE socket for every exchange
(see the class docstring: it avoids server-side resource exhaustion), the
error lands on whatever request happens to be in flight and aborts it.

Surfaced by F15: a multicast sweep to a scope with no listener is the
classic source, so implementing `pahdiscover` immediately started killing
the unicast request that followed it. UDP has no connection to reset, so
the error says nothing whatsoever about the exchange it interrupts.

**Fix.** Both `_recv_response` loops drain the queued error, record it in
`_last_discards` and keep waiting for the real answer.

Also fixed alongside: `_recv_response` ACKed an incoming CON by sending to
`addr` unconditionally, and Windows rejects some source addresses AS
destinations (WinError 10049 — a multicast source, or a scope id naming no
route out). Failing to ACK only makes the peer retransmit; raising killed
the run. Now caught and logged.

---

# OSCORE (`tests/runtime/transport/knx_oscore.py`)

Audited 2026-07-26 against the device's own C implementation in `security/`
(`oc_oscore_crypto.c`, `oc_oscore_context.c`, `oc_oscore_engine.c`,
`messaging/coap/oscore.c`, `api/oc_replay.c`) — note **`security/`, not `api/`**
— plus `traces/5_3.xml`. All findings below are OPEN; nothing has been changed.

## O1 — `seq_no_override` causes AEAD NONCE REUSE · **FIXED**

*(Was rated CRITICAL, then downgraded to HIGH after measuring reachability — see
the table at the end of this entry. The mechanism was verified and real; the
number of steps that could actually trigger it in this template was one, not
six.)*

**Fix:** a new `OscoreContext._reserve_ssn(seq)` advances the counter past a
pinned Partial IV, so no later ordinary send can re-emit it under the same key.
Applied to **`seq_no_override` only**.

Reserving rather than clamping is the key choice: **the transmitted PIV is left
exactly as asked**, because the negative tests assert on the precise wire value.
Verified:

```text
before: normal 00, 01 | override=0 -> 00 (COLLISION) | normal 00 again
after : normal 00, 01 | override=0 -> 00             | normal 02, 03, 04
        asked 100000 -> sent 100000 (ssn now 100001)
        asked 100001 -> sent 100001 (ssn now 100002)
        asked      0 -> sent      0 (counter does not move backwards)
```

### `seq_no_offset` must NOT reserve — tried it, it broke 13 TestCases

Worth recording, because reserving there looks obviously right and is not.

The first attempt applied `_reserve_ssn` to `seq_no_offset` as well. That failed
**5.3.17.1, .3, .4, .5, .6, .7, .8, 5.3.18.1, 5.3.19.1, .2, .5, .6 and
5.3.20.1** — 13 TestCases — with `planned: 2.04, received: 4.01`, cascading
through `@#"Copy of AUTH PREPARATION"`.

The mechanism, reproduced directly:

```text
ssn=5, offset +31  ->  transmits 36, counter left at 6   (correct)
ssn=5, offset +31  ->  transmits 36, counter pushed to 37 (the bad fix)
next ordinary send ->  37, far outside the device's replay window -> 4.01
```

`SeqNoOffset` exists to probe the device's replay **window**: the shifted value
is sent *without* disturbing our own sequence, so the test can jump and then
continue from where the counter really was. The counter must advance by exactly
one, never to the shifted value.

Nonce-reuse safety on that path comes from the offsets themselves. A **negative**
offset replays a PIV the context already used — the point of 5.3.17.x, and not a
live-plaintext collision because the device rejects the message. A **positive**
offset transmits ahead of the counter, which the counter later reaches, but the
intervening sends are exactly the window-probing traffic the test intends and the
device accepts only one of them.

Negative offsets still transmit distinct sub-counter values (`-1,-32,-35,-40,-64`
from ssn 70 give 69, 39, 37, 33, 10 — all distinct), so T6's clamp concern
remains dormant rather than resolved.

The single most severe finding of the whole audit, verified by execution, not
inspection:

```text
normal sends      -> piv 00, 01     ssn=2
protect_request(seq_no_override=0) -> piv 00,  ssn still 2
nonce(normal #1) == nonce(override) ?  True   6555b3cf06b76410b20172b3db
ciphertexts differ (different plaintext)? True
```

The override path pins the PIV **without advancing `self.ssn`** (documented as
deliberate). The nonce is a pure function of `(sender_id, piv, common_iv)`, so a
pinned value that a normal send also emits gives a **byte-identical nonce under
an unchanged `sender_key`, on different plaintext**.

For AES-CCM (CTR-based) that leaks the XOR of the two plaintexts and breaks
CBC-MAC forgery resistance. Inner CoAP messages are highly structured, so
recovery would be near-total. `seq_no_offset` has the same hazard by
construction: a `-1` offset after a `+31` retransmits a PIV already consumed.

**This is not an intentional deviation — it is the opposite of what the device
does.** `oc_oscore_engine.c` always increments the SSN for outbound requests
except on CON retransmissions (where the plaintext is identical, the one safe
case), and `oc_oscore_context.c` additionally bumps the SSN on restore
specifically to prevent reuse per RFC 8613 Appendix B.1.1.

Bounded in practice — it is a test tool on a lab network — but 5.3.17.x exists
precisely to model a passive observer. **Suggested fix: derive a fresh context
(fresh master secret ⇒ fresh `common_iv`/`sender_key`) per adversarial replay
test, so a repeated PIV lands under a different key.** Supersedes T5, which
described the same defect less precisely.

### Reachability, measured (this is why the suite still passes)

`SeqNoFn="fix"` has only **6 active steps**, and their pinned PIVs are:

| TestCase | pinned `SeqNo` | collides with a normal SSN? |
| --- | --- | --- |
| 5.3.18.1 (×5) | 100000 / 100001 | **No** — the largest TestCase sends 145 protected requests, so the SSN never approaches 100000 |
| 5.3.17.8 | **0** | **Yes** — 0 is squarely in the reachable range |

So the *mechanism* is real and verified, but in this template it can only fire
via the single `SeqNo=0` step in 5.3.17.8. The 5.3.18.1 uses — the ones the
original write-up leaned on — are out of range by five orders of magnitude and
cannot collide. That is a materially smaller blast radius than "critical"
suggests: **downgraded from CRITICAL to HIGH**, one reachable step rather than
six. Still worth fixing (a template edit or a longer run changes the arithmetic,
and the fix is cheap), but it is not currently corrupting results.

## O2 — `_build_nonce` corrupts the nonce for over-long sender IDs · **FIXED**

`id_offset = NONCE_LEN - 5 - len(sender_id)` goes negative for `len >= 9`, and
Python slice assignment **grows the bytearray** instead of raising:

```text
id=8  -> 13 bytes (correct)
id=9  -> 18 bytes (!)
id=12 -> 25 bytes
```

The device caps IDs at `OSCORE_SENDER_ID_LEN = 7` because the equivalent
`memcpy` would be a stack buffer underflow. Unreachable today (every ID in the
suite is ≤ 7 bytes) but `provisioning._hex_or_ascii` will produce an arbitrary
length from an XML `senderid=` and nothing validates it. Fails loudly today only
by luck (`AESCCM` rejects a >13-byte nonce).

**Fix:** `_build_nonce` now rejects an ID longer than 7 bytes (the layout is
`1 + 7 + 5 = 13`, and the same cap the device enforces) and a PIV longer than 5,
instead of letting Python's slice assignment silently grow the bytearray.
Verified: lengths 0–7 still yield a 13-byte nonce; 8, 9 and 12 now raise.

Note the original report said "> 8 bytes" — **8 is broken too**, just less
visibly: the ID overwrites the length byte, which is then re-set, silently losing
the ID's first byte. The check covers it.

## O3 — `unprotect_response` tries BOTH key constructions and accepts either · **PARTIALLY FIXED**

The trial-decrypt builds up to two `(nonce, aad, key)` candidates and treats
`InvalidTag` as "wrong guess". The discriminator only **reorders** them; both are
always tried. So if the device regressed and signed an ordinary unicast 2.xx
with its empty-id sender key, the suite would keep passing silently.

**Same shape as the CoAP defect this whole review started from**: a permissive
"try both and see" in a place where the device is strictly deterministic
(`oc_oscore_engine.c` picks exactly one construction from `piv_len > 0`, with no
fallback). `except Exception` is also too broad — it swallows `TypeError`/
`ValueError` from a malformed option alongside `InvalidTag`. See the existing
memory note *"unprotect_response is a minefield"*; the empirical comments in the
code are honest about how the current ordering was arrived at.

### Fixed half: the exception is now narrow

`except Exception` treated **every** failure as "wrong candidate, try the next".
Verified that the two are genuinely different:

```text
auth failure      -> InvalidTag   (a wrong key guess — legitimate, keep trying)
bad nonce length  -> ValueError   (OUR bug — must not be swallowed)
```

Swallowing the second reported our own defect as *"the device sent something we
could not decrypt"*, pointing the next reader at a device fault that does not
exist. Now only `InvalidTag` advances to the next candidate. Verified: a tampered
ciphertext still exhausts all candidates and raises `InvalidTag`; an injected
`ValueError` propagates instead of being disguised.

The same over-broad catch on the **group-telegram** path in `coap_client.py` is
narrowed to `(InvalidTag, IndexError, ValueError)` — deliberately wider than the
response path, because that one is multicast, telegrams for *other* contexts
genuinely arrive there, and `unprotect_request` indexes into the OSCORE option
before reaching the AEAD, so a foreign layout can raise `IndexError`/`ValueError`
long before `InvalidTag`. Still narrowed enough that a `TypeError`/
`AttributeError` in our own code surfaces rather than masquerading as a foreign
telegram.

### Still open: both candidates are still tried

The structural half is unchanged — a device that regressed to signing an ordinary
unicast 2.xx with its empty-id sender key would still be accepted. Fixing that
means picking exactly one construction the way the device does (`piv_len > 0`),
and the code's own comments record that every attempt to gate it so far
regressed real tests: *"Gating on `resp_has_own_piv` alone regressed 22
observe/group-comm tests; on `recipient_id` alone, 7; baseline-first
unconditionally, 4 of 5.3."* Needs the packet-capture oracle
(`eitt-packet-capture-plan.md`) to determine empirically which construction the
device actually uses per case, rather than another guess.

## O4 — Empty id_context derives DIFFERENT keys from the device · **FIXED**

`_build_info` maps `b""` → CBOR empty bstr `0x40`; the device branches on
**length** and maps both `NULL` and `b""` → `null` `0xf6`:

```text
py None : ...f6...   matches device
py b""  : ...40...   DIVERGES -> different HKDF info -> different key
```

`provisioning` returns `None` when the attribute is absent, but an XML
`contextid=""` takes the hex branch and yields `b""` — silently deriving the
wrong keys.

**Fix:** `if id_context else None` rather than `is not None`, so empty and
absent collapse to the same thing exactly as the device does. Re-verified the
device side first (`oc_oscore_context.c`: `if (id_ctx_len > 0)
encode_byte_string else encode_null`). Confirmed by execution:

```text
None (absent) -> 8546527454657374 f6 0a634b657910
b""  (empty)  -> 8546527454657374 f6 0a634b657910   <- now identical
b"\x01\x02"   -> 8546527454657374 4201020a634b657910 <- real ctx unaffected
```

Latent rather than active — no current template step sets `contextid=""` — but
it would have been a silent wrong-key derivation, which is the hardest class of
defect to diagnose from a test failure. Verified: 5.3 + 5.4 = 60 passed.

## O5 — No replay window on receive · **WONTFIX (as rejection) → moved to capture plan (as detection)**

The device maintains a 32-bit sliding window plus an echo whitelist
(`oc_replay.c`); the client keeps `self.ssn` for **sending only** and performs no
inbound replay check. Largely mitigated for request/response by token matching
in `_recv_oscore_response`, but `unprotect_request` — the **multicast** path, the
injectable one — has no protection at all. Supersedes/absorbs T7.

**Re-examined 2026-07-27 before implementing, and the fix is the wrong one.**
A receive-side replay window would produce false failures rather than catch
defects, for three reasons:

1. **No adversary exists on this link.** `unprotect_request` has exactly ONE
   caller — `_decrypt_group_telegram` in `coap_client.py` — on the multicast
   receive path. The DUT is the only other node in the netns (see
   `eitt-packet-capture-plan.md`: `veth-test` is the sole link), so there is no
   third party able to inject a replay. The window would defend against a threat
   the topology excludes.
2. **The suite deliberately replays sequence numbers.** `SeqNoOffset` (138
   active steps) and `seq_no_override` exist to transmit PIVs out of order.
   Inbound replay state would be a second mechanism fighting the one the tests
   require — and O1 already demonstrated the cost of that collision: applying
   reservation to `seq_no_offset` broke 13 TestCases across 5.3.17–5.3.20.
3. **Rejection destroys evidence.** If the DUT genuinely replayed a telegram,
   the useful tool behaviour is to SURFACE it so the step's assertion fails
   visibly. Dropping it silently turns a real device defect into a misleading
   "no telegram received" timeout — the same false-negative shape as the
   original F1.

The device needs a replay window because it faces a network; this tool is the
*adversary* in these tests, and giving it the victim's defences works against
its purpose.

**What is genuinely worth having is detection, not rejection**, and it belongs
in the packet-capture oracle where a duplicate `(kid, PIV)` pair is reported as
a finding instead of a dropped packet. Recorded as a capture-layer check in
`eitt-packet-capture-plan.md`. Re-open here only if the topology gains a third
node.

## OSCORE: intentional KNX deviations — CORRECT, do not "fix"

Each verified byte-for-byte against the C:

- **AAD always reuses the request's kid+PIV on responses** — matches
  `oc_oscore_engine.c` ("AAD use always request kid/PIV (8.4 step 3)").
- **Nonce uses the response's own PIV when present, the request's otherwise** —
  mirrors the device's `piv_len > 0` dispatch.
- **Empty-sender-ID key for group-AT device-originated messages** — the device
  really does create a temp sender context with an unset (empty) sender ID for
  the multicast echo response.
- **10-byte `kid_context` as the echo marker** — matches
  `OC_ECHO_KID_CTX_LEN (10)`.
- **AAD structure** `["Encrypt0", b"", cbor([1,[10],kid,piv,b""])]`,
  **`AEAD_ALG=10 / TAG_LEN=8 / KEY_LEN=16 / NONCE_LEN=13`**, **PIV
  minimum-length big-endian with the SSN==0 → single `0x00` case**, and the
  **OSCORE option layout/ordering** — all byte-identical to the device.

---

# SPAKE2+ (`tests/runtime/transport/knx_spake2plus.py`)

**In good shape.** The two failure modes I was most worried about are both
absent.

## S1 — `shareV` is never validated on-curve; infinity crashes · **FIXED (2026-07-27)**

`process_key_exchange_response` feeds `resp[11]` straight into
`_p256_point_sub`, which does raw integer arithmetic with **no curve-equation
check and no length check**. Validation happens only incidentally one step later,
when `_p256_scalar_mult` calls `from_encoded_point`.

An invalid-curve point *is* therefore caught — but by accident, at the wrong
layer, surfacing as a confusing `ValueError: Invalid EC key`. And
`shareV = w0*N` makes `base` the point at infinity, where `_p256_point_add`
returns `(None, None)` and the caller immediately does `rx.to_bytes(...)` →
`AttributeError`. Verified by executing `_p256_point_sub(M_POINT, M_POINT)`.

The device is explicitly more defensive here: `spake2plus.c` parses `shareP`
first specifically to reject the zero point. **P-256 has cofactor 1**, so there
is no small-subgroup attack and the absence of cofactor handling is correct, not
an omission.

**Fix — mirrors the device's structure rather than inventing one.**
`spake2plus.c` validates the peer's share *up front, before any arithmetic*
("Verify shareP is not the point at infinity by attempting to parse it", then
`calculate_ZV`). Two guards, because the three bad inputs fail at two different
places:

- **`_validate_peer_point(shareV, "shareV")`** at the top of
  `process_key_exchange_response`, before any arithmetic. Checks length and the
  `0x04` prefix, then round-trips through `_p256_point_from_uncompressed`
  (`from_encoded_point`) — the direct analogue of the device's
  `ecp_point_from_bytes`, rejecting off-curve and zero points alike.
- **`_p256_point_add` now raises on `(None, None)`** instead of calling
  `to_bytes` on it. The infinity case (`shareV == w0*N`) **cannot** be caught by
  the pre-check — whether the subtraction cancels depends on `w0`, which is
  derived from the password — and it crashes *inside* the helper, so a
  caller-side `if base is None` would have been dead code. Verified that by
  execution before moving the guard. `process_key_exchange_response` re-raises
  with context naming the protocol condition.

Verified all seven cases by execution:

| input | result |
| --- | --- |
| truncated (`04 01 02`) | `shareV is not a 65-byte uncompressed P-256 point (got 3 bytes...)` |
| empty | `shareV is empty` |
| off-curve, right length | `shareV is not a valid P-256 point: Invalid EC key.` |
| compressed prefix `0x02` | rejected on the prefix check |
| infinity (`P - P`) | `P-256 point addition produced the point at infinity` |
| **valid `M_POINT`/`N_POINT`** | **accepted** |
| **valid `M - N`** | **65 bytes, unchanged** |

The last two matter as much as the rejections: a validator that also rejects
good points would break every SPAKE2+ handshake in 5.3.

## S2 — Non-constant-time scalar multiplication · **FIXED (2026-07-27) · INFO**

`_ec_scalar_mult` is a textbook `if k & 1` double-and-add operating on the
long-term secret `w1` and the ephemeral `x_priv` — a classic timing side
channel. Irrelevant for a Python test client on a lab network, but this code
**must never be reused as production code**; wants a docstring warning.

**Fix: the docstring warning, as specified — no algorithm change.** Making this
constant-time would be the wrong call: it buys nothing on an isolated lab link
against a DUT that exposes no timing oracle, and a rewrite of working
crypto-adjacent arithmetic is exactly the kind of change that introduces a
subtle correctness bug into all of 5.3. The warning states that it is
test-client-only, why the exposure is acceptable here (the modelled attacker is
the DUT), and that the device's own scalar multiplication is mbedTLS and IS
hardened — so nobody mistakes this for a reimplementation of it.

## SPAKE2+: verified CORRECT

- **`confirmV` IS verified**, with `hmac.compare_digest` (constant-time), and
  raises on mismatch. This is the check that authenticates the *device* — had it
  been merely parsed, every security test would have been theatre.
- **Transcript order and length-prefixing** — `Context ‖ idProver ‖ idVerifier ‖
  M ‖ N ‖ shareP ‖ shareV ‖ Z ‖ V ‖ w0`, every element length-prefixed, matching
  the C element-for-element. **The classic SPAKE2+ transcript bug is NOT
  present.**
- **M and N** independently derived from the RFC 9383 compressed values and
  matched byte-for-byte against the device's `bytes_M`/`bytes_N`.
- **P-256**, **PBKDF2** (SHA-256, 80-byte output split 40/40, each reduced mod
  the group order; iterations and salt come from the device's step-1 response so
  no hardcoded mismatch is possible), **shareP = X + w0·M**,
  **Z = x·(shareV − w0·N)**, **V = w1·(shareV − w0·N)**, and the **key schedule**
  (`HKDF(K_main, "ConfirmationKeys", 64)` → KcA‖KcB;
  `HKDF(K_main, "SharedKey", 16)` → OSCORE master secret) — all match the device.
- **`_encode_w0_mpi` stripping leading zeros** and **little-endian uint64 length
  prefixes** are intentional KNX/mbedTLS-compatibility deviations, documented in
  the C, and correct.

---

# mDNS / DNS-SD (`tests/runtime/transport/mdns_client.py` + `runner._run_dns_message`)

Audited 2026-07-26. **The three worst findings are FALSE PASSES** — this module
is more likely to say the device is fine when it isn't than the reverse. That is
the opposite of the CoAP layer, where the dangerous defects were false fails.

## M1 — A truncated AAAA aborted parsing and DISCARDED every record · **FIXED**

`parse_records` checked the CLAIMED `rdlen == 16`, not `len(rdata)`. Python
slicing silently returns fewer bytes at end-of-packet, so `inet_ntop` raised
`ValueError`. Verified:

```text
AAAA rdlen=16 but 3 bytes present -> ValueError: invalid length of packed IP address string
```

That propagated out of `_listen`, was stored in `self._error`, and `records()`
**re-raised it instead of returning the records already captured**. One
malformed AAAA anywhere in a packet turned a fully successful discovery into a
failure — and the DUT's answers are exactly the packets that carry AAAA.

**Fix:** validate `len(rdata) == rdlen` and stop on mismatch; match AAAA on the
actual length.

## M2 — Any parse error discards the whole result set · **FIXED (2026-07-27)**

Compounds M1. `records()` raises `self._error` before returning `self._records`.
The same slot holds *setup* errors (where raising is right) and *per-packet
parse* errors (where it is not). A malformed packet from an unrelated host on
the link — a printer, another test rig, any `_companion-link` browser, all of
which multicast to `ff02::fb` and none of which this listener filters out —
destroys a good result set and reports it as a device failure.
**Fix:** per-packet try/except so a bad packet is skipped, not fatal.

**Fix applied as described** — `parse_records(data)` now sits in its own
try/except inside the read loop; an unparseable packet is logged and `continue`d
instead of escaping to the outer handler. The `while` re-tests the deadline, so
skipping cannot strand the loop.

Verified by execution: with `parse_records` stubbed to raise on a `GARBAGE`
packet, a listener that receives the bad packet **and then** a valid PTR returns
that PTR (`1 record`) rather than raising. The *setup* half of `_error` still
raises — see M20, which turned out to be broken for a different reason.

**Caught types are narrowed, not bare `Exception`** (same reasoning as O3/T1):
`struct.error, ValueError, IndexError, UnicodeDecodeError` — what a malformed
*datagram* can actually produce. A `TypeError`/`AttributeError` means OUR parser
is broken and must not be silently reclassified as foreign traffic.
`RecursionError` is deliberately absent: `decode_dns_name` already breaks
compression-pointer loops with its `seen` set.

Worth knowing: `parse_records` is defensive enough that hand-made malformed
datagrams (short header, all-`0xff`, truncated RR) are absorbed by its own bounds
checks and return `[]` instead of raising. **This guard is therefore a safety net
for inputs that are hard to construct deliberately, not a hot path** — which is
what it should be. The stub test above is what actually proves the behaviour.

## M20 — `socket()` outside the `try`: listener death is invisible · **FIXED (2026-07-27)**

Found while verifying M2's "setup errors must still raise" half — **not caused
by the M2 change; confirmed present at `0901ba60` by stashing.**

`_listen` constructed its socket on the line *before* `try:`, so an `OSError`
from `socket()` itself escaped `_listen` entirely. Three consequences, all
measured:

- `self._error` stayed `None`, so `records()` returned an innocent `[]` — the
  exact "empty result set with no hint why" that `_error` was introduced to
  prevent.
- `self._ready` was never set, so **every** construction burned the full
  `_ready.wait(timeout=2.0)`.
- The `finally` block referenced an unbound `sock`, so cleanup would itself
  raise `UnboundLocalError`.

**Fix:** `sock = None` before the `try`, construct inside it, and guard the
`finally` with `if sock is not None`. Verified: the error is now recorded and
re-raised by `records()`, and the constructor returns in **0.15s instead of
~2.15s**. Normal path re-checked and unaffected (1 PTR captured).

## M3 — Stale record cache satisfies a later assertion · **FIXED**

`runner._dns_records` is initialised once per runner, overwritten on every IN
query, and cleared **only** inside `_arm_unsolicited_dns`. Never between
TestCases, never between an OUT step and the next.

So an OUT step for an unsolicited announcement (5.1.2.5a/b) with a populated
cache **never listens at all** — `if not self._dns_records:` is False and it
matches straight out of the stale set. If an earlier query already drew a
PTR/SRV/AAAA for the same service, the step passes **without the device ever
having announced anything**. That is a false pass on precisely the state-change
assertions the `await_live` machinery exists to get right.

### Confirmed reachable, with the exact pair

Unlike M5, this one survived scrutiny. Only **two** TestCases open with an OUT
DnsMessage and therefore depend on the unsolicited-listen branch — **5.1.2.5a**
and **5.1.2.5b** — and in document order they are immediately preceded by:

```text
39  5.1.2.4.  Discovery using programming mode        <- queries _pm._sub...
40  5.1.2.5a. Unsolicited mDNS responses on pm change <- asserts _pm._sub...
41  5.1.2.5b. Unsolicited mDNS responses on ia change
```

5.1.2.4 queries **the same service** 5.1.2.5a then asserts on, so its answer was
sitting in the cache when 5.1.2.5a ran. `if not self._dns_records:` was therefore
False, the unsolicited branch never listened, and the step matched out of the
previous TestCase's answer — a pass with the device never having announced.
Masked in practice because `_arm_unsolicited_dns` clears the cache when it
succeeds; the hole is every path where arming does not happen.

**Fix:** `run()` clears `_dns_records` and `_dns_query_name` at the TestCase
boundary, alongside the existing "one failure must not corrupt its successors"
resets. Guarded to the **outermost** call only — `run()` recurses for an
`@#"..."` inline preparation, and clearing on the inner call would discard an
answer the enclosing TestCase legitimately cached. (`_sequence` starts `[]` and
is non-empty only while a TestCase is running, so its falsiness is the
outermost test.)

Verified: 5.1 = 42 passed. **This also fixes M4** — `_dns_query_name` is cleared
by the same change.

## M4 — `_dns_query_name` is never reset either · **FIXED (with M3)**

The `rqcheck` pairing check compares against the last query name from *any*
earlier step, including a previous TestCase. A step whose service happens to
equal a long-stale query name passes a check it should have failed.

## M5 — Goodbye records satisfy liveness assertions · **NOT A DEFECT (disproved)**

**The finding was wrong. Implemented, tested, disproved by the reference trace,
reverted.** Recorded in full because the reasoning is seductive and someone will
propose it again.

### The claim

`_match_dns_record` filters on `rtype`/`name`/`target` and never reads
`rec.ttl`. A ttl=0 record is a GOODBYE (RFC 6762 §10.1) — the device
*withdrawing* an advertisement — so it should not satisfy a step asserting the
record EXISTS. And since a state change emits the goodbye FIRST and the announce
after, matching in arrival order picks the goodbye, apparently defeating
`await_live` at the matching layer.

### Why it is wrong

**5.1.2.5a uses the same step shape for both directions of a state change:**

```text
PUT dev/pm {"value": false}   ->  expect PTR _pm._sub...   <- goodbye is CORRECT here
PUT dev/pm {"value": true}    ->  expect PTR _pm._sub...   <- announce is correct here
```

Step 1 *disables* programming mode, so the device withdrawing `_pm._sub` is
exactly the right answer. Preferring live records made that step fail:

```text
expected an mDNS PTR record for '_pm._sub._knx._udp.local.' but the only
match was a GOODBYE (ttl=0)
```

**Decisive evidence:** in `traces/5_1.xml` **every** `DnsMessage` row carries
`TTL="0"`, including the ones EITT evaluated `OK`. EITT does not discriminate on
TTL here at all — the attribute is a display default in the trace format, not an
observed wire value. There is also no `TTL` attribute on any of the 41 template
`DnsMessage` steps, so the script never expresses an expectation about it.

### What this means for the related findings

- **M6** (`await_live` missing on the fallback listener) is still real, but its
  severity drops: `await_live` governs whether the listener keeps *reading* past
  a goodbye, which matters for capturing the announce in Step 2. It was never
  being "defeated at the matching layer", because there is no TTL-based matching
  to defeat.
- The claim that this was a **false pass** is withdrawn. Matching a goodbye is
  correct behaviour for half of 5.1.2.5a.

A defensible future refinement would key on the step's *intent* — a step
following `pm=false` wants the withdrawal, one following `pm=true` wants the
announce — but the template gives no signal for that, and inventing one would
assert something EITT does not.

## M6 — `await_live` missing on the fallback listener · **FIXED**

Only the pre-armed listener in `_arm_unsolicited_dns` passes `await_live=True`.
The fallback `MdnsListener(timeout=_MDNS_TIMEOUT)` and `query()` do not. Whenever
arming fails (several early-return paths), the listener stops at the **first**
packet — the goodbye — and never reads the announce that follows it. **The exact
historical bug, still live on the fallback path.**

This is about *capturing*, not matching: `await_live` governs whether the
listener keeps reading past a goodbye, which is what 5.1.2.5a **Step 2**
(`pm=true`, expecting the announce) depends on. Failure mode is a **false fail** —
the announce simply never enters the record set.

Note this is the surviving half of the concern originally filed as M5. M5's
*matching*-layer claim was disproved (see its entry); this capture-layer gap is
real and independent of it.

**Fix:** the fallback `MdnsListener` in `_run_dns_message` now passes
`await_live=True`, matching the armed one. Both are listening for the *same*
thing — an unsolicited announcement after a state change — so both need to read
past the goodbye.

Checked before changing: the read loop is **deadline-bounded**
(`while time.monotonic() < deadline`), so a goodbye-only case still terminates on
timeout rather than hanging. The only cost is that such a case now waits out the
full `_MDNS_TIMEOUT` instead of returning early with the wrong records — the
right trade. Verified: 5.1 = 42 passed, no timing regression.

**Not changed:** the module-level `mdns_client.query()` helper also lacks
`await_live`, but it has **no callers** — dead code predating `repeat_query`.
Left alone rather than modified blind; delete it if it stays unused.

## M7 — An IN step that captured NOTHING returns ok=True · **OPEN**

`runner._run_dns_message`'s IN branch returns success unconditionally. The
failure is deferred to the following OUT step — so an IN with no following OUT,
or whose OUT is deactivated, makes a **completely unanswered query a silent
pass**.

## M8 — Timeout is indistinguishable from "answered with nothing" · **OPEN**

The receive loop exits identically whether the deadline expired with zero
packets or the break fired. `records()` returns `[]` either way; there is no
"did we ever receive a packet" signal. The resulting message reads as a device
failure but is equally consistent with a join failure, a wrong-interface send
(M14), a dropped query (M12), or M1/M2 having discarded a good result.

## M9 — Compression pointers: no hang, but names truncate silently · **OPEN**

Good news: the `seen` set **does** terminate mutual pointer loops (verified — no
hang, no expansion bomb, since `seen` tracks absolute offsets). The bad news is
*how*: every failure path is a bare `break` returning the labels accumulated so
far with no error signal. A loop, an out-of-bounds pointer, or a label running
off the end all yield a plausible short name; `decode_dns_name(bytes([3,0x61]))`
returns the empty name. Downstream the record simply fails to match and the step
blames the device — a false fail with a misleading cause.

## M10 — RDLENGTH not honoured as a record boundary for PTR/SRV · **PARTIALLY FIXED**

M1's fix rejects rdlen **overruns**. Still open: PTR and SRV ignore `rdata`
entirely and call `decode_dns_name(data, off)` / `(data, off+6)`, which read the
**whole packet** past the record boundary. A PTR with a too-small rdlen reads
into the next record's bytes and fabricates a target with no error. Verified:
`PTR rdlen-short -> target='i\x01y\x05'`. `SRV` with `rdlen == 6` and no target
likewise reads whatever follows.

## M11 — TXT parsing: overrun silently empties, repeated keys backwards · **OPEN**

No check that `i+n <= len(rdata)`; on overrun the slice truncates, the `=` test
fails, and the pair is dropped — yielding `{}`, indistinguishable from a
genuinely empty TXT. Repeated keys are last-wins; RFC 6763 §6.4 says **first**
wins. Valueless/boolean keys (legal per §6.4) are dropped rather than stored.

Reachability: `dns-sd_mdns.c` only ever emits a single `SP=<n>`, so repeated and
valueless keys are unreachable against this DUT; the overrun path is reachable
from any foreign packet. **Note also that `_match_dns_record` never reads
`DnsRecord.txt` at all — no OUT step asserts on TXT content, so the `SP` record
and its goodbye are effectively untested.**

## M12 — Header flags ignored; a foreign QUERY is parsed as a response · **OPEN**

`_, _, qd, an, ns, ar = ...` discards ID and FLAGS, so the **QR bit is never
checked**. RFC 6762 §7.1 known-answer suppression puts real answer records in
*query* packets. Verified: a foreign KAS query yields a parsed
`PTR _knx._udp.local -> inst._knx._udp.local ttl=4500`. Since the source address
is never checked either, **another device's records can satisfy an assertion
about the DUT**. Contained on an isolated veth namespace; a live false pass on a
shared lab network.

## M13 — QU bit never set; retries paper over the resulting delay · **OPEN**

QCLASS is always `0x0001`, so every query is a QM query and the responder may
delay 20–120 ms and apply rate limiting / known-answer suppression. This is the
root cause of the 6 s timeout and 0.75 s retry loop this module needs to be
reliable. Three identical QM queries in 3 s is itself what §7.1 damping targets.
Setting QU (`0x8001`) on the first query would let the DUT answer immediately by
unicast — the listener is bound to `("", 5353)` and would receive it.

## M14 — Silent degradation to a single wildcard join · **OPEN**

`_iface_index` swallows `OSError` from `if_nametoindex` and returns 0, so a
typo'd or unset `DEVICE_IFACE` collapses the join list to `[0]` alone — **the
single-interface configuration the historical silent-empty bug was about** — with
no warning. See the memory note about joining wildcard index 0 *as well as* the
named interface.

## M15 — No `IPV6_MULTICAST_IF` when idx == 0 · **OPEN**

`if idx:` skips the setsockopt and the sendto scope-id is 0, so on a multi-homed
host the kernel picks the route for `ff02::fb` itself — frequently not the test
interface. Combined with M14, a mistyped `DEVICE_IFACE` sends queries out the
wrong NIC while listening on all of them: every 5.1 step reports "received
nothing".

## M16 — Unknown `RecordType` silently falls back to PTR · **OPEN**

`_TYPE_BY_NAME.get(rtype.upper(), DNS_TYPE_PTR)`. An XML `RecordType` this
module does not know (`ANY`, `NSEC`, a typo) sends a **PTR** query; the OUT step
then fails with a misleading message pointing at the device. Contradicts the
runner's stated "deliberately loud, never a silent no-op" policy — should raise.

## M17 — `_ready` is set even when the listener failed to bind · **OPEN**

`_ready` is set in the `finally`, so a listener that failed to bind returns
looking healthy and the error only surfaces at `records()`. In between,
`repeat_query` pumps queries into the void.

## M18 — Fixed 0.15 s sleep as the only join barrier · **OPEN**

A guess at MLD report propagation with no verification that the join took
effect. Given this module's documented history, the most likely residual source
of intermittent empty results.

## M19 — TTL values are never validated · **OPEN**

Nothing checks the DUT's announce TTLs against RFC 6762 §10 (120 s host records,
75 min shared). `traces/5_1.xml` records `TTL="0"` on every element including
live ones, so the traces do not discriminate either — TTL correctness is
untested end to end.

## mDNS: checked and NOT defects

- Compression-pointer infinite loop — **not present**; `seen` terminates it, and
  there is no quadratic expansion bomb.
- Lying QDCOUNT / huge ANCOUNT (65535) with a short packet — degrades to `[]`,
  no crash, no DoS. Verified.
- Non-ASCII labels — `errors="replace"`, no crash.
- Cache-flush bit (class `0x8001`) — `_rclass` discarded, record parses
  normally. Correct for this use; the DUT does set it.
- Collapsing AN/NS/AR into one loop — **correct and necessary here**:
  `dns-sd_mdns.c` puts SRV/AAAA/subtype-PTR/TXT in the additional section.
- `IPV6_MULTICAST_HOPS 255` — correct per RFC 6762 §11.
- Header ID 0 and flags 0 on queries — correct per RFC 6762 §18.1.

---

## Deliberately NOT findings

Checked and clean — do not "fix" these:

- **No hardcoded section/case/GUID knowledge in engine code.** Confirmed
  independently by two audits. Section numbers appear only as hook *function
  names* (`eval_5_2_4_1_step_1`), which the XML's own `EvalFn` attribute names
  1:1, and in explanatory comments citing trace evidence.
- **`overrides.yaml` is legitimate** — it models the human operator's
  configuration, and it is well policed: `apply_message_activation` and
  `_check_overrides_match_real_cases` both hard-error on stale keys.
- **`provisioning.apply` and `hooks.get_hook` raise on unknown input.** Correct.
- **`match_content` on an empty `<Content>` asserts an EMPTY BODY** rather than
  skipping, and fails when the body cannot be decoded. That strictness caught
  two real bugs (5.8's missing `rt`, 5.10.5.2's empty `<Cbor>` body).
- **`Type` (CON/NON) is genuinely honoured** — per-telegram, with real NON
  frames on the multicast, OSCORE and echo-challenge paths.
- The `except Exception` blocks in `hooks.py` return `False` (failure), not
  `True`, and faithfully mirror the original EITT script.

## Known latent trap

`transport/coap_client.py` `_build_request` hardcodes `CON << 4`, ignoring the
caller's type. Harmless today — all 25 unsecured NON requests are either
multicast (a different builder) or the single echo-challenge response (a third
builder, correctly NON) — but a future unsecured unicast NON would be silently
sent as CON.
