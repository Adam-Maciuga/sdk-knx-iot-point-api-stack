# Unit Test Findings

Bugs and issues discovered during unit test development.

## F-001: ~~`oc_list_add_block()` circular link~~ — RESOLVED

- **Status:** Moot — `oc_list_add_block()` was removed from upstream.

## F-002: ~~`oc_ri_get_interface_mask()` returns 1 for empty input instead of `OC_IF_NONE`~~ — RESOLVED

- **Status:** Moot — `oc_ri_get_interface_mask()` had no production callers (used
  only by its own unit test) and was removed, along with its `GetInterfaceMask`
  tests in `test_oc_ri.cpp`.

## F-003: ~~`oc_core_read_and_set_device_hostname()` reads up to 128 bytes into a 17-byte stack buffer~~ — RESOLVED

- **File:** `api/oc_core_res.c`, `oc_core_read_and_set_device_hostname()` (~line 113)
- **Severity:** High — stack buffer overflow / memory corruption if the stored
  hostname file is larger than `HNAME_SIZE`.
- **Found by:** Code review while writing `test_oc_core_res.cpp` (not triggered
  by a test — doing so would corrupt the stack).
- **Description:** The local buffer was `char hname[HNAME_SIZE]` where
  `HNAME_SIZE == 4 + SERIAL_NUM_SIZE + 1 == 17`. The function then called
  `oc_storage_read(KNX_STORAGE_HOSTNAME, (uint8_t *)&hname, 128)`. If a stored
  hostname file contained more than 17 bytes, `fread()` wrote up to 128 bytes
  into the 17-byte stack buffer, overflowing it.
- **Resolution:** The buffer is now `char hname[MAX_HNAME_BUFFER_SIZE]`
  (`MAX_HNAME_BUFFER_SIZE == 129`, zero-initialized) and the read is capped at
  `MAX_HNAME_BUFFER_SIZE - 1` (128). `fread()` therefore writes at most 128
  bytes into a 129-byte buffer, leaving index 128 as a guaranteed trailing `\0`.
  This eliminates both the overflow and the subsequent `strlen()` over-read on
  an unterminated buffer.

## F-004: ~~`oc_message_add_ref(NULL)` dereferences NULL in the trailing debug log~~ — RESOLVED

- **File:** `api/oc_buffer.c`, `oc_message_add_ref()` (~line 56)
- **Severity:** Low/Medium — only triggered in `OC_DEBUG` builds; the increment
  itself is correctly NULL-guarded.
- **Found by:** `test_oc_buffer.cpp` / `BufferPool.AddRefNullIsNoOp` (was SEGFAULT).
- **Description:** The function guarded the increment with `if (message)`, but the
  `OC_DBG(...)` statement that followed read `message->ref_count`
  *unconditionally*, outside the guard:

  ```c
  if (message) { message->ref_count++; }
  OC_DBG("... counter is now %d", (void*)message, message->ref_count); // NULL deref
  ```

  With `OC_DEBUG` defined (as in the test/CI build) `OC_DBG` expanded to a real
  `printf`, so passing `NULL` dereferenced a NULL pointer and crashed. In a
  release build `OC_DBG` is a no-op, masking the defect.
- **Resolution:** The `OC_DBG` is now inside the `if (message)` block (matching
  `oc_message_unref`), so `oc_message_add_ref(NULL)` is a safe no-op in all
  builds. The previously skipped test `BufferPool.AddRefNullIsNoOp` now actively
  verifies the NULL-safe behaviour.

## F-005: ~~Group Value Response (st="a") updates value with the Update flag alone (Write flag ignored)~~ — RESOLVED

- **File:** `api/oc_knx.c`, `oc_core_knx_k_post_handler()` (the `service_a`
  branch, ~line 1007).
- **Severity:** Medium — spec-conformance deviation on the S-Mode receive path;
  a Group Value Response could overwrite a datapoint on a Group Object that has
  only the Update flag set (Write cleared), contrary to the specification.
- **Found by:** Spec-vs-code review of the `/k` receive handler while extending
  the runtime group-communication tests; probed by the additional runtime test
  `test_answer_updates_update_only_go` in `tests/runtime/test_group_comm.py`
  (cflag=0x80, Update-only). The EITT case `5.4.1.10` could not distinguish the
  deviation because it provisions cflag=0x90 (Write + Update), which updates
  under both interpretations.
- **Description:** Spec 2.5.7.3.3 Table 19 (Update flag) states a Group Value
  Response (st="a") updates the Group Object value only **if flag w=true** — the
  Write (w) flag must be set in addition to the Update (u) flag. The handler's
  st="a" branch originally checked **only** `OC_CFLAG_UPDATE`:

  ```c
  if (service & OC_CFLAG_UPDATE && ...) // st="a" applied with u alone, w ignored
  ```

  So with cflag=0x80 (u only, w=0) the stack incorrectly applied the response
  and overwrote the datapoint.
- **Resolution:** The st="a" branch now requires **both** flags via the
  precomputed predicate:

  ```c
  const bool service_a = received_notification.st == 'a' &&
                         cflags & OC_CFLAG_UPDATE && cflags & OC_CFLAG_WRITE;
  ```

  With cflag=0x80 the response is now ignored and the value is left unchanged.
  This also aligns the receive path with the outbound side
  (`oc_knx_client.c`, which already gates on `OC_CFLAG_WRITE`). Validated
  end-to-end in Docker (veth multicast): `test_answer_updates_update_only_go`
  passes (value stays unchanged with w=0) and the positive case
  the removed `test_5_4_1_10_multicast_answer_updates_value` (pre-rename
  name, cflag=0x90) still updates.

## F-006: ~~PM discovery with a serial-number wildcard returns Not Found~~ - RESOLVED

- **Status:** Resolved.
- **File:** `api/oc_discovery.c`, `oc_well_known_core_discovery_handler()` programming-mode branch.
- **Severity:** Low/Medium - affects discovery filtering when PM is enabled and a serial-number wildcard is combined with `if.pm`.
- **Found by:** `ProgrammingModeAndSerialNumber/WellKnownCorePmEndpoint.HandlesSerialNumberValueForProgrammingMode/PmOnWildcard`.
- **Input:** `ep=knx://sn.*&if=urn:knx:if.pm` with programming mode enabled.
- **Original behavior:** The PM-specific branch compared `*` directly with the configured serial number and returned `4.04 Not Found`.
- **Expected behavior under test:** Return `2.05 Content` with the device SN and IA identity, consistent with general `ep=knx://sn.*` wildcard discovery.
- **Resolution:** PM endpoint handling now recognizes exact, full wildcard, and partial wildcard SN values. The active `PmOnWildcard` test returns `2.05 Content`.

## F-007: ~~A 12-character serial number followed by `*` matched as an exact SN~~ - RESOLVED

- **File:** `api/oc_discovery.c`, serial-number handling in `oc_well_known_core_discovery_handler()`.
- **Severity:** Low/Medium - an invalid 13-character SN pattern was accepted when its first 12 characters matched the device SN.
- **Found by:** `WellKnownCoreHandler.SerialNumberOverlongPatternReturnsNotFound`.
- **Input:** `ep=knx://sn.00fa10020800*`, where the 12-character device SN is followed by `*`.
- **Expected behavior:** Return `4.04 Not Found`; a wildcard pattern may contain at most 11 SN characters followed by `*`.
- **Resolution:** Exact matching now also requires the query SN length to equal the configured serial-number length, so overlong patterns are rejected.

## F-008: ~~An empty serial-number endpoint returned the device identity~~ - RESOLVED

- **Status:** Resolved.
- **File:** `api/oc_discovery.c`, serial-number handling in `oc_well_known_core_discovery_handler()`.
- **Severity:** Low/Medium - an empty SN filter did not constrain discovery and could return the device identity.
- **Found by:** `WellKnownCoreHandler.SerialNumberEmptyReturnsNotFound` and the `PmOnEmpty` parameterized case.
- **Input:** `ep=knx://sn.` alone or followed by `if=urn:knx:if.pm` while programming mode is enabled.
- **Original behavior:** The SN branches required `ep_len > 9`, so the nine-character empty-SN endpoint bypassed SN matching and returned identity.
- **Expected behavior:** Treat the empty value as a non-match and return `4.04 Not Found` for unicast discovery.
- **Resolution:** The SN branches now accept the prefix length with `ep_len >= 9`; the existing no-match path rejects the zero-length SN. Both empty-SN tests pass.

## F-009: ~~A partial SN wildcard matched an internal substring~~ - RESOLVED

- **Status:** Resolved.
- **File:** `api/oc_discovery.c`, partial serial-number matching in `oc_well_known_core_discovery_handler()`.
- **Severity:** Low/Medium - a partial wildcard could select a device when the characters before `*` occurred anywhere in its SN.
- **Found by:** The synchronized `InternalSubstring` SN-only and SN+PM parameterized cases.
- **Input:** `ep=knx://sn.fa*` for device SN `00fa10020800`, with and without `if=urn:knx:if.pm`.
- **Original behavior:** `strstr()` accepted `fa` because it occurs inside the configured serial number.
- **Expected behavior:** Characters before `*` must match the beginning of the device SN; unicast returns `4.04 Not Found` and multicast is suppressed when they do not.
- **Resolution:** Both SN branches now use a bounded `strncmp()` from the beginning of the configured serial number. All synchronized transport cases pass.

## F-010: ~~`/.well-known/core` pagination emits a `p.next` link on the true last page whenever that page is exactly full~~ - RESOLVED

- **Status:** Resolved.
- **File:** `api/oc_discovery.c`, `oc_well_known_core_discovery_handler()` (~line 754):
  `const bool more_request_needed = query_parameter_key_value_pair_matches >= query_ps;`
