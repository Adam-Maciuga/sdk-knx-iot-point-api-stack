# Proposed Runtime (EITT-style) Tests — CoAP Observe / Notifications

> **Status:** Draft for discussion. No implementation yet — this captures *what*
> to test and the *test logic*, so we can agree on scope before writing code.
>

---

## 1. Why this document

There are currently **no EITT / runtime tests** for CoAP Observe in
`tests/runtime/`. The stack recently gained / reworked the observe path:

- `coap_notify_k_observers()` — S-Mode `/k` notification fan-out
- `coap_notify_observers()` — `/p` and core-resource notifications
- observe register / deregister refactor (lifetime `lt`, non-confirmable `non`)
- OSCORE AAD-for-observe fix (notification AAD must include the Observe option)

These are covered at the **unit** level (`tests/unit/test_coap_observe.cpp`,
`test_oc_oscore_crypto.cpp`), but the **wire behaviour** a certifier would see is
untested. This proposal defines that runtime layer.

---

## 2. Spec anchors

| Clause | Topic |
|--------|-------|
| **2.6.10.1** | Subscriptions — CoAP observe is the default mechanism; `if.g.s` / `if.o` Points SHALL support it; re-subscribe with a **new token**; server replaces a subscription by **source IP+port**, *not* by token |
| **2.5.9.1** | `/k` S-Mode notification resource (`rt=urn:knx:if.g.s`); first notification = `{ "sia": <ia> }` only; later notifications include the `s` object with `st="w"` |
| **2.5.9.3** | `lt` lifetime for `/k`; missing → `4.00 Bad Request`; device must support values up to at least 86400 s |
| **2.5.11.6** | `lt` lifetime for parameter/diagnostic Points; same rules |
| **2.5.9.4** | `non` query parameter; default is **confirmable** (`non=false`) |

> **Open question for the test spec:** the EITT test spec (`08_10_5 KNX IoT
> Point API Tests`) doesn't appear to number an "observe" section yet. We need to
> agree whether these become new `5.9.x` IDs or fold under an existing section.
> The IDs below are **placeholders** (`OBS-x`).

---

## 3. Prerequisite (blocker, needs sign-off)

`coap_client.py` has **no observe capability** today. To run any of these tests
the client must be able to:

1. Send a `GET` with the **Observe option = 0** (register) and **= 1** (deregister).
2. Keep the UDP socket open and **collect asynchronous notifications**.
3. **ACK confirmable (CON)** notifications and tolerate **non-confirmable (NON)**.
4. Track the **Observe sequence number** and **token** per subscription.

This touches an **infrastructure file**, so it should be agreed/approved before
implementation. Everything below assumes this capability exists.

---

## 4. Proposed test file

`tests/runtime/test_5_9_observe.py` (sibling of `test_5_4_group_comm.py`), with an
EITT-style docstring header listing each scenario and its spec clause.

---

## 5. Test logic

Each test below lists: **goal**, **setup**, **stimulus**, **expected**. They are
intentionally behaviour-only (response codes, payload shape, CON/NON, encryption).

### Group A — Subscription lifecycle on a `/p` datapoint

#### OBS-A1 · Register observe returns current value
- **Goal:** A valid observe registration succeeds and yields the first notification.
- **Setup:** Device loaded; datapoint `/p/1` has a known value.
- **Stimulus:** `GET /p/1` with `Observe=0`, `lt=86400`.
- **Expected:** `2.05 Content`; response carries the **Observe option** and the
  current datapoint value as the first notification.

#### OBS-A2 · Value change triggers a notification
- **Goal:** A registered observer is notified when the value changes.
- **Setup:** OBS-A1 active.
- **Stimulus:** Change `/p/1` (PUT, or an inbound s-mode write).
- **Expected:** Observer receives a follow-up notification with the new value and
  an **incremented Observe sequence number**.

#### OBS-A3 · Deregister stops notifications
- **Goal:** Explicit deregistration ends the relationship.
- **Setup:** OBS-A1/A2 active.
- **Stimulus:** `GET /p/1` with `Observe=1` (same token), then change the value.
- **Expected:** Deregister acknowledged; **no further notifications** arrive.

#### OBS-A3b · `DELETE /sub` removes subscription
- **Goal:** `DELETE /sub` is a third deregistration path distinct from `Observe=1`.
- **Setup:** OBS-A1/A2 active.
- **Stimulus:** `DELETE /sub` (unicast, same security context), then change the value.
- **Expected:** `2.02 Deleted`; **no further notifications** arrive.

#### OBS-A4 · Re-subscribe replaces by IP+port, not token
- **Goal:** A new subscription from the same client replaces the old one (2.6.10.1).
- **Setup:** OBS-A1 active from a given source IP+port.
- **Stimulus:** Register again from the **same IP+port** with a **new token**.
- **Expected:** Exactly **one** active subscription remains; a single value change
  produces a **single** notification (no duplicate from the stale subscription).

### Group B — Lifetime (`lt`)

