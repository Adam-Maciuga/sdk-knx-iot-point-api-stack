# Unit Test Findings

Bugs and issues discovered during unit test development.

## F-001: ~~`oc_list_add_block()` circular link~~ — RESOLVED

- **Status:** Moot — `oc_list_add_block()` was removed from upstream.

## F-002: `oc_ri_get_interface_mask()` returns 1 for empty input instead of `OC_IF_NONE`

- **File:** `api/oc_ri.c`, line ~228
- **Severity:** Low — unlikely to be called with empty strings in practice
- **Found by:** `test_oc_ri.cpp` / `GetInterfaceMask.EmptyUrn`
- **Description:** The lookup loop starts at index 0 where
  `interface_string_full_urn[0]` is `""`. When called with `("", 0)`, the
  condition `0 == strlen("") && strncmp("","",0) == 0` is true, so the function
  returns `1 << 0 = 1` instead of `OC_IF_NONE (0)`.
- **Fix:** Start the loop at `i = 1` (skip the sentinel entry) or guard with
  `if (interface_name_len == 0) return OC_IF_NONE;`.
## F-003: `oc_core_read_and_set_device_hostname()` reads up to 128 bytes into a 17-byte stack buffer

- **File:** `api/oc_core_res.c`, `oc_core_read_and_set_device_hostname()` (~line 113)
- **Severity:** High — stack buffer overflow / memory corruption if the stored
  hostname file is larger than `HNAME_SIZE`.
- **Found by:** Code review while writing `test_oc_core_res.cpp` (not triggered
  by a test — doing so would corrupt the stack).
- **Description:** The local buffer is `char hname[HNAME_SIZE]` where
  `HNAME_SIZE == 4 + SERIAL_NUM_SIZE + 1 == 17`. The function then calls
  `oc_storage_read(KNX_STORAGE_HOSTNAME, (uint8_t *)&hname, 128)`. If a stored
  hostname file contains more than 17 bytes, `fread()` writes up to 128 bytes
  into the 17-byte stack buffer, overflowing it.
- **Fix:** Pass `HNAME_SIZE` (or `sizeof(hname)`) as the size argument to
  `oc_storage_read()` instead of the literal `128`.