- **Severity:** Medium - breaks the pagination contract (EITT 5.10.1.6-style `p.next` following) whenever the number of matching resources is an exact multiple of the requested page size (`ps`); with `ps=1` this triggers on literally every page, so the client's final "follow the link" request always gets `4.00 Bad Request`.
- **Found by:** New runtime test `test_wellknown_core_pagination_follows_next_links_over_all_fbs` in
  `tests/runtime/test_discovery_pagination.py` (`TestDiscoveryPaginationManyFunctionalBlocks`, test ID
  5.1.1.900 -- local extra, not an official EITT clause), which paginates
  `GET /.well-known/core?rt=*&ps=1` page by page, following each `p.next` link, over the real DUT's core
  resources + application resources/functional blocks (`?rt=*` pulls the latter into scope, see the
  `rt_len > 0 || if_len > 0` branch a few lines above).
- **Input:** Sequential `GET /.well-known/core?rt=*&pn=N&ps=1` for `N = 0..12` against the DUT (13 total
  matching resources with `rt=*`). Every response, including the response for `pn=12` (the last entry,
  index 12 of 13), carries a `p.next` link to `pn=13`. `GET .../core?rt=*&pn=13&ps=1` then returns
  `4.00 Bad Request` because `first_entry (13) >= total (13)`.
- **Root cause:** `more_request_needed` is computed from how many matches were placed on the *current*
  page (`query_parameter_key_value_pair_matches >= query_ps`), not from whether `first_entry + query_ps <
  total`. Whenever the current page happens to be exactly filled - trivially true for `ps=1` on any page
  with at least one entry, and true in general whenever `total` is a multiple of `ps` - the handler adds a
  `p.next` link even though no further resources exist, sending a conforming client that follows the link
  into a guaranteed `4.00`.
- **Not caught by existing unit tests:** `WellKnownCoreHandler.MoreDataIncludesNextPageIndicator` and
  `ExplicitPageNumberIncrementsNextPageIndicator` only check that *a* `p.next` link appears on early pages
  (never that the mechanism terminates correctly); `FinalPageOmitsNextPageIndicator` uses `ps=20` against a
  small mocked resource set, so the last page is a *partial* fill (`matches < ps`) and never exercises the
  exact-fill boundary.
- **Resolution:** `more_request_needed` now compares against the known `total` instead of the page's match
  count: `const bool more_request_needed = (first_entry + query_ps) < total;`. The full `WellKnownCoreHandler`
  unit group (43 tests) still passes, and the new runtime test paginates through all matching resources
  with `ps=1` and terminates cleanly with no next-page link on the true last page.

## F-011: ~~Unterminated CoAP query/URI values passed to `atoi()` / `strtol()` / `strtoull()` (out-of-bounds read)~~ - RESOLVED

- **Status:** Resolved.
- **Files:** `api/oc_knx_helpers.c` (`evaluate_query_px`), `api/oc_discovery.c` (group-address query),
  `api/oc_knx_swu.c` (`po` / `ps` / `pkgs`), `messaging/coap/observe.c` (`lt`),
  `api/oc_helpers.c` (both URI-wildcard helpers).
- **Severity:** High - a remotely reachable out-of-bounds read on every affected handler, plus a family of
  silent mis-parses that let malformed values through as valid numbers.
- **Found by:** Code audit of `evaluate_query_px`, then a census of every `atoi`/`strtol`/`strtoull` call
  site in the stack.
- **Root cause:** Values obtained from `oc_iterate_query_get_values()` and `coap_get_query_variable()`
  point **into the raw CoAP message buffer** and are returned with a separate length; they are **never**
  NUL terminated. `atoi`/`strtol`/`strtoull` take no length parameter, so they scanned past the value into
  adjacent message memory. Checking `endptr` afterwards does not help - the over-read has already happened.
  Three further defects rode along:
  1. `errno` alone only reports `ERANGE`; `"abc"`, `"2abc"` and `"-1"` all convert with `errno` untouched.
  2. On a 64-bit host `long` is wider than `int`, so `"2147483648"` raised no `ERANGE` and silently
     truncated on assignment.
  3. `strtoull` accepts a leading `-` and negates modulo 2^64 without an error, so `"-1"` became
     `0xFFFFFFFFFFFFFFFF`.
  Several sites also passed `NULL` as `endptr`, silently accepting trailing garbage (`"lt=100abc"` -> 100,
  `"1a2zz"` -> 0x1a2).
- **Resolution:** A single file-static `parse_64bit_from_string()` in `api/oc_knx_helpers.c` now performs a
  **bounded copy** of exactly `value_len` bytes into a local NUL-terminated buffer before converting, and
  rejects a missing/empty/over-length value, a leading sign, `errno`, an `endptr` that does not land on
  `value_len`, and any value above the wrapper's maximum. Four public wrappers expose it, each encoding
  its base and range in its own name and result type: `oc_parse_uint16_from_base10_string`
  (decimal, `0..USHRT_MAX`), `oc_parse_uint16_from_base16_string` (hexadecimal, `0..USHRT_MAX`),
  `oc_parse_sint32_from_base10_string` (decimal, `0..INT_MAX`) and `oc_parse_uint64_from_base16_string`
  (hexadecimal, full 64-bit range).
  Every affected site was converted, including the hexadecimal IID/IA parsing of
  `ep=knx://ia.<iid>.<ia>` in `api/oc_discovery.c` and `api/oc_knx_client.c`, which additionally gained
  trailing-garbage and negative-value rejection. `evaluate_query_px` signals a parse failure by setting
  `pn`/`ps` to `-1`, which the 14 pagination guards already reject via
  `first_entry >= total || query_ps < 1 || query_pn < 0`.
- **Verification:** 1312/1312 unit tests pass, including the new `ParseUint16` / `ParseSint32` /
  `ParseUint16Base16` / `ParseUint64` groups in `test_oc_knx_helpers.cpp` (each with a case that passes a
  length shorter than the
  literal, pinning the bounded read) and the 18-case `KnxSwuABlockInvalidQuery` suite. The full runtime
  suite is green at 347 passed / 5 skipped, exercising the pagination, discovery, observe, SWU-block and
  wildcard paths against a live DUT over CoAP/OSCORE.

## F-012: ~~DNS-SD service instance advertised without a TXT record (RFC 6763 clause 6 violation)~~ - RESOLVED

- **File:** `port/dns-sd_mdns.c` (`send_announcement`, `send_query_response`, `send_srv_response`,
  `mdns_query_callback`) and the vendored mDNS library, at the time patched by `port/mdns.patch`
  (since folded into `port/mdns/mdns.h`, see `port/mdns/README.md`).
- **Severity:** High - the device was undiscoverable by any RFC 6763 conformant client, including
  the standard `zeroconf` Python library and, by extension, the EITT-style discovery tests.
- **Found by:** `test_discovery_mdns.py` - five deterministic failures
  (`test_srv_resolution_of_unconfigured_device`, `test_aaaa_resolution_of_unconfigured_device`, `test_full_chain_by_serial_when_configured`,
  `test_full_chain_by_individual_address`, `test_full_chain_via_programming_mode_subtype`), all failing at
  `_verify_discovery_chain` with `Could not resolve service info for <sn>._knx._udp.local.`
- **Description:** The responder emitted a TXT record **only** when a non-zero sleep period was
  configured (`knx.sp != 0`). A normal, non-sleepy device therefore advertised PTR, SRV and AAAA
  records but no TXT record at all, and answered no TXT queries. RFC 6763 clause 6 states that
  *every* DNS-SD service instance must have a TXT record, using a single zero-length string when
  there is no key/value data to convey.
- **Why it surfaced only now:** `tests/common/requirements.txt` pinned `zeroconf>=0.100.0` with no
  upper bound, so every CI and Docker run installed the newest release from PyPI. Version 0.151.3
  tightened `ServiceInfo._is_complete` to `bool(self._txt_seen and (ipv4 or ipv6))`, enforcing the
  RFC 6763 clause 6 requirement. Older versions considered an instance complete without a TXT
  record, which masked the defect. The stack behaviour did not change - the library stopped
  tolerating it. Three plausible but wrong hypotheses were eliminated by controlled re-runs first
  (a recent 5000ms to 3000ms discovery timeout reduction, and the CI-only
  `OC_DEBUG_ENABLED`/`OC_PRINT_ENABLED` build flags); both reproduced the failure identically,
  which is the signature of a missing record rather than a slow or build-dependent one.
- **Spec tension:** KNX IoT clause 2.6.1.2.4 describes the TXT record content as optional and
  clause 2.6.1.2.4.1 reserves the `SP` key exclusively for sleepy devices, while clause 2.6.1.2.1
  incorporates RFC 6763 by reference, which mandates the record itself. Synthesising an `SP` value
  for a non-sleepy device would violate 2.6.1.2.4.1, so the only spec-legal resolution is to emit
  the RFC 6763 clause 6.1 *empty* TXT record: one 0x00 byte, no `=` separator.
- **Resolution:** A TXT record for the service instance is now emitted unconditionally - carrying
  `SP=<seconds>` when `knx.sp != 0` and the empty TXT record otherwise. It is included in
  announcements, in solicited PTR query responses and as an additional record on SRV responses so a
  client can complete the SRV -> AAAA -> TXT chain from a single packet. `MDNS_RECORDTYPE_TXT` was
  added to the accepted query-type filter and a new `send_txt_response()` answers direct TXT and ANY
  queries for the instance name. A third hunk in `port/mdns.patch` (now part of `port/mdns/mdns.h`) teaches
  `mdns_answer_add_txt_record()` to encode a zero-length string when both key and value are empty,
  instead of unconditionally writing `key` `=` `value`.
- **Latent defect fixed along the way:** `sp_str` was declared *inside* the `if (knx.sp != 0)`
  block while `mdns_record_t` stores only a pointer to it, so the record referenced a buffer whose
  scope had already ended by the time the packet was serialised. The buffer is now declared at
  function scope.
- **Masked by CI:** both runtime jobs in `.gitlab-ci.yml` (`linux-runtime-test` and
  `linux-runtime-test-asan`) carry `allow_failure: true`, so these five failures still produced a
  green pipeline badge. The badge is not evidence that the runtime suite passed.
