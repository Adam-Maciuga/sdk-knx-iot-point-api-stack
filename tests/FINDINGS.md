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
  `test_5_4_1_10b_answer_updateonly_go` in `tests/runtime/test_5_4_group_comm.py`
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
  end-to-end in Docker (veth multicast): `test_5_4_1_10b_answer_updateonly_go`
  passes (value stays unchanged with w=0) and the positive case
  `test_5_4_1_10_multicast_answer_updates_value` (cflag=0x90) still updates.

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
- **Found by:** New runtime test `test_5_1_1_900_pagination_follows_next_links_over_all_fbs` in
  `tests/runtime/test_5_1_discovery.py` (`TestDiscoveryPaginationManyFunctionalBlocks`, test ID
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