#### OBS-B1 · Missing `lt` is rejected
- **Stimulus:** `GET /p/1` with `Observe=0` and **no** `lt`.
- **Expected:** `4.00 Bad Request` (2.5.9.3 / 2.5.11.6).

#### OBS-B2 · `lt=0` is rejected
- **Stimulus:** `GET /p/1` with `Observe=0` and `lt=0`.
- **Expected:** `4.00 Bad Request`.

#### TBD OBS-B3 · Lifetime expiry stops notifications *(slow / optional)*
- **Goal:** Device stops notifying after `lt` elapses.
- **Note:** The spec requires the device to support `lt` up to at least 86400 s,
  but small values (e.g. `lt=30`) are also valid. The EITT sequence uses `lt=30`
  for this test rather than waiting 24 h. Mark **slow** / CI-skipped in automated
  runs, or use `lt=30` and accept the short wait.

### Group C — Confirmable vs non-confirmable (`non`)

#### OBS-C1 · Default notifications are confirmable (CON)
- **Stimulus:** Register observe **without** `non`; trigger a change.
- **Expected:** Notification arrives as **CON**; device expects an **ACK**.

#### OBS-C2 · `non=true` yields non-confirmable (NON)
- **Stimulus:** Register observe with `non=true`; trigger a change.
- **Expected:** Notification arrives as **NON** (no ACK expected).

### Group D — `/k` S-Mode notifications (drives `coap_notify_k_observers`)

#### OBS-D1 · First `/k` notification carries only `sia`
- **Stimulus:** `GET /k` with `Observe=0`, `lt=86400`.
- **Expected:** `2.05 Content`; first payload is exactly `{ "sia": <ia> }`
  (**no** `s` object) — per 2.5.9.1.

#### OBS-D2 · Subsequent `/k` notification includes the `s` object
- **Setup:** OBS-D1 active.
- **Stimulus:** Trigger the device to emit an outbound s-mode **"w"** message.
- **Expected:** Subscriber receives a follow-up `/k` notification whose payload
  **includes** the `s` object with `st="w"`.

#### TBD OBS-D3 · Plain GET on `/k` (no Observe) sends only the first payload
- **Stimulus:** `GET /k` **without** the Observe option.
- **Expected:** Returns `{ "sia": <ia> }`; **no** subsequent notifications follow.

#### OBS-D4 · Inbound s-mode POST to `/k` does not echo to observers
- **Goal:** Confirm the documented rule "does not notify on `/k` for inbound POST".
- **Setup:** A `/k` observer is registered.
- **Stimulus:** Send an inbound s-mode `POST /k`.
- **Expected:** The observer does **not** receive a notification for that inbound
  message.

### Group E — OSCORE-protected notifications (drives the AAD fix)

#### OBS-E1 · Encrypted subscription delivers decryptable notifications
- **Stimulus:** Register observe over an **OSCORE-protected** request; trigger a
  change.
- **Expected:** Notifications are **encrypted** and decrypt correctly with the
  established context; sequence numbers still advance.

#### OBS-E2 · Notification AAD binds the Observe option (regression for the fix)
- **Goal:** Guard the bug the AAD fix addressed — the Observe option must be part
  of the notification's AAD.
- **Stimulus:** Verify the AAD used for a notification; optionally a negative case
  where a mismatched/tampered AAD or context causes **AEAD verification failure**.
- **Expected:** Correct AAD → decrypt OK; wrong AAD → reject.

### Group F — Robustness / multiple observers

#### OBS-F1 · Independent sequence counters per observer
- **Setup:** Two subscribers on the same resource.
- **Stimulus:** Trigger changes.
- **Expected:** Each subscriber gets its own notifications with **independent**
  sequence numbers.

#### OBS-F2 · Expired/silent observer is dropped, others still notified
- **Setup:** Two subscribers; one stops responding / is removed.
- **Stimulus:** Trigger a change.
- **Expected:** The stale observer is pruned before the next round; the remaining
  observer still receives its notification.

---

## 6. Out of scope here (already covered by unit tests)

| Concern | Where it's tested today |
|---------|-------------------------|
| Raw AAD byte composition, AEAD nonce, HKDF | `test_oc_oscore_crypto.cpp` |
| Observe sequence-counter masking, observer list bookkeeping | `test_coap_observe.cpp` |
| `coap_notify_*` NULL / empty-list guards | `test_coap_observe.cpp` |

Runtime focuses on **observable wire behaviour**; the byte-level crypto and
data-structure logic stay at the unit level.

---

## 7. Discussion points for the review

1. **Client infrastructure:** OK to extend `coap_client.py` with observe support?
   Who owns / reviews that change?
2. **Test-spec numbering:** new `5.9.x` IDs vs folding into an existing section.
3. **Lifetime expiry (OBS-B3):** acceptable to mark slow/skip, or do we add a
   test-only short-lifetime path?
4. **`/k` stimulus (OBS-D2):** what is the cleanest way to make the DUT emit an
   outbound s-mode "w" in the test harness (reuse the `test_5_4` triggers)?
5. **OSCORE AAD negative case (OBS-E2):** how deep do we go — observe-only, or a
   broader AAD regression?