- **Verification:** the mDNS discovery file went from 5 failed / 27 passed to **32 passed, 0
  failed**, and the wider `5_1` slice from 5 failed / 125 passed to **130 passed, 0 failed**. No
  test expectations were modified. The `state_sync` snapshot tests, which assert exact TXT/SP
  content, still report `sp=None` for a non-sleepy device, confirming the empty TXT record is parsed
  as *absence of an SP key* rather than as a spurious value, while `sp='300'` is still observed
  after setting a sleep period. `tests/common/requirements.txt` now pins
  `zeroconf>=0.151.3,<1.0` so the conformance level being tested against is explicit and the same
  silent drift cannot recur.

## F-013: ~~`oc_set_pdu_size()` double-counts the CoAP header when deriving the block size~~ - WITHDRAWN, NOT A DEFECT

- **File:** `api/oc_main.c`, `oc_set_pdu_size()`
- **Status:** Withdrawn. The original code was correct; the finding rested on a wrong
  assumption about the meaning of the `pdu_size` argument.
- **The actual contract:** `include/oc_buffer_settings.h` documents the argument as
  "MTU size covers CoAP payload/header", i.e. the payload **plus one** CoAP header.
  The in-function comment ("add internally *second* coap header size") says the same:
  the caller supplies the outer header, the function adds the second one for the
  OSCORE inner plaintext layer. The stored MTU therefore ends up at
  `payload + 2 * COAP_MAX_HEADER_SIZE`, matching the static default at line 141.
- **Why the original three lines were consistent:** under that contract the guard,
  the store and the block-budget subtraction all correctly used a *single*
  `COAP_MAX_HEADER_SIZE` - add one, subtract one, guard one. Checked against the
  default initializer, which the surrounding comment declares to be "same as
  `oc_set_pdu_size()` would calculate", the equivalent call is
  `oc_set_pdu_size(KNX_PAYLOAD_SIZE + COAP_MAX_HEADER_SIZE)`:
  - stored MTU: `(payload + H) + H` = `payload + 2H` - matches line 141
  - block budget: `(payload + H) - H` = `payload` = 1024 - matches line 142
  - guard: `payload + H >= H + 16` holds for any `payload >= 16`

  All three agree, and this is the only self-consistent reading.
- **Where the analysis went wrong:** it assumed the argument meant "payload only",
  which made the single `H` in the setter look asymmetric against the default's `2H`.
  It was not asymmetric. The follow-on claim of an unsigned underflow was equally
  unfounded: the smallest accepted argument `H + 16` minus `H` is exactly 16, which is
  precisely *why* the original guard used a single `H`. Changing the store to `2 * H`
  is what actually broke the contract; the matching `2 *` in the subtraction and the
  raised guard were downstream consequences of that first wrong edit. All three have
  been reverted.
- **Invariant now pinned:** the guard and the block-budget subtraction must always use
  the **same** multiple of `COAP_MAX_HEADER_SIZE`. Touching one without the other
  silently breaks the contract. Recorded as a block comment above
  `oc_set_pdu_size()` and enforced by
  `PduWorstCaseFrame.GuardAndBlockBudgetUseSameHeaderMultiple`, supported by
  `SetPduSizeToDefaultEquivalentReproducesDefaultPduSize`,
  `SetPduSizeToDefaultEquivalentReproducesDefaultBlockSize`,
  `PduBelowTwoHeadersPlusMinBlockIsRejected`,
  `MinimumAcceptedPduYieldsMinimumBlockSize` and `BlockSizeIsMonotonicInPduSize`.
- **Worth keeping from the exercise - worst-case frame headroom (measured, not
  assumed):** `test_oc_buffer_settings.cpp` builds a maximal-option CoAP packet with
  the stack's own serializer (full 8-byte token, long Uri-Path, Uri-Port, Observe,
  Max-Age, ETag, Block1/Block2, Size1/Size2 and the OSCORE option) and serializes it
  through `coap_oscore_serialize_message()`. The outer message measures **90 bytes**,
  far below the `COAP_MAX_HEADER_SIZE` budget of 164, and
  `outer + inner + KNX_PAYLOAD_SIZE + OSCORE_AEAD_TAG_LEN` fits the default MTU with
  room to spare. The `2 * COAP_MAX_HEADER_SIZE` reservation in the default is
  conservative, as intended.

## D-001: `oc_set_pdu_size()` contract redefined to the total CoAP PDU size

- **File:** `api/oc_main.c`, `include/oc_buffer_settings.h`
- **Status:** Deliberate design change, not a defect. Supersedes the contract described
  in the withdrawn F-013 above.
- **Old contract:** the argument was the payload **plus one** (outer) CoAP header, and
  the setter added the second (inner plaintext) header internally. Consequence:
  `oc_get_pdu_size()` returned a different unit than `oc_set_pdu_size()` accepted, so
  `oc_set_pdu_size(oc_get_pdu_size())` inflated the buffer by `COAP_MAX_HEADER_SIZE`
  (164 bytes) on every call - a silent drift with no compiler or runtime warning.
- **New contract:** the argument is the **total CoAP PDU size**, i.e. the size of the
  buffer allocated per message, and it is stored verbatim. The payload budget left to
  the application is `pdu_size - 2 * COAP_MAX_HEADER_SIZE`.
- **Why two header budgets:** an OSCORE message holds both layers in the same buffer at
  the same time (RFC 8613 section 5.3). The outer message carries the CoAP header, the
  token, the Class U options and the OSCORE option and is what goes on the wire; the
  inner COSE plaintext carries the real request code and the Class E options. Options
  such as Uri-Path move inside and are duplicated or replaced outside, so both
  representations must be budgeted for independently.
- **Migration impact:** none. `oc_set_pdu_size()` has no production callers - it appears
  only in its own definition, the header declaration, the unit tests and the docs.
- **Behavioural deltas:** the guard now rejects anything below
  `2 * COAP_MAX_HEADER_SIZE + 16` (344) instead of `COAP_MAX_HEADER_SIZE + 16` (180),
  and a given argument now yields a smaller block size than before because one more
  header budget is subtracted. The static defaults at `oc_main.c` lines 141-142 are
  unchanged; the equivalent call is now
  `oc_set_pdu_size(KNX_PAYLOAD_SIZE + 2 * COAP_MAX_HEADER_SIZE)` (1352).
- **Pinned by:** `BufferSettings.GetPduSizeReturnsExactlyWhatWasSet` and
  `BufferSettings.SetPduSizeFromGetPduSizeIsIdempotent`, which would both fail if the
  setter ever resumed adjusting the stored value.
- **Superseded in part by D-002:** the `2 * COAP_MAX_HEADER_SIZE` arithmetic quoted
  above was replaced by the mode-specific budget sum `OC_COAP_HEADER_BUDGET`. The
  contract itself (argument = total PDU, stored verbatim) is unchanged.

## D-002: `COAP_MAX_HEADER_SIZE` split into three mode-specific budgets

- **Files:** `messaging/coap/conf.h`, `messaging/coap/coap.c`,
  `messaging/coap/oscore.c`, `api/oc_main.c`, `api/oc_client_api.c`
- **Status:** Deliberate design change, not a defect.
- **Problem with the single constant:** one 164-byte `COAP_MAX_HEADER_SIZE` was used
  for three structurally different frames. It was simultaneously too generous for the
  OSCORE outer message (which carries only Class U options) and derived from no
  itemization at all for the inner plaintext, whose Uri-Path, Uri-Query and Echo
  options scale with the KNX URI limits. `2 * COAP_MAX_HEADER_SIZE` was a coincidence
  that happened to be large enough, not a bound.
- **New model:** `coap_oscore_serialize_message(pkt, buf, inner, outer, oscore)` is
  called in exactly three combinations, each with a thin wrapper in `oscore.c` and its
  own budget:

  | inner / outer / oscore | wrapper | budget | value |
  |---|---|---|---|
  | 1 / 0 / 0 | `oscore_serialize_plaintext()` | `COAP_MAX_INNER_HEADER_SIZE` | 214 |
  | 0 / 1 / 1 | `oscore_serialize_message()` | `COAP_MAX_OUTER_HEADER_SIZE` | 54 |
  | 1 / 1 / 0 | `coap_serialize_message()` | `COAP_MAX_HEADER_SIZE` | 268 |

  Each budget is itemized option by option in `conf.h` against RFC 7252 clause 3.1
  option-header costs, so a change to a KNX URI limit propagates automatically.
- **Derived sizing:** `OC_COAP_HEADER_BUDGET = outer + inner = 268`,
  `KNX_PAYLOAD_SIZE = 768`, default PDU 1036, `COAP_MAX_MESSAGE_SIZE = 1152`,
  `OC_MAX_ON_WIRE_BLOCK_SIZE = 512` (RFC 7252 clause 4.6 - the block must leave room
  for both headers and the AEAD tag inside one datagram).
- **Latent bug fixed on the way:** `api/oc_client_api.c` offset the s-mode payload by
  a budget that no longer matched the mode its packet was serialized in.
- **Guards:** three `static_assert`s in `oc_main.c` bound the payload from above
  (`+ budget <= COAP_MAX_MESSAGE_SIZE`), from below (must hold one 16-byte block) and
  cap the derived block size. Each was proven live by deliberately breaking the
  constant it protects and observing the named failure.
- **Pinned by:** `CoapHeaderBudgets` (4), `CoapMessageSize` (3) and
  `CoapSerializationModes` (4) in `test_oc_buffer_settings.cpp`, plus
  `tests/runtime/test_coap_header_budget.py` (9), which drives real CoAP/OSCORE
  exchanges with every KNX URI option at its maximum. An under-budgeted frame makes
  `coap_oscore_serialize_message()` return 0 and null `coap_pkt->buffer`, i.e. the
  message is dropped **silently** - which is why the runtime coverage exists.
- **Known consequence - see F-017:** lowering `KNX_PAYLOAD_SIZE` to 768 pushed the
  larger link-format bodies over one block, which exposed a gap in the *test client*
  (no Block2 reassembly). The stack itself paginates correctly.
