# Runtime Test Development — Accumulated Knowledge

Session: May 2026 · Branch: `unit_tests_claude`

## Architecture Overview

### Test Infrastructure
- **Location**: `tests/runtime/` — Python-based integration tests using pytest
- **Server binary**: `build/linux-test-gcc/tests/runtime_test_server` (built via `cmake --preset linux-test-gcc`)
- **CI**: GitLab CI on `luftd/knx-ci:latest` (Python 3.13.5, GCC 15.2.0, IPv6 available)
- **Server password**: `"2X4W3TE0DFLLS19Y1FCH"` (from `app_get_password()` in the test server)
- **Device config**: Serial=`00fa10020800`, MID=`667`, Model=`"KNX Certification"`

### Python Test Modules
| File | Purpose |
|------|---------|
| `conftest.py` | Fixtures: server subprocess, port detection, CoapClient, OSCORE provisioning |
| `coap_client.py` | Minimal sync CoAP client with OSCORE + Echo retry support |
| `knx_oscore.py` | OSCORE AES-CCM-16-64-128 encryption/decryption for KNX IoT |
| `knx_spake2plus.py` | SPAKE2+ handshake client (3-step at `POST /.well-known/knx/spake`) |
| `test_5_1_discovery.py` | Discovery tests for `/.well-known/core` |

### Provisioning Flow
1. Start server subprocess, detect listening port via stdout
2. SPAKE2+ handshake → derive shared secret (master secret)
3. HKDF-SHA256 key derivation → sender_key, recipient_key, common_iv
4. Create `OscoreContext` with `sender_id=""` (empty), `recipient_id="PaseTmp"`
5. AT (access token) provisioning via OSCORE-protected `POST /auth/at`

## Bugs Found and Fixed

### 1. Subprocess Pipe Deadlock (conftest.py)
**Symptom**: Server stopped processing requests after ~64KB of stdout output.
**Root cause**: When starting subprocess with `stdout=subprocess.PIPE` and only reading until a sentinel line, the OS pipe buffer (~64KB on Linux) fills up. The server's `printf()` blocks, freezing its main loop.
**Fix**: Start a daemon thread to drain stdout after the sentinel:
```python
def _drain(stream):
    for _ in stream:
        pass
threading.Thread(target=_drain, args=(proc.stdout,), daemon=True).start()
```

### 2. OSCORE InvalidTag & AT Timeout — Loopback MULTICAST Flag (ipadapter.c)
**Symptom**: All OSCORE-protected requests returned InvalidTag errors, and AT provisioning (POST /auth/at) timed out even after echo challenges succeeded.
**Root cause**: In `oc_udp_receive_message()` (port/linux/ipadapter.c), the `mcast_sock` receive path unconditionally sets `flags = IPV6 | MULTICAST` for all messages. On Linux, the mcast_sock also receives unicast traffic from `::1` loopback, so those messages are incorrectly tagged as multicast. This causes:

1. Echo responses: `ECHO_CAUSED_BY_MC_SRC` flag is set → `unicast_echo_response_by_mc = true` → enters `if (is_smode)` → false for PASE → falls through without setting nonce/AAD → InvalidTag
2. Normal responses (e.g. 2.01 CREATED for AT): `oc_buffer.c` routes to `OUTBOUND_MC_OSCORE_EVENT` → `oc_oscore_send_multicast_message()` finds no group-addressed token for PASE → message dropped → client times out

**Fix**: Check `IN6_IS_ADDR_MULTICAST()` on the actual packet destination address before setting the MULTICAST flag:
```c
if (IN6_IS_ADDR_MULTICAST((struct in6_addr *)message->mcast_dest.address)) {
    message->endpoint.flags = IPV6 | MULTICAST;
} else {
    message->endpoint.flags = IPV6;
}
```
With this fix, `::1` messages get `flags=IPV6` and flow through the correct unicast paths (echo, OSCORE encryption, buffer routing) throughout the entire stack.

### 3. Discovery Regex for Empty Href (test_5_1_discovery.py)
**Symptom**: `parse_link_format()` failed to parse `<>;ep="..."` (empty href).
**Root cause**: Regex `<([^>]+)>` requires 1+ chars; server returns `<>` with empty href.
**Fix**: Changed to `<([^>]*)>` (zero or more chars).

