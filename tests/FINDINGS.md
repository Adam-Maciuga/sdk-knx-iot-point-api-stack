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