- **Follow-up, Uri-Path over-reservation (inner 219 -> 214):** the first itemization
  charged two option-header bytes for every part of a string option group
  (`len + 2 * parts`), i.e. it assumed every part needs a length extension byte. A part
  only earns that byte at 13 characters or more, so the character budget caps the
  extension bytes as well as the part count does. `COAP_STRING_OPTION_GROUP_SIZE(len,
  parts) = len + parts + min(parts, len / 13)` in `conf.h` applies both caps. Uri-Path
  drops 64 -> 59 (48 characters over at most 8 parts, of which at most 3 can be long);
  Uri-Query is unchanged at 72, because 64 characters over 4 parts means every part
  *can* be long, so there the part count was already the binding cap. Net: inner
  219 -> 214, plain 273 -> 268, default PDU 1041 -> 1036.
  - The bound stays safe when leading options are absent and the deltas grow: the
    largest inner option number is Echo (252), so a delta from zero still needs only one
    extension byte, while every absent option frees at least three. Sum-of-maxima
    absorbs it.
  - **Pinned by:** `CoapHeaderBudgets.StringOptionGroupChargesOneExtensionPerLongPartOnly`
    (the formula) and `CoapHeaderBudgets.SegmentHeavyUriPathFitsTheTightenedGroupBudget`
    (a measured count-heavy split; the pre-existing `MAX_URI_PATH` case only exercises
    the character-heavy split, which the tightening does not bind on).

## F-017: ~~`/dev` and `/.well-known/core` truncate link-format instead of paginating~~ - WITHDRAWN, NOT A DEFECT

- **Files:** originally suspected `api/oc_knx_dev.c`, `api/oc_discovery.c`; the actual
  gap was in `tests/runtime/coap_client.py`.
- **Status:** WITHDRAWN. The stack behaves correctly. The finding as first written was
  a misdiagnosis, corrected by measurement before any product code was touched.
- **Original symptom:** after `KNX_PAYLOAD_SIZE` dropped from 1024 to 768 (see D-002),
  four runtime tests began seeing link-format bodies cut mid-attribute, e.g.
  `...,</p/p1>;rt="urn:knx:dpa.65500.201";i`. This looked like a truncated payload
  returned as a successful 2.05.
- **What the measurement actually showed:** decoding the response options revealed the
  DUT was already doing the right thing. For `GET /.well-known/core?if=*`:

  | Option | Value | Meaning |
  |---|---|---|
  | Block2 (23) | `0x0d` | NUM=0, **M=1 (more blocks follow)**, SZX=5 -> 512-byte blocks |
  | Size2 (28) | `688` | total body length |

  That is exactly RFC 7959. The `;i` at the end is a legitimate block boundary, not a
  half-written buffer. The body was never truncated - only the first of two blocks was
  ever delivered, because the client never asked for the second.
- **Real root cause:** `tests/runtime/coap_client.py` had no Block1/Block2 support at
  all. It read the first datagram's payload and returned it as the whole body, silently
  discarding everything the Block2 "more" bit announced. `multicast_get()` additionally
  never populated `CoapResponse.options`, so Block2 was invisible on that path too.
- **Measured link-format sizes** (at `KNX_PAYLOAD_SIZE` 768, one 512-byte block):

  | Request | First block | True body (Size2) |
  |---|---|---|
  | `GET /.well-known/core` | 396 | 396 (fits, no Block2) |
  | `GET /.well-known/core?if=*` | 512 | **688** |
  | `GET /.well-known/core?rt=*` | 512 | **688** |
  | `GET /dev` (OSCORE) | 512 | **553** |

- **Resolution:** the test client now reassembles Block2 (see the "Block-wise transfer"
  section in `coap_client.py`). All four tests pass at `KNX_PAYLOAD_SIZE` 768 with no
  product change. Raising the payload to 1024 would also have made them pass, but only
  by keeping the bodies under one block - it would have hidden the client gap rather
  than fixing it, and any future growth in the resource list would have re-broken them.
- **Lesson:** a payload that ends mid-token is not evidence of truncation. Check the
  Block2 "more" bit and Size2 before concluding the writer is at fault.

## F-014: ~~Inbound `block1_size` derived from the Block2 option's SZX~~ - RESOLVED

- **File:** `messaging/coap/coap.c`, `coap_oscore_parse_options()`, Block1 case
- **Status:** RESOLVED. Fixed by the user; regression tests added in the same pass.
- **Defect:** the Block1 parse branch computed the block size from the wrong field:

  ```c
  coap_pkt->block1_size = (uint16_t)(16 << (coap_pkt->block2_num & 0x07));
  ```

  The SZX bits were read from `block2_num` instead of `block1_num`. The adjacent
  `block1_more`, `block1_offset` and `block1_num` assignments in the same branch all
  used `block1_num` correctly, which is what made the single wrong operand easy to
  miss.
- **Introduced by:** commit `4a5f26c8` (2026-03-23, "format/ comment"). That commit was
  cosmetic in intent - it added a `(uint16_t)` cast and a trailing comment to both the
  Block2 and Block1 branches and rewrapped `block1_offset` onto one line. The Block2
  branch immediately above received the identical edit and is correct there; the Block1
  branch appears to have been copy-pasted from it without switching the operand back.
  Commit `def5d1d6` (2026-06-01, block size retype to `uint16_t`) later moved the line
  and carried the defect forward unchanged.
- **Impact:** Block1 and Block2 are independent options and a peer may legitimately use
  a different SZX for the request body than for the response body. Two failure modes:
  1. Both options present with different sizes - the inbound request-body block size
     collapses onto whatever Block2 carried, misplacing every block boundary during a
     block-wise upload.
  2. Block1 present with no Block2 - `block2_num` retains its initial value, so
     `block1_size` silently degrades to the SZX 0 minimum of 16 bytes regardless of what
     the peer requested.
- **Fix:** read the SZX from `block1_num`, matching the surrounding assignments.
- **Pinned by:** three tests in `tests/unit/test_coap.cpp`, all of which were confirmed
  to fail against the reverted defect and pass against the fix:
  `RoundTrip_Block1AndBlock2UseDifferentSizes` (Block1=128 / Block2=1024, observed 64
  when broken), `RoundTrip_Block1SizeIsIndependentOfAbsentBlock2` (Block1=512 with no
  Block2, observed 16 when broken), and
  `RoundTrip_Block1SurvivesEveryValidSzxWhileBlock2Differs` (sweeps all seven legal
  Block1 sizes with Block2 pinned to a deliberately different value, so the two can
  never coincidentally agree).

## F-015: ~~DNS-SD unsolicited announcement sent once per change (RFC 6762 section 8.3 violation)~~ - RESOLVED

- **File:** `port/dns-sd_mdns.c` (`knx_dns_sd_update_service`, `knx_dns_sd_set_sleep_period`).
- **Severity:** Medium - multicast carries no acknowledgement. A lost single announcement leaves
  every cache with the old record until its TTL runs out.
- **Found by:** a review of the announcement path against RFC 6762 section 8.3, confirmed by
  `test_dns_sd_mdns.cpp`. The runtime test `test_transition_pm_disable` had encoded the
  single announcement: its capture window opened exactly one second after the setup change, so a
  conformant responder fails it with the repeat of the setup announcement.
- **Description:** RFC 6762 section 8.3 requires at least two unsolicited responses, one second
  apart, and section 8.4 applies the same step to every change of a record. The stack sent
  `send_announcement()` once per change. The KNX IoT standard 3/10/5 clause 2.6.1.2.2 names RFC 6763
  without a change, so RFC 6762 applies as written. The normative test case 5.1.2.5a measures the
  TTL of the `_pm` PTR record and names no count of responses; a device with two announcements
  passes it. The test `5.1.901.3` has no id in the `08_10_5` catalog and encoded the behavior of
  this stack, not a requirement.
- **Resolution:** Every successful announcement schedules `repeat_announcement()` as a delayed
  callback on the RI event loop (`oc_set_delayed_callback`, one second). A new change within that
  second restarts the delay, so the repeat always carries the newest record. `knx_dns_sd_stop()`
  cancels a pending repeat before the goodbye. `test_discovery_mdns.py` now drops packets that
  arrive before the trigger of a transition (`TransitionResult(packets, trigger_time)`), and
  `MDNS_ANNOUNCE_WAIT_S` rose to 1.5 s so the repeat passes before the next step.
- **Verification:** `test_dns_sd_mdns.cpp`: the four cases `SecondAnnouncementFollowsOneSecondLater`,
  `RepeatLeavesOnlyOnce`, `ChangeWithinOneSecondRestartsTheRepeat` and `SleepPeriodChangeGetsARepeat`
  are red on the previous code and green on the fix. The runtime suite `-k 5_1` is 130 passed, and
  its announcement capture shows the repeat one second after the first packet.

## F-016: ~~TXT record in the answer slot of an mDNS packet is dropped (empty SP goodbye and empty TXT answer)~~ - RESOLVED

- **File:** `port/dns-sd_mdns.c` (`knx_dns_sd_set_sleep_period` for `sp == 0`, `send_txt_response`)
  and the vendored mDNS library `mdns.h` (`mdns_answer_add_record`,
  `mdns_answer_multicast_rclass_ttl`, `mdns_query_answer_unicast`).
- **Severity:** Medium - a cleared sleep period never reaches a cache, so a listener keeps
  `SP=<n>` until the TTL runs out, and a direct TXT query gets a packet without a record. The
  discovery chain of `zeroconf` is not affected, because it reads the TXT record from the
  additional section of the PTR and SRV answers.
- **Found by:** two cases of `test_dns_sd_mdns.cpp`. `ClearingTheSleepPeriodSendsATxtGoodbye`
  sets SP to 30 and back to 0 and expects the goodbye for the TXT record `SP=30`.
  `TxtQueryForTheInstanceGetsTheTxtAnswer` sends a TXT query and expects the TXT record as the
  answer. Both cases recorded the same 12-byte packet: `00 00 84 00 00 00 00 01 00 00 00 00`.
  That is a bare DNS header. Its ANCOUNT field (bytes 7 and 8) promises one answer record, and
  no record follows. A receiver reads the count, finds no record and discards the packet.