### 4. Discovery Test Expectations (test_5_1_discovery.py)
**Symptom**: `test_contains_required_resources` and `test_contains_functional_block` failed.
**Root cause**: Server returns only root entry `<>;ep="knx://sn.00fa10020800 knx://ia.0.ffff"` (45 bytes) for unfiltered `.well-known/core` — no individual resource paths in unfiltered response.
**Fix**: Use `rt=` filtered queries to verify each resource type exists instead of expecting them in unfiltered response.

**Symptom**: `test_filter_ep_wrong_serial` failed with `assert resp is not None`.
**Root cause**: Server clears transaction without sending CoAP response for non-matching ep filter.
**Fix**: Accept `None` (timeout) as valid behavior — server suppresses responses for non-matching filters.

## OSCORE Technical Details

### Crypto Parameters
- **Algorithm**: AES-CCM-16-64-128 (16-byte key, 8-byte tag, 13-byte nonce)
- **OSCORE option number**: 9
- **Key derivation**: HKDF-SHA256 from master secret
- **Tag length**: 8 bytes (`OSCORE_AEAD_TAG_LEN`)

### Context Roles (PASE)
From the **client** (Python test) perspective:
- `sender_id = "PaseTmp"` (our KID when sending)
- `recipient_id = ""` (empty, server's KID)
- `sender_key` = HKDF(secret, "PaseTmp") = key for encrypting requests
- `recipient_key` = HKDF(secret, "") = key for decrypting responses

From the **server** (C stack) perspective:
- Context found by `oc_oscore_find_context_by_kid_and_kid_context` matching `kid="PaseTmp"` against `ctx->recipient_id`
- So server's context: `sender_id=""`, `recipient_id="PaseTmp"`
- `sender_key` = HKDF(secret, "") = key for encrypting responses (= client's `recipient_key`)

### Echo Challenge Flow
1. Client sends OSCORE-protected request
2. Server responds with 4.01 Unauthorized + Echo option (OSCORE-protected)
3. Client decrypts, extracts Echo value
4. Client retries original request including the Echo option
5. Server processes the request normally

### Multicast Echo Response — Random kid_context
When the DUT receives an s-mode multicast request and needs to send an echo
challenge (4.01), it creates a **temporary OSCORE context** with a 10-byte
RANDOM `kid_context` (see `oc_oscore_engine.c`, case `ECHO_CAUSED_BY_MC_SRC`).

To decrypt:
1. Parse the response OSCORE option with `_parse_oscore_option()` to extract
   `kid_ctx`
2. Create context: `OscoreContext(ms=MC_MS, sender_id=b"",
   recipient_id=MC_SENDER_ID, id_context=kid_ctx)`
3. Call `unprotect_response()` with `request_piv=b"\x01"`,
   `request_kid=MC_SENDER_ID`

Neither `MC_CTX_TX` nor `MC_CTX_RX` will work for decryption — the DUT
always generates a fresh random id_context for the echo response.

### Multicast Replay Protection & Echo Challenges
**Problem**: When `OC_REPLAY_PROTECTION` is enabled (default ON), the first request from an
unknown OSCORE client (new kid/kid_ctx) triggers an echo challenge. For unicast requests,
`coap_client.py`'s `oscore_request()` handles this automatically (retry with echo value).
For **multicast** requests, the DUT:
1. Receives the multicast POST
2. Decrypts OSCORE payload (context lookup by kid from AT)
3. Replay check: first-time client → state = `ECHO`
4. Sends 4.01 echo challenge as unicast (outer code 2.04 due to OSCORE wrapping)
5. Returns `UNAUTHORIZED_4_01` from `coap_receive()` — resource handler **never runs**

For multicast echo responses (`ECHO_CAUSED_BY_MC_SRC`), the DUT creates a NEW sender
context with a random 10-byte `kid_context` (see `oc_oscore_send_unicast_message` x0 path).
This makes the echo response **impossible to decrypt** with the client's original keys.

**Fix — Pre-sync the replay record**: Before sending a multicast POST, send a **unicast**
POST to `/k` with the same group OSCORE context. The unicast path uses
`ECHO_CAUSED_BY_UC_SRC`, which reuses the existing context (no new kid_ctx) — so
`oscore_request()` can decrypt and retry. After the echo is resolved,
`oc_replay_add_client()` creates a replay record. Subsequent multicast POSTs with the
same kid/kid_ctx will be `SYNCED` and reach the `/k` handler.

Helper function `_sync_group_context()` in `test_5_4_5_3_multicast.py` implements this.

### Nonce/AAD Construction for Echo Response
- **Nonce**: `AEAD_nonce(sender_id, inbound_piv, common_iv)` — uses server's sender_id and the PIV from the inbound request
- **AAD**: `compose_AAD(recipient_id, inbound_piv)` — uses recipient_id (= client's sender_id "PaseTmp") and inbound PIV
- **Key**: server's `sender_key`

## SPAKE2+ Protocol
- 3-step handshake at `POST /.well-known/knx/spake`
- Uses CoAP separate response pattern (server sends ACK, then response)
- Password: device-specific (test server uses `"2X4W3TE0DFLLS19Y1FCH"`)

## Test Results Baseline (before OSCORE fix)
- 8 passed, 3 failed, 57 skipped, 29 errors
- 57 skipped: `pytest.skip` when server doesn't respond to unauthenticated requests (expected behavior)
- 29 errors: All OSCORE InvalidTag (the mc echo fallthrough bug)
- 3 failed: Discovery test expectation mismatches

## Unit Test Conventions
- Framework: Google Test v1.15.2, fetched via CMake FetchContent
- Tests are C++ (.cpp), stack is pure C — use `extern "C" { #include "header.h" }`
- All tests in `tests/unit/` directory
- Naming: `test_<module>.cpp`
- CMake: `kis_add_test(test_<module> test_<module>.cpp)` in `tests/unit/CMakeLists.txt`
- Build: `cmake --preset=linux-test-gcc` (CI) or `windows-test-gcc` (local)
- Existing: `test_oc_base64.cpp` (14 tests), `test_oc_uuid.cpp` (9 tests)

## Testability Analysis

- ~170+ public functions cataloged across `api/`, `security/`, `messaging/coap/`, `util/`
- 22 testable modules identified, prioritized into 3 tiers (pure functions first)
- Existing unit tests: `test_oc_base64.cpp` (14 tests), `test_oc_uuid.cpp` (9 tests)
- Tier 1 (pure, no dependencies): oc_base64, oc_uuid, oc_helpers, oc_mmem, oc_knx_helpers
- Tier 2 (light dependencies): oc_rep, oc_endpoint, oc_blockwise, oc_ri
- Tier 3 (requires runtime/mocks): oc_core_res, oc_knx, oc_oscore_*, oc_spake2plus

## CI Configuration
- Docker image: `luftd/knx-ci:latest`
- IPv6 available on CI runners
- Runner: `gitlab-runner0.net.knx.org`
- Storage cleanup: delete `tests/runtime/knx_iot_server_creds/` before each test run to prevent stale OSCORE state

## Lessons Learned (May 2026 - Tests 5.5.9.3, 5.8.x, 5.10.x)

### POST /p uses integer CBOR keys, not string keys
- EITT CBOR payloads for POST /p (parameter batch write) use integer keys: `{1: value, 11: "/p/p1"}` NOT `{"value": 1, "href": "/p/p1"}`
- The stack's POST handler (`oc_knx_client.c`) only recognizes integer iname keys

### Metadata query ?m= on parameter resources
- `?m=*` returns all metadata (id, value, dpt, href, if, rt)
- `?m=id` returns only the `id` field (key 0, string `knx://sn:<serial>/p/p1`)
- `?m=value` returns only the value (key 1)
- The runtime_test_server.c `get_int_dp` handler was enhanced to support id/value/dpt/full metadata queries

### Pagination p.next links may omit ps
- When the stack returns a `p.next` link (e.g. `</fp/g?pn=1>;rt="p.next"`), it may omit the `ps` parameter
- The EITT trace shows the test tool always re-adds `ps=` when following next links
- Test code must append `ps=N` to next links if not already present

## CoAP Observe / Notifications (June 2026 - test_5_9_observe.py)

### Status: spec-backed approximation, NOT an EITT replication
- There is no EITT (08_10_5) observe section and no observe telegrams in
  `EittProject.xml`. `test_5_9_observe.py` is anchored to spec clauses
  (2.5.9.x, 2.6.10.1) and RFC 7641/8613, but the `5.9.x` / `OBS-x` numbering
  is a placeholder. The file header carries a prominent APPROXIMATION notice.

### Infrastructure added
- `coap_client.py`: Observe option (6) encoding in `_build_request` /
  `_build_oscore_request`; `ObserveSubscription` + `Notification` dataclasses;
  `observe_register`, `observe_register_oscore`, `collect_notifications`,
  `observe_deregister`, `count_incoming_messages`, `_observe_seq`.
- `knx_oscore.py`: `protect_request(..., observe=)` encodes the inner Observe
  option (6) before Uri-Path (11); `0`=register, `1`=deregister.
- `runtime_test_server.c`: `/p/2` and `/p/3` are `OC_DISCOVERABLE |
  OC_OBSERVABLE`; `test_set_dp()` calls `oc_notify_observers()` (resource
  looked up via `oc_ri_get_app_resource_by_resource_path`) after storing.

### Dedicated-observer-per-port pattern (critical)
- The stack keys subscriptions by source IP+port (2.6.10.1). Each observer
  must use its own UDP socket — use the `make_observer` factory, never the
  shared `coap` client, or notifications and request/response frames
  interleave on one socket.
- Observers share the session `oscore_ctx` (OSCORE is transport-independent,
  identified by kid not port; the Python SSN counter stays monotonic).

### OSCORE notification sequence number is OUTER (RFC 8613 4.1.3.5.2)
- For OSCORE-protected notifications the Observe **sequence number is in the
  outer (Class U) Observe option**; the inner (decrypted) Observe is present
  but empty (it only signals "this is a notification").
- `_recv_oscore_response` and `collect_notifications` therefore read seq from
  the outer option (falling back to inner). Reading only the inner option
  yields a constant 0 and breaks all seq-advance assertions.

### Known stack deviations from spec (documented in the test file header)
1. **/k subsequent-notification payload (spec 2.5.9.1 + 2.5.9.2).**
   `coap_notify_k_observers()` (`messaging/coap/observe.c`) forwards only the
   raw datapoint value `{1: <value>}` that `oc_issue_s_mode_message()`
   (`api/oc_knx_client.c`) passed it, dropping the mandatory
   `{4: <sia>, 5: {6: "w", 7: <ga>, 1: <value>}}` Group Notification wrapper.
   `OBS-D2` keeps the spec-correct assertions but is `xfail(strict=False)` so
   the deviation is visible and auto-flips to pass if the stack is fixed.
2. **Observe seq at registration→first-notification boundary (RFC 7641 3.4).**
   The 2.05 registration response and the first notification share the same
   Observe value (the per-observer counter is sent first, incremented after).
   RFC 7641 only requires monotonicity *between notifications*; `OBS-A2` and
   `OBS-E1` compare notifications to each other, not to the registration
   baseline.
3. **Inbound s-mode write re-emit (NOT a deviation).** An inbound `w` on a GA
   mapped to a transmission-flagged Datapoint legitimately triggers an
   outbound s-mode write (spec 2.5.9.3), which reaches /k observers.
   `OBS-D4` clears the group tables first (it runs after `OBS-D2`, which
   provisions GA 65535→/p/3 as a sender) to exercise the "no re-transmit"
   path; otherwise the leftover sender produces a legitimate echo.

### Result baseline
- 15 passed, 1 xfailed (`OBS-D2`) on `windows-test-gcc`. Multicast-dependent
  paths still notify on Windows even when the physical s-mode send goes
  nowhere, so `OBS-D` tests run; `OBS-D2`'s s-mode path skips gracefully if no
  notification is captured.