- **Description:** `mdns_answer_add_record()` returns without writing when the record type is
  TXT ("TXT records will be coalesced into one record later"). The coalescing step
  `mdns_answer_add_txt_record()` runs only over the authority and the additional arrays, never
  over the single `answer` argument. A TXT record passed as `answer` therefore leaves the packet
  empty, while the header still counts one answer. The stack passes a TXT record as `answer` at
  the two places above.
- **Status:** Resolved.
- **Resolution:** two hunks added to `port/mdns.patch`. After the `answer` record is added, both
  `mdns_answer_multicast_rclass_ttl()` and `mdns_query_answer_unicast()` now call
  `mdns_answer_add_txt_record(buffer, capacity, data, &record, 1, rclass, ttl, &string_table)`
  (`&answer` in the unicast function, which keeps no local copy). The multicast path carries the
  two call sites of the stack; the unicast path is patched to keep the library consistent.
  `mdns_answer_add_record()` ignores a TXT record and `mdns_answer_add_txt_record()` ignores every
  other type, so the extra call is a no-op for the other record types. The hardcoded
  `answer_rrs = htons(1)` needed no change - it was already correct once the record is written.
  The note above `FetchContent_Declare(mdns)` in `port/CMakeLists.txt` recorded the reason until the
  library was vendored; the hunks now live in `port/mdns/mdns.h` and are listed in `port/mdns/README.md`.
- **Verification:** the `GTEST_SKIP()` calls were removed from
  `ClearingTheSleepPeriodSendsATxtGoodbye` and `TxtQueryForTheInstanceGetsTheTxtAnswer`; both pass
  with their original assertions. `test_dns_sd_mdns.cpp` is 28 passed, 0 skipped; the full Linux
  suite is 1420 passed, 0 skipped. The runtime suite `-k 5_1` is 130 passed, and its capture of the
  cleared sleep period now shows two goodbye packets instead of an empty one.

## F-018: `if=` of a functional block link ignores `short_urn_form` on `/.well-known/core` - RESOLVED

- **File:** `api/oc_knx_fb.c` (`oc_add_functional_blocks_from_application_to_response`)
- **Severity:** Low - a well-formed but abbreviated urn on one attribute of one link type. The
  link is still parseable; a client that resolves `if=` against the full `urn:knx:` namespace
  reads the wrong interface, and the same response carries both spellings.
- **Found by:** inspection of a live unicast `GET /.well-known/core` response, which mixes the
  two forms inside a single payload:

  ```
  </dev>;rt="urn:knx:fb.0";if="urn:knx:if.ll";ct=40      <- core resource, long form
  </f/417_1>;rt="urn:knx:fb.417";if=":if.ll";ct=40       <- functional block, rt long / if short
  ```

- **Description:** the function takes a `short_urn_form` flag, documented at `oc_knx_fb.h:34` as
  "if the urn:knx needs to be added as part of response or not (/well-known vs /f)". It has two
  callers: `oc_discovery.c:710` passes `false` (`/.well-known/core`, long form) and
  `oc_knx_fb.c:480` passes `true` (`/f`, short form). The `rt=` attribute honours the flag, but
  `if=` did not - it was emitted from one hardcoded literal
  `"\";if=\":if.ll\";ct=40"`, so `/.well-known/core` always got the short form. The core
  resources on the same response come from their own resource definitions and use the long form,
  hence the mismatch inside one payload.
- **Status:** Resolved.
- **Resolution:** the `if=` literal now branches on `short_urn_form` the same way the `rt=`
  literal above it does: `:if.ll` when short, `urn:knx:if.ll` when long.
- **Verification:** full unit suite 1407 passed, 0 failed (windows-test-gcc). No test asserted on
  the emitted string before or after - the existing `if.ll` occurrences in `test_oc_discovery.cpp`
  and `test_oc_knx_fb.cpp` are query *inputs*, not response assertions. The runtime discovery
  regexes in `test_discovery_pagination.py` match on `rt=` and `ct=` only. The fix was subsequently carried
  by the 380 passed / 5 skipped / 0 failed runtime run (848.00s, `-IncludeUncommitted`).

## F-019: `CONTENT_NONE` marker serialized as a CoAP Content-Format option - RESOLVED

- **Files:** `include/oc_ri.h` (`oc_content_format_t`), `messaging/coap/coap.c` /
  `messaging/coap/coap.h` (`coap_set_header_content_format`, `coap_get_header_accept`,
  `coap_set_header_accept`), `api/oc_ri.c`, `api/oc_client_api.c`
- **Severity:** Low-Medium - no interop break, but the device emitted an option value that is not a
  registered CoAP Content-Format, on both a request and a payload-carrying response.
- **Found by:** inspection, while tracing what `coap_set_header_content_format(pkt, CONTENT_NONE)`
  at `oc_client_api.c:182` actually produced on the wire.
- **Root cause - an application level marker crossing into the packet layer.** `CONTENT_NONE` is an
  invented member of `oc_content_format_t`, not an IANA CoAP Content-Format. The enum is int sized,
  but `coap_packet_t` stores `content_format` and `accept` as `uint16_t` (`coap.h:133`, `coap.h:135`).
  The old value `99999` lies outside the 0-65535 registry range and therefore truncated silently to
  **34463**, which falls inside the 33000-64997 Expert Review block and so is indistinguishable from
  a real assignment. `coap_set_header_content_format()` set the option bit unconditionally, and
  `COAP_SERIALIZE_INT_OPTION` (`coap.h:165`) gates only on that bit, never on the value.
- **Two symptoms:**
  1. `api/oc_client_api.c:182` - the outbound well-known GET set Content-Format to the marker. The
     request carries no payload (the comment two lines above says so), so the option described
     nothing.
  2. `api/oc_ri.c:1153` - `(*response_state)->return_content_type = accept`, where `accept` is left
     at `CONTENT_NONE` whenever the request carried no Accept option (`oc_ri.c:1015`).
     `messaging/coap/engine.c:982` then passed that straight to the setter, so **every Block2
     continuation response to an Accept-less request carried the marker as its Content-Format** -
     on a response that does carry a payload. The two neighbouring call sites are guarded
     (`content_format > 0` at `oc_ri.c:1422`, an explicit LINK/CBOR test at `oc_ri.c:1431`);
     this one was not.
- **Why it went unnoticed:** the Python runtime client always sends an explicit Accept, so symptom 2
  is never exercised by the suite. And Content-Format is option 12 - even, therefore *elective* - so
  RFC 7252 clause 5.4.1 requires a conformant peer to ignore it silently rather than reject it. No
  test failure, no error log, no interop break.
- **Specification:** RFC 7252 clause 5.10.3 states "in the absence of this option, no default value
  is assumed", i.e. base CoAP leaves the absent case undefined and provides no "none" value. KNX IoT
  clause 2.2.4 supplies exactly that missing default: "In the absence of the CoAP Accept and
  Content-Format options in a CoAP request or a successful CoAP response, devices SHALL assume the
  default format specified for the corresponding device resource for each of those options."
  So absence is the correct encoding for "no format", and inside a KNX network it is unambiguous.
- **Status:** Resolved.
- **Resolution:** five changes.
  1. `CONTENT_NONE` moved from `99999` to **`64999`**, taken from the 64998-64999 block the IANA CoAP
     Content-Formats registry reserves for documentation: it can never collide with an operationally
     assigned format and it fits the `uint16_t` the packet layer uses, so the truncation is gone.
     The enum carries the reasoning and the "application layer only" rule.
  2. `coap_set_header_content_format()` maps `CONTENT_NONE` to `UNSET_OPTION` instead of setting the
     option, so the marker cannot reach the wire from any caller.
  3. `oc_ri.c:1153` resolves an absent Accept to `matching_resource->content_type[0]` per clause
     2.2.4, instead of propagating the wildcard into a response Content-Format. A sweep confirmed
     all 57 static resource definitions and the application bound ones carry a concrete format in
     slot 0, so the marker is not simply relocated.
  4. `api/oc_client_api.c:182` deleted - `coap_udp_init_message()` already wipes the packet
     (`coap.c:941`), so the option was correctly absent until this call set it.
  5. `coap_get_header_accept()` / `coap_set_header_accept()` retyped from `unsigned int` to
     `oc_content_format_t`, symmetric with the content-format pair and with the same `CONTENT_NONE`
     guard. This removes the cast and shadow variable at `oc_ri.c:1015-1017`.
- **Pinned by:** `CoapTest.ContentFormat_NoneClearsTheOption` and
  `CoapTest.ContentFormat_NoneIsNotSerialized` in `tests/unit/test_coap.cpp` - the second serializes
  and re-parses, so it fails against the original code. Mirrored by `CoapTest.Accept_NoneClearsTheOption`,
  and `CoapTest.Accept_NotSet` now also pins that a caller supplied default survives an absent option,
  which is the contract `oc_ri.c:1015` depends on.
- **Verification:** full unit suite 1412 passed, 0 failed (windows-test-gcc). Full runtime suite
  380 passed, 5 skipped, 0 failed, 0 errors (385 collected, 848.29s, `-IncludeUncommitted`) - fully
  green, identical tally and identical skip set to the run before these changes. Note the runtime
  suite confirms no regression but does not *prove* symptom 2: `coap_client.py` always sends an
  explicit Accept, so the `CONTENT_NONE` branch at `oc_ri.c:1153` is never entered. The unit
  round-trip test is what pins that.

## F-020: `oc_add_functional_blocks_from_application_to_response()` misreports success on a full-before-entry page - RESOLVED

- **File:** `api/oc_knx_fb.c`, `oc_add_functional_blocks_from_application_to_response()`
- **Severity:** Low - reachable only through a caller that both checks the return value and can
  enter the function with the page already exhausted; the one such caller in this codebase never
  hits that state today, so there was no observed behavioral defect, only a latent one.
- **Found by:** inspection while reviewing the function's early-exit branch for the "page is full"
  case:

  ```c
  else if (first_entry + *matches >= last_entry)
  {
    // page is full, leave
    return matches;   // BUG: returns the int* pointer's truthiness, not a real answer
  }
  ```

  `matches` is never NULL at either call site (both pass the address of a local stack `int`), so
  this always evaluates to `true` in C's pointer-to-`_Bool` conversion - regardless of whether
  this call actually appended anything to the response. `oc_knx_fb.h:43-44` documents the contract
  explicitly: "@return true (at least one FB was added)" / "@return false (no FB was added)".
- **Why it was never observed:** the function has two callers.
  `oc_discovery.c:717` (`/.well-known/core`) discards the return value entirely (a bare statement,
  not an `if`), so the bug has zero effect there. `oc_knx_fb.c` (`/f`, the only caller that checks
  the return value) always passes `last_entry = total` rather than a real page-size bound, so the
  "page full" branch can only trigger once all remaining FBs have actually been emitted - meaning
  `*matches` has necessarily increased by the time it is hit, and `true` happens to be the correct
  answer there today. (That `last_entry = total` convention is itself a separate, pre-existing gap:
  `/f`'s `query_ps` currently does not cap its page size through this path. Not fixed here - logged
  as a follow-up, not folded into this fix.)
- **Resolution:** the branch now uses the same predicate the function already applies at its own
  end (`*matches > already_key_value_pair_matches_added_to_response`), so the false-success case is
  no longer reachable at all, independent of any caller's paging convention:

  ```c
  else if (first_entry + *matches >= last_entry)
  {
    // page is full, leave. Report whether THIS call actually appended anything.
    return *matches > already_key_value_pair_matches_added_to_response;
  }
  ```

  The function's own closing `if (*matches > already...) return true; return false;` was then
  collapsed to `return *matches > already_key_value_pair_matches_added_to_response;` - the same
  expression now appears at both return points, unchanged behavior, less code.
- **Pinned by:** two new `CountFunctionalBlocks` cases in `tests/unit/test_oc_knx_fb.cpp`, added
  test-first against the original code: `AddToResponseReturnsFalseWhenPageWasAlreadyFullBeforeThisCall`
  drives `first_entry=0, last_entry=0` with one discoverable FB so the "full" branch is hit on the
  very first eligible resource before anything is emitted - this failed (`true` instead of `false`)
  against the original `return matches;` and passes against the fix. Its companion
  `AddToResponseReturnsTrueWhenThisCallFillsThePageBeforeStopping` drives a real multi-entry
  page (3 distinct FBs, `last_entry=2`) to confirm the fix did not overcorrect into always
  returning `false` for the ordinary paginated case.
- **Enabled by:** an earlier optimization in the same session replaced the fixed 64-slot static
  array backing FB counting/emission (see the removed `g_array_of_different_fbs`,
  `NUM_FBS`) with a predicate scan (`is_first_occurrence_of_fb()`) over the live resource list.
  That also made `oc_add_functional_blocks_from_application_to_response()` unit-testable for the
  first time (it previously only indexed a global array populated by a prior call to
  `oc_count_functional_blocks_from_application()`, with no independent resource traversal of its
  own) - without that change, this finding would have remained integration-only and undetectable
  by the exact repro used here.
- **Verification:** full unit suite 1415 passed, 0 failed (windows-test-gcc). Full runtime suite
  380 passed, 5 skipped, 0 failed, 0 errors (385 collected, 848.18s, `-IncludeUncommitted`) - fully
  green, identical tally and identical skip set to every prior run in this session. As expected,
  the runtime suite does not exercise the corrected branch: the one caller that checks the return
  value (`/f`) always passes `last_entry = total`, so it never enters this function with an
  already-full page. The test-first unit repro is what actually pins the fix.

## F-021: Discovery responses split into two Block2 blocks, which a multicast query cannot reassemble - CLOSED (test-client gap, not a device defect)

- **Files:** `CMakeLists.txt` (`KNX_PAYLOAD_SIZE`), and by consequence
  `api/oc_discovery.c` (`/.well-known/core`) and `api/oc_knx_dev.c` (`/dev`).
- **Severity:** Medium - no memory-safety or correctness defect in the stack; the
  blockwise responses are RFC 7959-correct. The risk is a **certification failure**
  on resource-rich devices, where the discovery body crosses one block.
- **Found by:** runtime EITT tier, template TestCase **5.1.1.3e "Multicast Discovery
  Query with If Wildcard"**, while diagnosing interpreter finding T12
  (`tests/docs/eitt-interpreter-findings.md`).
- **Description:** `ff269ddc` ("Split CoAP header budget into OUTER and INNER") lowered
  `KNX_PAYLOAD_SIZE` from 1024 to 768, which floors `OC_BLOCK_SIZE` at 512. The
  link-format discovery bodies are just over that on the Linux test server:

  | Request | Body | Blocks at SZX=5 (512 B) |
  |---|---|---|
  | `GET /.well-known/core?if=*` (multicast) | 547 | 2 (num=0 more=1, then num=1) |
  | `GET /dev` (OSCORE unicast) | 553 | 2 |

  For **unicast** this is entirely correct and the test client reassembles it (see
  F-017, which withdrew exactly this symptom as a client gap, and T12 for the OSCORE
  path). For a **multicast discovery query** it is a different situation: RFC 7959 5.1
  says a multicast response cannot be continued over multicast, so a client would have
  to open a unicast follow-up to every responder, and a responder cannot reliably serve
  block 1 of a NON multicast response it did not keep state for. The practical effect
  is that a multicast discovery query returns a body cut mid link-format, e.g. ending
  `...;if="urn:knx:if.p urn:kn`, and an `if=` wildcard assertion over the full list
  fails.
- **What the certified reference does:** `tests/eitt/data/traces/5_1.xml` **TelNo 28**
  is the reference device's answer to the same multicast query - a single `NON` 2.05
  `application/link-format` datagram carrying the whole **547-byte** body, with **no
  Block2 and no Size2 option**. So the certified behaviour is one datagram, not two
  blocks.
- **Note:** `ff269ddc` also deleted the comment that had documented this constraint:
  `# (stack needs a minimum of 1k for discovery responses + extra buffer, ...)`.
- **Options for the maintainer** (not decided here - this is a product trade-off
  between PDU size, RFC 7252's 1152-byte guidance for unknown path MTU, and discovery):
  1. Raise `KNX_PAYLOAD_SIZE` back to at least 1024 on platforms whose MTU allows it,
     keeping the smaller budget only where it is actually needed.
  2. Decouple the block size from the payload budget, so `OC_BLOCK_SIZE` can stay at
     1024 for discovery while the per-message buffer stays at 768.
  3. Trim the discovery body (shorter `rt=`/`if=` forms, fewer attributes) so it fits
     one block with headroom.
- **Deliberately NOT masked in the test suite.** A unicast-continuation reassembly was
  written for `CoapClient.multicast_get` and then reverted: the real EITT tool evaluates
  whatever arrives on the multicast query, so a certification run sees the same
  truncated first block. Hiding it client-side would have turned a certification risk
  into a green test. 5.1.1.3 step 2 still passes, but only because its pattern is
  lenient enough to match inside the first block - 5.1.1.3e is the honest signal.
- **Status (2026-09-16): CLOSED as a device finding.** Decision: our EITT interpreter is a CoAP
  client under our control, so it behaves like any blockwise-capable client on the multicast path
  too. `CoapClient.multicast_get` now completes a block-0 answer that carries `more=1` by
  RESTARTING the transfer over unicast to the responder (a bare "Block2 num=1" follow-up is
  answered with a Reset, because the device serves Block2 continuations only from the response
  buffer it created for a unicast block 0, `messaging/coap/engine.c` "processing block2 option") and
  keeps the multicast answer's identity (NON, mid, token, source address) on the reassembled
  response. Result: section 5.1 **132 passed / 0 failed**; 5.1.1.3e passes on the complete 709-byte
  body, and 5.1.1.3 - which the first attempt in 7ad61f58 had broken - passes too. The earlier
  revert (45ca9cb5) rested on the assumption that the real EITT tool evaluates block 0 only; that
  was never verified against the tool and is not this suite's concern. The payload budget of 768
  stays as set on purpose. Tracker T12 is FIXED for all paths.

## F-022: Unbounded recursion in the s-mode write loopback (self-DoS + multicast flood) - RESOLVED

- **File:** `api/oc_knx_client.c`, `oc_send_s_mode_mc_or_uc_message()` - the "checking & updating
  internal group objects" block.
- **Severity:** High - a legal MaC configuration drives the device into unbounded recursion: one
  multicast datagram and one `OC_MAX_APP_DATA_SIZE` `malloc` per level until the stack or the
  message pool is exhausted. It is both a self-DoS and a multicast flood of the installation, and it
  is reachable from a single inbound s-mode write, i.e. by any peer on the group.
- **Found by:** debugging EITT TestCase **5.4.2.2a** against the virtual device
  `apps/knx/eitt/knx_iot_eitt.c`; confirmed from the device log, not by inspection.
- **Reproduction config.** Two group objects share one GA, which is a legal and common KNX pattern
  (an input and its status output on the same GA):

  | GO id | href   | direction                             | GA | cflags     |
  |-------|--------|---------------------------------------|----|------------|
  | 13    | `/p/1` | input, PUT handler `knx_iot_put_lsab` | 2  | 88 (`rwt`) |
  | 14    | `/p/2` | output, GET only                      | 2  | 88 (`rwt`) |

- **Root cause - the internal GO update re-enters the send path.** After a write is transmitted for
  `sending_ga`, the function walks every GO carrying that GA and, for each one with
  `OC_CFLAG_WRITE` and a `put_handler`, SYNCHRONOUSLY invokes the application PUT handler
  ("update internal GOs on any write request"). A product PUT handler is permitted to answer with a
  status write of its own, and that write re-enters `oc_send_s_mode_mc_or_uc_message()`, which
  transmits and then runs the same loopback again. The cycle observed on the device:

  ```
  inbound s-mode write on GA 2
    -> PUT /p/1  (knx_iot_put_lsab_callback)
      -> oc_send_s_mode_mc_or_uc(/p/2, 'w')
        -> multicast on GA 2
          -> loopback finds /p/1 writable
            -> PUT /p/1 -> ... forever
  ```

  There is no depth bound and no termination condition: the recursion is limited only by the call
  stack or the message pool.
- **Log evidence.** The device emitted one multicast per iteration, with consecutive MIDs inside the
  same millisecond:

  ```
  Sending s-mode multicast w   ... mid 40512
  Sending s-mode multicast w   ... mid 40513
  Sending s-mode multicast w   ... mid 40514
  ...
  checking & updating internal group objects   (once per level)
  ```

  Each level also allocates an `OC_MAX_APP_DATA_SIZE` value buffer before recursing, so memory
  pressure grows linearly with depth.
- **Why a per-resource exclusion is not sufficient.** The obvious narrower fix - skip the loopback
  for the GO/resource that originated the send - does not break this cycle, because the cycle runs
  through a DIFFERENT resource each half-turn (`/p/1` -> `/p/2` -> `/p/1`). Excluding only the
  originating href leaves the loop intact.
- **Resolution:** a file-scope re-entrancy flag, `g_in_internal_go_update`, makes the loopback
  strictly single level. The outermost write still updates every internal GO exactly once - the
  legitimate behaviour, including updating a *different* input on the same GA - but a write issued
  from *within* such an update only transmits and does not start a second loopback round. The flag
  is taken by the frame that enters the update and released only by that same frame (`nested_write`),
  so nested frames cannot clear a guard they did not take.
- **Other call sites checked.** `oc_issue_s_mode_message()` and `oc_do_s_mode_message_update()` do
  not contain the loopback pattern - they only encode and transmit. The deferred
  `knx_coap_discovery_response_handler()` also sends (the IPv6-resolved path) but runs no GO table
  walk, so it is not a recursion source. No further guards were needed.
- **Pinned by:** `SmodeLoopback.RecursiveWriteFromPutHandlerIsBounded` in
  `tests/unit/test_smode_loopback_recursion.cpp`, which reproduces the table above with a PUT
  handler that sends on the same GA. Verified as a real repro by removing the guard: the handler
  re-entered **17 times** (stopped only by the test's own safety ceiling) with **17** transmissions,
  against **1** call and **2** transmissions with the guard. Its companion
  `SmodeLoopback.SingleLevelLoopbackStillUpdatesOtherInputs` pins that the guard did not
  over-correct - a second, non-sending input on the same GA is still updated exactly once - and that
  test passes both with and without the guard, so it is not merely detecting the flag's presence.
- **Verification:** full unit suite **1435 passed, 0 failed** (`windows-test-gcc`, 42.26 s). Docker
  device tiers on the working tree (2026-09-15, `linux-test-gcc`): runtime tier **214 passed, 0 failed,
  5 skipped**; EITT tier **171 passed**, the only failures being the known F-021 (5.1.1.3e) and the
  5.2.6.1→5.2.10.1b cluster, which is the `-Tier all` fwv-ordering hazard (eitt-interpreter-findings.md
  O6), not this change — `-Tier eitt -Section 5.2` alone is **109 passed / 5 skipped on both HEAD and
  the working tree**. `tests/runtime/test_observe.py` 19/19 against the working tree.
- **Status:** RESOLVED.

## F-023: Trailing garbage on the second s-mode read answer for a GA with two read-flagged GOs - RESOLVED

- **File:** `api/oc_knx.c`, `oc_core_knx_k_post_handler()` - the `service_r` branch
  ("read with GO table index").
- **Severity:** Medium-High - the device serialises stale heap/stack content onto a multicast
  datagram. It is an information leak of adjacent buffer memory to every listener on the group, and
  the malformed answer is a certification risk. No memory *corruption* occurs (the read stays inside
  the message buffer), but the content is undefined.
- **Found by:** debugging EITT TestCase **5.4.2.2a**; observed twice on the wire
  (Wireshark/EITT), and only after the second GO on GA 2 existed.
- **Reproduction config.** The same two-GO configuration as F-022, both with the r-cflag set:

  | GO id | href   | GA | cflags     |
  |-------|--------|----|------------|
  | 13    | `/p/1` | 2  | 88 (`rwt`) |
  | 14    | `/p/2` | 2  | 88 (`rwt`) |

- **Wire evidence.** An inbound s-mode read (`st=r`, GA 2) was answered correctly with 16 bytes:

  ```
  BF 04 19 11 01 05 BF 07 02 06 61 61 01 F4 FF FF
  ```

  A SECOND multicast `st=a` answer then arrived ~200 ms later, 31 bytes - the same body with 15
  bytes spliced in before the closing `FF FF`:

  ```
  BF 04 19 11 01 05 BF 07 02 06 61 61 01 F4 FF <15 junk bytes> FF FF
  ```

  The 15 junk bytes differed between runs, which is what identified them as uninitialised/stale
  memory rather than a deterministic encoding error:

  ```
  run 1:  CB 01 00 00 A0 E9 1F 33 CB 01 00 00 00 00 00
  run 2:  AB 02 00 00 C0 EF CD 9F AB 02 00 00 A8 33 CF
  ```

- **Root cause - CONFIRMED (not hypothesis): the single global CBOR encoder is re-armed by the act
  of sending, between the two GET handlers.** The encoder is one global
  (`g_encoder` / `g_buf`, `api/oc_rep.c:15-25`). The sequence is:

  1. `oc_ri_invoke_coap_entity_handler()` calls `oc_rep_new(response_buffer.buffer, ...)`
     **exactly once** (`api/oc_ri.c:1175`) before dispatching into the `/k` POST handler.
  2. Loop iteration 1 (GO 13, `/p/1`): the GET handler encodes into the response buffer,
     `oc_prepare_cbor_response()` sets `response_length`, and the answer is transmitted correctly.
  3. **Transmitting calls `oc_send_s_mode_multicast_message()` -> `oc_issue_s_mode_message()` ->
     `oc_init_s_mode_message_update()`, which calls `oc_rep_new(udp_message_update->data +
     COAP_MAX_HEADER_SIZE, OC_BLOCK_SIZE)`** (`api/oc_client_api.c:125`) - re-pointing the global
     encoder at the OUTGOING UDP message buffer - and then writes the whole s-mode envelope
     `{ 4: sia, 5: { 6: st, 7: ga, 1: value } }` into it.
  4. Loop iteration 2 (GO 14, `/p/2`): the GET handler runs with the global encoder **still pointing
     into that already-transmitted message buffer, with a non-empty write cursor**. It appends its
     value after the envelope, and `oc_prepare_cbor_response()` measures the payload from the START
     of the message buffer - so the reported length covers the envelope bytes too. Those bytes are
     then read back out of the previous message and sent as part of the answer.

  A second, independent contributor: `oc_ri_new_request_from_inbound_request()`
  (`api/oc_ri.c:315-332`) copies only `buffer` and `buffer_size` into the per-iteration
  `oc_response_buffer_t`; it never resets `response_length`. Since `response_buffer` is declared once
  outside the loop (`api/oc_knx.c:846`), a GET handler that answers without setting a length inherits
  the previous GO's value.
- **Resolution:** the `service_r` branch re-arms the encoder onto its own response buffer
  immediately before invoking the GET handler, and clears the stale length:

  ```c
  oc_rep_new(response_buffer.buffer, (int)response_buffer.buffer_size);
  response_buffer.response_length = 0;
  ```

  This is the same idiom already used elsewhere in the file (`api/oc_knx.c:599`). It is placed in the
  read branch rather than in `oc_init_s_mode_message_update()` because re-arming the encoder is that
  function's legitimate job - the defect is the `/k` loop's assumption that the encoder it was handed
  survives a send.
- **Pinned by:** `SmodeReadAnswer.SecondReadAnswerOnSameGaHasNoTrailingGarbage` in
  `tests/unit/test_smode_read_answer_payload.cpp`, which drives the real `/k` POST handler over two
  read-flagged GOs on one GA and records the payload length handed to the send path for each answer.
  The test stub deliberately reproduces the real send path's encoder re-arm *and* its envelope write,
  because that side effect IS the defect - without it the test would pass vacuously (it did, on the
  first attempt, until the stub was corrected). Verified against the unfixed code: answer 1 measured
  **4 bytes** and answer 2 measured **18 bytes** - the 4-byte value plus 14 bytes of stale envelope,
  the same shape as the 16-vs-31 seen on the wire - and the second body's final byte was `0xCD`, the
  response buffer's fill pattern, instead of the CBOR break `0xFF`. With the fix both answers measure
  4 bytes and both end on `0xFF`.
- **Verification:** full unit suite **1435 passed, 0 failed** (`windows-test-gcc`, 42.26 s). Docker
  device tiers on the working tree (2026-09-15, `linux-test-gcc`): runtime tier **214 passed, 0 failed,
  5 skipped**; EITT tier **171 passed**, the only failures being the known F-021 (5.1.1.3e) and the
  5.2.6.1→5.2.10.1b cluster, which is the `-Tier all` fwv-ordering hazard (eitt-interpreter-findings.md
  O6), not this change — `-Tier eitt -Section 5.2` alone is **109 passed / 5 skipped on both HEAD and
  the working tree**. `tests/runtime/test_observe.py` 19/19 against the working tree.
- **Status:** RESOLVED.

## F-024: `mdns_socket_listen()` reads past the receive buffer when a question name fills the datagram - RESOLVED

- **File:** `port/mdns/mdns.h`, `mdns_socket_listen()` (vendored third-party `mjansson/mdns` library).
- **Origin:** found and fixed on `feat/sec-hardening` as *their* **F-026** (fuzz harness `fuzz_mdns`, 6-hour
  mutation campaign; the sec-hardening series numbers findings independently of this file). That fix was a
  hunk in `port/mdns.patch` applied to a FetchContent copy of the library; this branch vendors the file
  directly, so the hunk never arrived and the seed replay stayed at 23/24.
- **Severity:** Medium - heap-buffer-overflow **read**, reachable by any unauthenticated peer on the link
  segment with one crafted mDNS datagram. The production listener in `port/dns-sd_mdns.c` uses a fixed
  2048-byte receive buffer with no headroom, which is exactly the vulnerable pattern.
- **Description:** in the question loop, after `mdns_string_skip()` has computed the end `offset` of the
  question name, the code reads the two 16-bit fields `rtype` and `rclass` at that offset without checking
  that four bytes remain in the datagram. A question name that ends at or within three bytes of the buffer
  end makes the parser read past the buffer. Reproducer: a DNS header claiming one question followed by
  31 full 63-byte labels plus one 50-byte label, filling the 2048-byte datagram exactly.
- **Resolution:** `if ((offset + 4) > data_size) break;` before the `rtype`/`rclass` reads, the sec-hardening
  change applied verbatim to the vendored file.
- **Verification:** fuzz seed replay (`ctest --preset linux-fuzz-clang`, Docker) **24/24 passed**, up from
  23/24; the `fuzz_mdns` seed `question_name_fills_datagram` is the regression test. Unit suite
  **1435 passed, 0 failed** (`windows-test-gcc`). Runtime tier (Docker, `linux-test-gcc`, includes the mDNS discovery and malformed-
  discovery tests against the live listener) **236 passed, 0 failed, 9 skipped**. Baseline is 237/8; the
  extra skip is `TestUnicastDiscoveryMalformedLengths::test_unicast_discovery_ia_too_short`, which skips
  itself when it does not observe the DUT's unicast-resolution discovery GET within its window (three of
  its siblings skip the same way in every run). It exercises CoAP unicast discovery, not the mDNS parser.
- **Status:** RESOLVED.

## F-025: `memcpy` / `memcmp` / `memchr` called with a NULL pointer and length 0 (UBSan nonnull-attribute) - OPEN

- **Found by:** the first local run of the new `eitt-asan-memcheck` CI job script (EITT tier against the
  `linux-test-asan` DUT, `UBSAN_OPTIONS=print_stacktrace=1`). The conformance tally was identical to
  gcc (176 passed / 1 failed / 8 skipped / 2 xfailed); the DUT's stderr carried UBSan reports.
- **Severity:** Low - undefined behaviour per C11 7.24.1p2 (the pointer arguments of the `mem*`
  functions must be valid even when the length is 0), no observed misbehaviour. Modern compilers do
  exploit the `nonnull` attribute for optimisation, so a NULL that "works" today is not guaranteed to.
- **Call sites reported:**
  - `api/oc_blockwise.c:262` `oc_blockwise_find_buffer()`: `memcmp(query, oc_string(buffer->uri_query),
    query_len)` with `query == NULL` (no query in the request) and/or an unset `uri_query` string, length 0.
    Both argument positions were reported.
  - `messaging/coap/transactions.c:75` `coap_new_transaction()`: `memcpy(t->token, token, token_len)` with
    `token == NULL`, `token_len == 0` (the comment there says "memcpy can handle 0 bytes", which is the
    misconception).
  - `api/oc_ri.c:430`, `:431`, `:464` query parsing: `memchr(start, ..., end - start)` with `start == NULL`
    and an empty range.
  - `_deps/tinycbor-src/src/cborencoder.c:290` `append_to_buffer()` via `cbor_encode_byte_string(&a, id,
    id_len)` in `security/oc_oscore_context.c:520` `oc_oscore_context_derive_param()`: the empty Sender ID
    is passed as `(NULL, 0)`. Third-party call site; fix on our side by passing a non-NULL pointer to an
    empty buffer.
- **Fix sketch:** guard each `mem*` call with `if (len > 0)` or pass a valid pointer for the empty case;
  the tinycbor one by passing `(const uint8_t*)""` when `id_len == 0`. Then re-run the memcheck job
  script; UBSan must be silent for it to turn green.
- **Status:** OPEN. The `*-memcheck` jobs in `.gitlab-ci.yml` are red on this until fixed (they are
  nightly and `allow_failure`, so the push pipeline is not affected).

## F-026: ~~`<ga>`-scoped access tokens are authorized against a different group address than the one acted upon~~ - RESOLVED

- **Files:** `api/oc_knx_sec.c`, `oc_knx_sec_check_acl()` (~line 2050); `api/oc_knx.c`, `oc_core_knx_k_post_handler()` (~line 790).
- **Severity:** High - authorization bypass, confirmed end-to-end against the live DUT: a `<ga>`-scoped token could write to a group address outside its list.
- **Description:** For a token scoped exactly `OC_ACL_GA`, the ACL gate and the `/k` handler disagreed on which `ga` (key 7) they take when one s map carries the key twice - the gate stopped at the first, the handler keeps the last:

  ```c
  /* oc_knx_sec_check_acl: stopped at the FIRST ga */
  if (s_map->type == OC_REP_INT && s_map->iname == 7) {
    group_address_match = check_access_token_for_group_address(...);
    break;
  }

  /* oc_core_knx_k_post_handler: keeps the LAST ga */
  if (object->iname == 7) {
    received_notification.ga = (uint32_t)object->value.integer;
  }   /* no break */
  ```

  So `7: <allowed>` followed by `7: <foreign>` was authorized against the allowed one and executed with the foreign one. Across *separate* s maps both converged on the last, so only the single-map duplicate-key case diverged.
- **Evidence:** `test_oc_rep.cpp::ParseRep_DuplicateIntegerKeyInNestedMap` (the parser keeps both keys as separate reps) and, at the time, `test_oc_knx_sec.cpp::SecAcl.GaScopeUsesFirstGaWithinOneSMap` (the gate took the first). End-to-end, `test_group_comm.py::TestUnicastWriteDuplicateGroupAddress` sent `{4: sia, 5: {7: 65535, 6: "w", 1: true, 7: 1}}` to `/k` with a token scoped `[65535]`: the DUT answered **2.04 Changed** and wrote `/p/4`, bound to ga=1. Two controls in the same run excluded a provisioning artefact - the ga=65535 write reaches only `/p/1`, and `TestForeignGaBindingIsLive` proves the ga=1 -> `/p/4` binding works when properly scoped.
- **Resolution:** the gate no longer stops at the first ga. It walks every s map and every `ga` key, returns `false` as soon as one is outside the token's list, and returns `true` only if at least one ga was present and all of them were covered. This is fail-closed and order-independent, so it also covers the multi-s-map case. The `/k` handler was left unchanged - the ACL gate is the right place to bound what the payload may address.
- **Pinned by:** `SecAcl.GaScopeRejectsForeignGaAnywhereInOneSMap` (the original bypass, now denied), `SecAcl.GaScopeRejectsForeignGaInASecondSMap` (both orderings across two s maps), `SecAcl.GaScopeAllowsSeveralAuthorizedGasInOneSMap` (guards against over-correcting into "deny on more than one ga"), and the runtime probe `test_group_comm.py::TestUnicastWriteDuplicateGroupAddress::test_duplicate_ga_does_not_bypass_scope`, whose `xfail` marker was removed.
- **Verification:** unit suite **1588 passed, 0 failed**; full runtime tier **207 passed, 43 skipped, 0 failed** (`linux-coverage-gcc`).

## F-027: ~~repeating one property in an `/auth/at` POST satisfies the mandatory-property check~~ - RESOLVED

- **File:** `api/oc_knx_sec.c`, `oc_core_auth_at_post_handler()`.
- **Severity:** Medium - validation bypass on the credential-installation path: it created an AT entry with an id and a scope but no profile and no OSCORE key material. Not remotely reachable, `POST /auth/at` requires an `if.sec`-scoped token.
- **Description:** an entry may only be created once all five mandatory properties are present (id, profile, scope, `cnf:osc:id`, `cnf:osc:ms`). The handler tracked this with a plain **counter** - `current_at_properties`, starting at 1, incremented per recognised property, rejected below 5. Since `oc_parse_rep()` keeps a repeated CBOR map key as separate reps (see F-026), one property counted four times reached the threshold on its own.
- **Evidence:** the hand-encoded payload `{0: {0: "tok", 9: ["if.sec"] ×4}}` was answered **2.01 Created** and `oc_core_items_used_in_auth_at_table()` became 1, although only id and scope were supplied. The control `SecAtPost.CreateWithTooFewPropertiesIsRejected` (id + one scope) was correctly rejected with 4.00, so the check itself was reached.
- **Resolution:** the counter is replaced by the bitmask `at_properties_seen` over `AT_PROP_ID|SCOPE|PROFILE|OSC_ID|OSC_MS`. A repeated key now sets the same bit again and can no longer complete the set.
- **Pinned by:** `SecAtPost.RepeatedPropertyDoesNotSatisfyTheMandatoryCount` (the bypass, now rejected) and `SecAtPost.CreateWithTooFewPropertiesIsRejected`.
- **Verification:** unit suite **1591 passed, 0 failed**; full runtime tier **207 passed, 43 skipped, 0 failed**.

## F-028: an empty OSCORE `kid` matches an unprovisioned AT table slot - RESOLVED

- **File:** `api/oc_knx_sec.c`, `oc_core_find_at_entry_by_osc_id()`.
- **Severity:** Medium-High - the lookup returned an AT entry that was never provisioned. Found while answering the open question of F-027; it is an independent defect that does not need F-027 to trigger.
- **Description:** the match is `oc_byte_string_len(entry->osc_id) == pkt->kid_len && memcmp(...) == 0`. For an inbound message with an empty `kid` (`kid_len == 0`) both operands are zero-length and `memcmp` over 0 bytes returns 0, so **every** slot with an empty `osc_id` matches - including untouched slots. Of the two callers only the first was guarded by `if (coap_pkt->kid_len > 0)`; the echo-response path was not.
- **Evidence:** against a completely empty AT table, a packet with `kid_len == 0` returned slot 0.
- **Resolution:** the function now rejects an empty `kid` itself (`if (pkt->kid_len == 0) return NULL;`) instead of relying on each caller to guard, and the slot scan skips entries without an id.
- **Not claimed:** end-to-end exploitability over the echo path was not demonstrated - the fix was applied once the bogus match was reproduced.
- **Pinned by:** the `AtTable` cases in `test_oc_knx_sec.cpp`, including the empty-`kid` probe and `FindByOscIdMatchAndMiss`, which now marks its entry as provisioned rather than setting `osc_id` alone.
- **Verification:** unit suite **1591 passed, 0 failed**; full runtime tier **207 passed, 43 skipped, 0 failed**.
