# KNX IoT Point API Stack — Test Catalog

> **466 unit tests** (26 files) + **233 runtime tests** (16 files) = **699 total**
>
> Branch: `unit_tests_claude` | Last verified: 244 passed, 1 skipped (Docker CI)

---

## Part 1: Unit Tests (Google Test / C++)

Built with GCC, run via CTest. 24 `.cpp` files in `tests/`.

---

### test_oc_base64.cpp — Base64 Encoding (14 tests)

**Source:** `api/oc_base64.c`

| Test | What it verifies |
|------|-----------------|
| `EmptyInput` | Empty input produces empty output |
| `SingleByte` | 1 byte → 4 base64 chars |
| `TwoBytes` | 2 bytes → correct `=` padding |
| `ThreeBytes` | 3 bytes → 4 chars (no padding) |
| `SixBytes` | 6 bytes → 8 chars |
| `OutputBufferTooSmall` | Returns -1 when output buffer is too short |
| `ValidFourCharNoPadding` | Known encoded string → correct bytes |
| `ValidWithOnePad` | Handles `=` padding correctly |
| `ValidWithTwoPads` | Handles `==` padding correctly |
| `InvalidLength` | Non-multiple-of-4 input rejected |
| `InvalidCharacter` | Returns -1 on non-base64 characters |
| `PaddingInWrongPosition` | Mid-string padding rejected |
| `BinaryData` | encode(decode(x)) round-trip for binary data |
| `AllByteValues` | Round-trip for all 256 byte values |

---

### test_oc_uuid.cpp — UUID Handling (9 tests)

**Source:** `api/oc_uuid.c`

| Test | What it verifies |
|------|-----------------|
| `KnownUuid` (str→uuid) | Standard UUID string parses correctly |
| `UppercaseHex` | Uppercase hex digits accepted |
| `WildcardStar` (str→uuid) | `*` produces all-zeros UUID |
| `KnownUuid` (uuid→str) | Known UUID → correct string with hyphens |
| `AllZeros` | All-zero UUID → "00000000-..." |
| `AllOnes` | All-0xFF UUID → "ffffffff-..." |
| `BufferTooSmall` | Short buffer handled safely |
| `WildcardStar` (uuid→str) | All-zeros → "*" |
| `StringToBinaryToString` | str→uuid→str round-trip |

---

### test_oc_list.cpp — Linked List (29 tests)

**Source:** `util/oc_list.c`

| Test | What it verifies |
|------|-----------------|
| `Init_ListIsEmpty` | New list has length 0, head/tail NULL |
| `Add_SingleItem` | One item: head == tail == item |
| `Add_MultipleItems_OrderPreserved` | Items appended in order |
| `Push_AddsToHead` | Item becomes new head |
| `Push_RemovesDuplicate` | Push existing item moves it to head |
| `Pop_RemovesHead` | Returns and removes first item |
| `Pop_UntilEmpty` | Repeated pop empties list |
| `Pop_EmptyList_ReturnsNull` | No crash on empty list |
| `Chop_RemovesTail` | Returns and removes last item |
| `Chop_SingleItem` | Single-item list → empty |
| `Chop_EmptyList_ReturnsNull` | No crash on empty list |
| `Head_EmptyList_ReturnsNull` | Returns NULL |
| `Tail_EmptyList_ReturnsNull` | Returns NULL |
| `HeadTail_SingleItem_AreSame` | Single item is both head and tail |
| `Remove_Head` | Removes first item, second becomes head |
| `Remove_Middle` | Removes middle item, list stays connected |
| `Remove_Tail` | Removes last item, previous becomes tail |
| `Remove_Nonexistent_NoOp` | No effect, no crash |
| `Remove2_ReturnsItem` | Alternate API returns removed item |
| `Remove2_Nonexistent_ReturnsNull` | Returns NULL |
| `Length_EmptyList` | Returns 0 |
| `Length_AfterAddRemove` | Correct after add/remove |
| `Insert_AfterItem` | Item inserted after given item |
| `Insert_NullPrev_PushesToHead` | Equivalent to push |
| `Insert_AtTail` | Item inserted after tail |
| `Copy_SharesElements` | Copies all items to new list |
| `Copy_EmptyList` | Empty list copies safely |
| `ItemNext_WalkList` | Iteration via next pointers works |
| `ItemNext_Null_ReturnsNull` | NULL input returns NULL |

---

### test_oc_mmem.cpp — Managed Memory (9 tests)

**Source:** `util/oc_mmem.c`

| Test | What it verifies |
|------|-----------------|
| `AllocByte_ReturnsNonNull` | Byte allocation succeeds |
| `AllocByte_MemoryIsWritable` | Allocated memory can be written/read |
| `AllocInt_ReturnsNonNull` | Int allocation succeeds |
| `AllocDouble_ReturnsNonNull` | Double allocation succeeds |
| `AllocNull_ReturnsZero` | NULL/zero input returns 0 |
| `Free_ClearsSize` | Free sets size to 0 |
| `FreeNull_NoOp` | No crash on NULL |
| `MultipleAllocFree` | Multiple alloc/free cycles work |
| `Init_DoubleInit_NoOp` | Double init is safe |

---

### test_oc_helpers.cpp — String & Hex Utilities (63 tests)

**Source:** `api/oc_helpers.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `Uint64ToDec` | 4 | uint64 → decimal string (0, 42, MAX, power of 10) |
| `Uint64ToHex` | 5 | uint64 → hex string (0, small, large, MAX, leading zeros) |
| `ByteArrayToHex` | 4 | Binary → hex conversion, edge cases |
| `HexToByteArray` | 5 | Hex → binary, odd length, empty, round-trip |
| `UriWildcard` | 3 | Detects `*` in URI paths, NULL safety |
| `UriWildcardInt` | 4 | Extracts integer from wildcard URI |
| `UriWildcardString` | 2 | Extracts string from wildcard URI |
| `UriFbValue` | 4 | Extracts FB number/instance from URI |
| `Strnchr` | 3 | Character search with length limit |
| `ToLower` | 4 | String lowercase conversion |
| `ZeroContent` | 4 | Checks if buffer is all zeros |
| `OcStringTest` | 21 | String alloc/free/copy/compare/concat/url-compare |
| `OcMmemTest` | — | *(moved to test_oc_mmem.cpp)* |

---

### test_c_timestamp.cpp — Timestamp Parsing (28 tests)

**Source:** `api/c-timestamp/`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `TimestampParse` | 9 | RFC 3339 parsing: UTC, offsets, fractional, invalid, leap day |
| `TimestampFormat` | 4 | Timestamp → string formatting, buffer too small |
| `TimestampRoundTrip` | 1 | parse → format → parse preserves value |
| `TimestampCompare` | 5 | Less/equal/greater comparisons (sec, nsec) |
| `TimestampValid` | 6 | Validates ranges (nsec, offset min/max) |
| `TimestampToTm` | 3 | Converts to struct tm (epoch, known date, invalid) |

---

### test_oc_rep.cpp — CBOR Representation (36 tests)

**Source:** `api/oc_rep.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `New/EncoderBuf` | 2 | Buffer initialization |
| `SetInt` | 5 | Integer encode/decode (positive, negative, large, wrong key, NULL) |
| `SetBool` | 2 | Boolean true/false |
| `SetDouble/Float` | 2 | Floating point |
| `SetTextString` | 2 | String encode (including NULL) |
| `SetByteString` | 1 | Binary data |
| `MultipleValues` | 1 | Mixed types in single map |
| `Arrays` | 3 | Int/bool/double arrays |
| `ToJson` | 3 | JSON serialization (compact, pretty, size query) |
| `AddLineToBuffer` | 3 | Buffer append helpers |
| `EncodeRaw` | 1 | Raw CBOR append |
| `FreeRep/Parse` | 2 | Cleanup and empty-map parse |
| `TextKey getters` | 6 | Text-keyed getters (int, bool, double, float, string, byte string NULL checks) |
| `GetString/ByteString` | 2 | NULL size parameter handling |

---

### test_coap.cpp — CoAP Parser/Serializer (48 tests)

**Source:** `messaging/coap/coap.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `InitMessage` | 4 | CON/NON/ACK/RST + ClearsFields |
| `Token` | 3 | 4-byte, 8-byte, truncation at 8 |
| `ContentFormat` | 2 | Set/get, not-set default |
| `Accept` | 2 | Set/get, not-set default |
| `MaxAge` | 2 | Set/get, default value |
| `ETag` | 2 | Set/get, not-set |
| `Observe` | 2 | Set/get option 7 |
| `UriPath` | 3 | Set/get, leading slash strip, not-set |
| `Block1/Block2` | 7 | Block options: set/get, invalid size/num, not-set |
| `Size1/Size2` | 3 | Size options |
| `Echo` | 2 | Echo option for freshness |
| `Payload` | 2 | Set/get payload |
| `SetStatusCode` | 2 | Valid codes, overflow protection |
| `OptionHeader` | 4 | Delta encoding (small, 1-byte ext, 2-byte ext, count-only) |
| `RoundTrip` | 7 | Serialize→parse (empty, token, CF, payload, path, observe, ETag) |
| `Parse_WrongVersion` | 1 | Rejects non-CoAP-1.0 |

---

### test_coap_oscore.cpp — OSCORE Option (21 tests)

**Source:** `messaging/coap/oscore.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `PivToSsn` | 5 | Big-endian PIV bytes → uint64 SSN (1/3/5 bytes, zero-length, single zero) |
| `SsnToPiv` | 6 | uint64 SSN → PIV bytes (zero, small, 3-byte, max-4, wrap, power-of-32) |
| `PivSsnRoundTrip` | 2 | Conversion integrity (small, large) |
| `OscoreOption` | 6 | Set/parse OSCORE option (PIV-only, PIV+KID, full, inner variants) |
| `OscoreSerialize` | 2 | Serialization to wire format (PIV-only, full round-trip) |

---

### test_oc_oscore_crypto.cpp — OSCORE Crypto (15 tests)

**Source:** `security/oc_oscore_crypto.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `HkdfSha256` | 4 | Key derivation (basic, empty salt, short output, different IKM) |
| `AeadNonce` | 3 | Nonce construction (basic, XOR with IV, empty ID) |
| `ComposeAAD` | 3 | Additional Authenticated Data (basic, empty, different KID) |
| `OscoreEncrypt` | 5 | AES-CCM round-trip, wrong key/ciphertext/AAD, empty plaintext |

---

### test_oc_oscore_context.cpp — OSCORE Context (12 tests)

**Source:** `security/oc_oscore_context.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `ContextDeriveParam` | 4 | HKDF key derivation (sender key, IV, with context, uniqueness) |
| `OscoreContextTest` | 8 | Add/find/free contexts, bad params, kid_context |

---

### test_oc_knx_sec.cpp — Security Credentials (11 tests)

**Source:** `api/oc_knx_sec.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `AtProfileToString` | 5 | Profile enum → string (oscore, dtls, tls, pase, unknown) |
| `ContainsInterface` | 6 | Interface bitmask matching (exact, subset, no overlap, empty, multi-bit) |

---

### test_oc_replay.cpp — Replay Protection (11 tests)

**Source:** `api/oc_replay.c`

| Test | What it verifies |
|------|-----------------|
| `UnknownClient_ReturnsEcho` | First-seen client triggers echo challenge |
| `AddThenCheck_SameSSN_IsReplay` | Exact SSN reuse detected |
| `AddThenCheck_HigherSSN_IsSynced` | Higher SSN accepted |
| `AddThenCheck_LowerSSN_InWindow_IsSynced` | Lower SSN within window accepted |
| `FarOlderSSN_OutsideWindow_IsEcho` | SSN outside window triggers echo |
| `WindowSlides_OldSSN_Evicted` | Window advancement evicts old SSNs |
| `DifferentClients_IndependentRecords` | Separate tracking per client |
| `SameKID_DifferentContext_AreIndependent` | Context isolation |
| `FreeAll_ClearsState` | Free-all resets everything |
| `EmptyKID_ReturnsEcho` | Empty KID returns echo |
| `ReAdd_ResetsWindow` | Re-adding client resets window |

---

### test_oc_knx_helpers.cpp — KNX Helpers (9 tests)

**Source:** `api/oc_knx_helpers.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `CollectAndRank` | 6 | Status code ranking (ok, override, lower-no-override, worst-wins, not-found, internal-error) |
| `NextPage` | 3 | Pagination URL construction (page 0, 1, large) |

---

### test_oc_endpoint.cpp — Endpoint Handling (16 tests)

**Source:** `api/oc_endpoint.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `EndpointCompare` | 5 | Full compare (same, different port/addr, multicast, NULL) |
| `EndpointCompareAddress` | 3 | Address-only compare (ignores port) |
| `EndpointCopy` | 3 | Deep copy, NULL src/dst safety |
| `IsLinkLocal` | 5 | fe80:: detection, global, loopback, NULL, non-IPv6 |

---

### test_oc_knx_fp.cpp — Comm Flags + GO-Table Search (17 tests)

**Source:** `api/oc_knx_fp.c`

**Comm flags (`oc_cflags_as_string`, 7 tests)**

| Test | What it verifies |
|------|-----------------|
| `AllFlags` | All comm flags → "rwitu" |
| `NoFlags` | No flags → "....." |
| `ReadOnly` | Read → "r...." |
| `WriteOnly` | Write → ".w..." |
| `TransmissionOnly` | Transmission → "...t." |
| `ReadWrite` | Read+Write → "rw..." |
| `InitAndUpdate` | Init+Update → "..i.u" |

**Group-object-table search (`GoTableSearch` fixture, 10 tests)** — regression coverage for the array-bounds fix in `oc_core_find_next_go_table_index_with_ga` (commit b677b04f).

| Test | What it verifies |
|------|-----------------|
| `TotalSizeMatchesBuildConstant` | `oc_core_get_group_object_table_total_size()` == `GOT_MAX_ENTRIES` |
| `GetEntryOutOfBoundsReturnsNull` | Out-of-range index → NULL |
| `FindFirstNoMatchReturnsNeg1` | No matching GA → -1 |
| `FindFirstSingleMatch` | Single matching entry found |
| `FindFirstSkipsEmptySlots` | Empty (id==-1) slots skipped |
| `FindNextWalksAllMatchesThenNeg1` | Iterates every match then -1 |
| `FindNextNegativeCurrentIndexDoesNotUnderflow` | current_index < -1 returns -1 without reading `g_got[negative]` |
| `FindNextCurrentIndexAtOrPastEndReturnsNeg1` | current_index ≥ end → -1 |
| `FindMatchesCorrectGaWithinMultiGaEntry` | Match on any GA in a multi-GA entry |
| `FindIndexFromId` | `oc_core_find_index_in_group_object_table_from_id` |

---

### test_oc_buffer_settings.cpp — Buffer/MTU Settings (9 tests)

**Source:** `api/oc_main.c` (declared in `include/oc_buffer_settings.h`)

| Test | What it verifies |
|------|-----------------|
| `MaxAppDataSizeReturnsKnxPayloadSize` | `oc_get_max_app_data_size()` == `KNX_PAYLOAD_SIZE` |
| `MaxAppDataSizeIsStable` | Repeated calls return the same value |
| `SetMtuSizeBelowMinimumReturnsError` | MTU below `COAP_MAX_HEADER_SIZE+16` → -1 |
| `SetMtuSizeBelowMinimumDoesNotChangeState` | Failed set leaves MTU unchanged |
| `SetMtuSizeValidReturnsZero` | Valid MTU accepted |
| `GetMtuSizeAddsConstantHeaderOffset` | `oc_get_mtu_size()` adds a constant header offset |
| `BlockSizeIsPowerOfTwoInRange` | Block size is a power of two in 16..1024 |
| `LargeMtuYieldsMaxBlockSize` | Large MTU → 1024-byte block |
| `SmallerMtuYieldsSmallerBlockSize` | Smaller MTU → smaller block size |

---

### test_oc_core_res.cpp — Device Hostname (5 tests)

**Source:** `api/oc_core_res.c`

| Test | What it verifies |
|------|-----------------|
| `SetStoresValue` | `oc_core_set_device_hostname()` stores the hostname |
| `SetOverwritesPrevious` | Second set overwrites the first |
| `SetEmptyString` | Empty hostname accepted |
| `ReadAndSetUsesDefaultWhenStorageUnavailable` | Falls back to `knx-<serial>` when storage unconfigured |
| `ReadAndSetDefaultFitsHnameBuffer` | Default hostname fits within `HNAME_SIZE` |

---

### test_oc_knx.cpp — KNX Core (17 tests)

**Source:** `api/oc_knx.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `LsmStateString` | 7 | State enum → string (unloaded, loaded, loading, unloading, load-completing, unknown, error) |
| `LsmEventString` | 5 | Event enum → string (nop, start-loading, load-complete, unload, unknown) |
| `IsRedirectedRequestFrom` | 5 | URL routing classification (NULL, empty, /k, /p, other) |

---

### test_oc_knx_fb.cpp — Functional Block Helpers (8 tests)

**Source:** `api/oc_knx_fb.c`

| Test | What it verifies |
|------|-----------------|
| `SimpleDpa` | "dpa.417.61" → 417 |
| `UrnPrefixed` | "urn:knx:dpa.421.62" → 421 |
| `SingleDigitFb` | "dpa.1.1" → 1 |
| `LargeFbNumber` | "dpa.99999.1" → 99999 |
| `NoDotReturnsNegOne` | "dpa" → -1 |
| `EmptyString` | "" → -1 |
| `DotAtEnd` | "dpa." → 0 |
| `MultipleDotsOnlyFirstMatters` | Correct parsing of first number |

---

### test_oc_timer.cpp — Timer Operations (7 tests)

**Source:** `util/oc_timer.c`

| Test | What it verifies |
|------|-----------------|
| `SetStoresInterval` | Interval stored correctly |
| `NotExpiredImmediately` | Large interval → not expired |
| `ExpiredWithZeroInterval` | Zero interval → immediately expired |
| `ExpiredWithPastStart` | Forced past start → expired |
| `RemainingDecreasesOverTime` | Remaining time decreases |
| `ResetAdvancesStartByInterval` | Reset shifts start forward |
| `RestartUsesCurrentTime` | Restart sets start to now |

---

### test_coap_engine.cpp — Duplicate Detection + History Clear (8 tests)

**Source:** `messaging/coap/engine.c`

| Test | What it verifies |
|------|-----------------|
| `FreshMessageReturnsFalse` | First message with a MID is not duplicate |
| `SameMessageIsDuplicate` | Same MID+endpoint is duplicate |
| `DifferentMidIsNotDuplicate` | Different MID → fresh |
| `DifferentPortIsNotDuplicate` | Same MID, different port → fresh |
| `DifferentAddressIsNotDuplicate` | Same MID, different addr → fresh |
| `HistoryWrapsAround` | Circular buffer evicts oldest entries |
| `ClearRequestHistory.WipedMessageBecomesFreshAgain` | `oc_coap_clear_request_history()` makes a prior MID fresh again |
| `ClearResponseHistory.SafeOnEmptyCache` | `oc_coap_clear_response_history()` is safe on an empty cache |

---

### test_coap_transactions.cpp — Transaction Lifecycle (15 tests)

**Source:** `messaging/coap/transactions.c`

| Test | What it verifies |
|------|-----------------|
| `NewTransactionAllocates` | Creates with correct MID/token/message |
| `GetByMidFindsTransaction` | Lookup by MID works |
| `GetByMidReturnsNullWhenNotFound` | Returns NULL for unknown MID |
| `GetByMidSkipsSModeNon` | s-mode NON excluded from regular lookup |
| `GetByTokenFindsTransaction` | Lookup by token works |
| `GetByTokenReturnsNullWhenNotFound` | Returns NULL for unknown token |
| `GetByTokenSkipsSModeNon` | s-mode NON excluded |
| `GetAnyByMidFindsSModeNon` | "any" variant includes s-mode |
| `GetAnyByTokenFindsRegular` | "any" variant includes regular |
| `ClearRemovesTransaction` | Removes from list and frees |
| `ClearNullIsSafe` | No crash on NULL |
| `FreeAllClearsEverything` | Empties entire list |
| `FreeByEndpointSelectiveRemoval` | Only removes matching endpoint |
| `MultipleTransactionsCoexist` | Multiple tracked simultaneously |
| `ZeroLengthTokenMatch` | Both token_len==0 matches correctly |

---

### test_coap_observe.cpp — Observer Management + Notify (16 tests)

**Source:** `messaging/coap/observe.c`

| Test | What it verifies |
|------|-----------------|
| `AddObserverViaHandler` | Registers observer, count increments |
| `DuplicateObserverReplacesExisting` | Same endpoint+URI replaces |
| `RemoveByClient` | Removes all for an endpoint |
| `RemoveByClientNoMatch` | No-op for unknown endpoint |
| `RemoveByToken` | Removes matching token |
| `RemoveByTokenNoMatch` | No-op for unknown token |
| `RemoveByMid` | Removes by last message ID |
| `RemoveByResource` | Selective removal by resource pointer |
| `FreeAllObservers` | Clears all, count → 0 |
| `MultipleObserversSameResource` | Multiple tracked on one resource |
| `DeregisterViaHandler` | OC_OBSERVE_DEREGISTER through handler |
| `NotifyKObserversNullPayloadIsSafe` | `coap_notify_k_observers(NULL, …)` is a no-op |
| `NotifyKObserversZeroLengthIsSafe` | Zero-length payload is a no-op |
| `NotifyKObserversNoObserversIsSafe` | Empty /k observer list completes safely |
| `NotifyObserversNullResourceReturnsZero` | NULL resource → returns 0 |
| `NotifyObserversNoObserversReturnsZero` | Zero observers short-circuits → 0 |

---

### test_oc_ri.cpp — Resource Interface (35 tests)

**Source:** `api/oc_ri.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `StatusCode` | 9 | oc_status_t → CoAP code mapping |
| `CountInterfaces` | 4 | Bit counting in interface mask |
| `CountScopes` | 3 | Scope bit counting |
| `InterfaceStringFullUrn` | 4 | Index → URN string lookup |
| `GetInterfaceMask` | 4 | URN string → bitmask |
| `LsmStateString` | 6 | State enum → string (duplicate of test_oc_knx.cpp) |
| `LsmEventString` | 5 | Event enum → string (duplicate of test_oc_knx.cpp) |

---

## Part 2: Runtime Tests (Python / pytest)

Tests exercise the **full stack** over real CoAP/OSCORE on IPv6.
The `runtime_test_server` binary runs as a subprocess — no mocks.
16 `.py` files in `tests/runtime/`. 233 test functions (245 collected with parametrize).

**DUT config:** Serial=`00fa10020800`, Password=`2X4W3TE0DFLLS19Y1FCH`, MID=667, IA=`0x1101`, IID=`0x1199887766`

---

### test_5_1_discovery.py — Unicast Discovery (10 tests)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestUnicastDiscovery** | `test_5_1_1_2_unicast_discovery` | 5.1.1.2 |
| **TestUnicastDiscoveryFilters** | `test_5_1_1_4_step1_rt_known_value` | 5.1.1.4 |
| | `test_5_1_1_4_step2_rt_wildcard` | 5.1.1.4 |
| | `test_5_1_1_4_step3_if_known_value` | 5.1.1.4 |
| | `test_5_1_1_4_step4_ep_serial_number` | 5.1.1.4 |
| | `test_5_1_1_4_step5_ep_sn_wildcard` | 5.1.1.4 |
| **TestInvalidUnicastQueries** | `test_5_1_1_4b_invalid_query_key` | 5.1.1.4b |
| | `test_5_1_1_4c_invalid_interface` | 5.1.1.4c |
| | `test_5_1_1_4d_invalid_resource_type` | 5.1.1.4d |
| | `test_5_1_1_4e_if_wildcard` | 5.1.1.4e |

---

### test_5_1_discovery_multicast.py — Multicast Discovery (19 tests)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestMulticastDiscovery** | `test_5_1_1_1_site_local_discovery` | 5.1.1.1 |
| | `test_5_1_1_1_second_request` | 5.1.1.1 |
| **TestMulticastDiscoveryFilters** | `test_5_1_1_3_step1_rt_known_value` | 5.1.1.3 |
| | `test_5_1_1_3_step2_rt_wildcard` | 5.1.1.3 |
| | `test_5_1_1_3_step3_if_known_value` | 5.1.1.3 |
| | `test_5_1_1_3_step4_ep_serial_number` | 5.1.1.3 |
| | `test_5_1_1_3_step5_ep_sn_wildcard` | 5.1.1.3 |
| **TestInvalidMulticastQueries** | `test_5_1_1_3b_invalid_query_key` | 5.1.1.3b |
| | `test_5_1_1_3c_invalid_interface` | 5.1.1.3c |
| | `test_5_1_1_3d_invalid_resource_type` | 5.1.1.3d |
| **TestMulticastInterfaceWildcard** | `test_5_1_1_3e_if_wildcard` | 5.1.1.3e |
| **TestMulticastProgrammingModeDiscovery** | `test_5_1_1_5_pm_on_returns_response` | 5.1.1.5 |
| | `test_5_1_1_5b_pm_off_no_response` | 5.1.1.5b |
| **TestMulticastPMEndpointDiscovery** | `test_5_1_1_6_pm_ep_correct_sn` | 5.1.1.6 |
| | `test_5_1_1_6b_pm_off_no_response` | 5.1.1.6b |
| | `test_5_1_1_6c_pm_on_wrong_sn` | 5.1.1.6c |
| **TestMulticastIADiscovery** | `test_5_1_1_9a_correct_iid_ia` | 5.1.1.9a |
| | `test_5_1_1_9b_wrong_iid` | 5.1.1.9b |
| | `test_5_1_1_9c_wrong_ia` | 5.1.1.9c |

---

### test_5_1_discovery_extended.py — Extended Discovery (14 tests)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestProgrammingModeDiscovery** | `test_5_1_1_5c_pm_unicast_device_in_pm` | 5.1.1.5c |
| | `test_5_1_1_5d_pm_unicast_device_not_in_pm` | 5.1.1.5d |
| | `test_5_1_1_6d_pm_ep_correct_sn` | 5.1.1.6d |
| | `test_5_1_1_6e_pm_ep_not_in_pm` | 5.1.1.6e |
| | `test_5_1_1_6f_pm_ep_wrong_sn` | 5.1.1.6f |
| **TestGroupAddressDiscovery** | `test_5_1_1_7_multicast_ga_single_go` | 5.1.1.7 |
| | `test_5_1_1_7b_multicast_ga_multiple_go` | 5.1.1.7b |
| | `test_5_1_1_7c_multicast_ga_no_match` | 5.1.1.7c |
| | `test_5_1_1_7d_multicast_multi_ga_per_go` | 5.1.1.7d |
| | `test_5_1_1_7e_unicast_matching_ga` | 5.1.1.7e |
| | `test_5_1_1_7f_unicast_multiple_go` | 5.1.1.7f |
| | `test_5_1_1_7g_unicast_no_matching_ga` | 5.1.1.7g |
| | `test_5_1_1_7h_unicast_multi_ga_per_go` | 5.1.1.7h |
| **TestGAWildcard** | `test_5_1_1_8_ga_wildcard_rejected` | 5.1.1.8 |

---

### test_5_1_discovery_mdns.py — mDNS Discovery (16 tests)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestMdnsDiscoverySerialUnconfigured** | `test_5_1_2_1_browse_knx_service` | 5.1.2.1 |
| | `test_5_1_2_1_browse_serial_subtype` | 5.1.2.1 |
| | `test_5_1_2_1_srv_resolution` | 5.1.2.1 |
| | `test_5_1_2_1_aaaa_resolution` | 5.1.2.1 |
| | `test_5_1_2_1_coap_reachability` | 5.1.2.1 |
| **TestMdnsDiscoverySerialConfigured** | `test_5_1_2_2_serial_subtype_configured` | 5.1.2.2 |
| | `test_5_1_2_2_full_chain_configured` | 5.1.2.2 |
| **TestMdnsDiscoveryIA** | `test_5_1_2_3_ia_subtype_discovery` | 5.1.2.3 |
| | `test_5_1_2_3_ia_full_chain` | 5.1.2.3 |
| **TestMdnsDiscoveryIAZero** | `test_5_1_2_3a_ia_zero_subtype` | 5.1.2.3a |
| **TestMdnsDiscoveryIAUnconfigured** | `test_5_1_2_3b_ia_unconfigured_subtype` | 5.1.2.3b |
| **TestMdnsDiscoveryPM** | `test_5_1_2_4_pm_subtype_enabled` | 5.1.2.4 |
| | `test_5_1_2_4_pm_full_chain` | 5.1.2.4 |
| | `test_5_1_2_4_pm_subtype_disabled` | 5.1.2.4 |
| **TestMdnsUnsolicitedPM** | `test_5_1_2_5a_pm_toggle_announcements` | 5.1.2.5a |
| **TestMdnsUnsolicitedIA** | `test_5_1_2_5b_ia_change_announcement` | 5.1.2.5b |

---

### test_5_2_wellknown_knx.py — API Version & IA (5 tests)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestApiVersion** | `test_5_2_1_1_api_version` | 5.2.1.1 |
| **TestRestart** | `test_5_2_2_3_restart_pm_resets` | 5.2.2.3 |
| **TestIaAssignment** | `test_5_2_3_1_set_ia_and_iid` | 5.2.3.1 |
| | `test_5_2_3_1b_get_ia_rejected` | 5.2.3.1b |
| | `test_5_2_3_2_set_ia_iid_and_fid` | 5.2.3.2 |

---

### test_5_2_reset.py — Reset Commands (2 tests)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestFactoryReset** | `test_5_2_2_1_factory_reset` | 5.2.2.1 |
| **TestResetCode7** | `test_5_2_2_2_reset_code_7` | 5.2.2.2 |

---

### test_5_2_fingerprint.py — Fingerprint Resource (5 tests)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestFingerprintChanges** | `test_5_2_4_1_fingerprint_changes` | 5.2.4.1 |
| **TestFingerprintInvalidWrite** | `test_5_2_4_1b_post_fingerprint_rejected` | 5.2.4.1b |
| | `test_5_2_4_1c_put_fingerprint_rejected` | 5.2.4.1c |
| **TestFingerprintLoadingState** | `test_5_2_4_2_fingerprint_loading` | 5.2.4.2 |
| **TestFingerprintUnloadedState** | `test_5_2_4_3_fingerprint_unloaded` | 5.2.4.3 |

---

### test_5_2_device_resources.py — Device Resources (16 tests, 24 collected)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestDevResourceList** | `test_5_2_5_1_dev_link_format` | 5.2.5.1 |
| **TestDevResourceRead** | `test_5_2_6_1_serial_number` | 5.2.6.1 |
| | `test_5_2_6_1_firmware_version` | 5.2.6.1 |
| | `test_5_2_6_1_hardware_type` | 5.2.6.1 |
| | `test_5_2_6_1_model` | 5.2.6.1 |
| | `test_5_2_6_1_sna` | 5.2.6.1 |
| | `test_5_2_6_1_da` | 5.2.6.1 |
| | `test_5_2_6_1_iid` | 5.2.6.1 |
| | `test_5_2_6_1_pm` | 5.2.6.1 |
| | `test_5_2_6_1_mid` | 5.2.6.1 |
| | `test_5_2_6_1_port` | 5.2.6.1 |
| | `test_5_2_6_1_ipv6` | 5.2.6.1 |
| | `test_5_2_6_1_hname` | 5.2.6.1 |
| | `test_5_2_6_1_fid` | 5.2.6.1 |
| **TestDevResourceWrite** | `test_5_2_7_1_write_pm` | 5.2.7.1 |
| **TestDevReadOnlyPutRejected** | `test_5_2_7_1b_put_readonly_rejected` ×9 | 5.2.7.1b |

---

### test_5_2_swu.py — Software Update (3 tests, 7 collected)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestSwuResourceList** | `test_5_2_8_1_swu_link_format` | 5.2.8.1 |
| **TestSwuFirmwareUpdate** | `test_5_2_9_1_swu_firmware_update` | 5.2.9.1 |
| **TestSwuReadOnlyPutRejected** | `test_5_2_10_1b_put_readonly_swu_rejected` ×5 | 5.2.10.1b |

---

### test_5_3_security.py — Security & OSCORE (54 tests)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestSpake2PlusProtocol** | `test_5_3_1_1_full_handshake` | 5.3.1.1 |
| | `test_5_3_1_2_wrong_password_spake` | 5.3.1.2 |
| | `test_5_3_1_2b_wrong_confirm_p` | 5.3.1.2b |
| **TestSpake2PlusResetTypes** | `test_5_3_1_3_spake_across_reset_types` | 5.3.1.3 |
| **TestSecondarySpake** | `test_5_3_1_4a_secured_spake_post_rejected` | 5.3.1.4a |
| **TestAuthResourceList** | `test_5_3_4_1_get_auth_without_oscore_returns_403` | 5.3.4.1 |
| | `test_5_3_4_1_get_auth_link_format` | 5.3.4.1 |
| **TestWriteAccessToken** | `test_5_3_8_1_write_at_and_verify` | 5.3.8.1 |
| | `test_5_3_8_1_write_at_with_group_scope` | 5.3.8.1 |
| | `test_5_3_8_1b_post_auth_at_without_oscore_fails` | 5.3.8.1b |
| | `test_5_3_8_1c_put_auth_at_without_oscore_fails` | 5.3.8.1c |
| | `test_5_3_8_1d_post_at_without_if_sec_scope_fails` | 5.3.8.1d |
| | `test_5_3_8_1e_put_at_without_if_sec_scope_fails` | 5.3.8.1e |
| | `test_5_3_8_1f_put_at_with_if_sec_scope_still_fails` | 5.3.8.1f |
| | `test_5_3_8_2_mixed_scope_rejected` | 5.3.8.2 |
| | `test_5_3_8_3_multi_group_scope` | 5.3.8.3 |
| | `test_5_3_8_4_overwrite_existing_at` | 5.3.8.4 |
| | `test_5_3_8_5_create_multiple_ats_single_write` | 5.3.8.5 |
| | `test_5_3_8_6_update_multiple_ats` | 5.3.8.6 |
| **TestReadAccessTokenList** | `test_5_3_9_1_get_auth_at_link_format` | 5.3.9.1 |
| | `test_5_3_9_3a_get_auth_at_without_oscore_returns_403` | 5.3.9.3a |
| | `test_5_3_9_3b_get_auth_at_without_oscore_configured_device` | 5.3.9.3b |
| **TestReadAccessTokenById** | `test_5_3_10_1_get_at_by_id` | 5.3.10.1 |
| | `test_5_3_10_1_get_nonexistent_at_returns_error` | 5.3.10.1 |
| | `test_5_3_10_2a_get_at_without_oscore_returns_403` | 5.3.10.2a |
| | `test_5_3_10_2b_get_at_with_wrong_scope_returns_403` | 5.3.10.2b |
| **TestDeleteAccessToken** | `test_5_3_11_1_delete_at` | 5.3.11.1 |
| | `test_5_3_11_1_delete_nonexistent_at` | 5.3.11.1 |
| **TestOscoreResourceList** | `test_5_3_12_1_get_auth_o_without_oscore_returns_403` | 5.3.12.1 |
| | `test_5_3_12_1_get_auth_o_link_format` | 5.3.12.1 |
| **TestReplayWindowSize** | `test_5_3_13_1_get_replwdo_without_oscore_returns_403` | 5.3.13.1 |
| | `test_5_3_13_1_get_replwdo` | 5.3.13.1 |
| **TestOscoreDelay** | `test_5_3_15_1_get_osndelay_without_oscore_returns_403` | 5.3.15.1 |
| | `test_5_3_15_1_get_osndelay` | 5.3.15.1 |
| | `test_5_3_16_1_put_osndelay` | 5.3.16.1 |
| **TestAntiReplayWindow** | `test_5_3_17_1_old_ssn_within_window_accepted` | 5.3.17.1 |
| **TestGroupAntiReplayWindow** | `test_5_3_17_3_group_anti_replay_window` | 5.3.17.3 |
| **TestSyncDelayEcho** | `test_5_3_17_4_sync_delay_echo` | 5.3.17.4 |
| **TestInvalidEchoReply** | `test_5_3_17_5_invalid_echo_reply` | 5.3.17.5 |
| **TestGroupOscoreMulticast** | `test_5_3_17_6_receive_device_multicast` | 5.3.17.6 |
| **TestEchoChallenge** | `test_5_3_17_7_echo_on_new_context` | 5.3.17.7 |
| **TestEchoPaseToken** | `test_5_3_17_8_echo_on_pase_at_post` | 5.3.17.8 |
| **TestMessageIntegrity** | `test_5_3_18_1_corrupted_auth_tag_rejected` | 5.3.18.1 |
| | `test_5_3_18_1_different_auth_tag_rejected` | 5.3.18.1 |
| **TestOscoreEncryption32ByteMs** | `test_5_3_19_1_oscore_with_32_byte_master_secret` | 5.3.19.1 |
| | `test_5_3_19_1_oscore_with_16_byte_master_secret` | 5.3.19.1 |
| **TestOscoreMasterSalt** | `test_5_3_19_2_oscore_with_master_salt` | 5.3.19.2 |
| **TestGroupOscoreMasterSaltCtxId** | `test_5_3_19_5_group_oscore_with_salt_and_ctx_id` | 5.3.19.5 |
| **TestOscoreContextId** | `test_5_3_19_6_context_ids_with_master_salt` | 5.3.19.6 |
| **TestAccessControlByScope** | `test_5_3_20_1_limited_scope_can_access_matching_resource` | 5.3.20.1 |
| | `test_5_3_20_1_limited_scope_rejected_for_post_auth_at` | 5.3.20.1 |
| | `test_5_3_20_1_limited_scope_rejected_for_dev_pm` | 5.3.20.1 |
| | `test_5_3_20_1_if_p_scope_can_access_dev_pm` | 5.3.20.1 |
| | `test_5_3_20_1_if_d_scope_can_read_device_info` | 5.3.20.1 |

---

### test_5_4_group_comm.py — Group Communication (19 tests)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestUnicastWrite** | `test_5_4_1_1_unicast_write_updates_value` | 5.4.1.1 |
| **TestUnicastWriteUnauthorized** | `test_5_4_1_1b_unauthorized_ga_returns_forbidden` | 5.4.1.1b |
| **TestMulticastWrite** | `test_5_4_1_2_multicast_write_updates_value` | 5.4.1.2 |
| **TestMulticastRead** | `test_5_4_1_3_multicast_read_gets_answer` | 5.4.1.3 |
| **TestUnicastRead** | `test_5_4_1_4_unicast_read_gets_answer` | 5.4.1.4 |
| **TestUnicastWriteMultipleGO** | `test_5_4_1_5_write_updates_both_datapoints` | 5.4.1.5 |
| **TestTriggerMulticastWrite** | `test_5_4_1_6_trigger_multicast_write` | 5.4.1.6 |
| **TestTriggerFirstGA** | `test_5_4_1_7_sends_first_ga` | 5.4.1.7 |
| **TestNotSendingInLoadingState** | `test_5_4_1_8_no_send_in_loading` | 5.4.1.8 |
| **TestInitFlagStartup** | `test_5_4_1_9_init_flag_sends_read_on_restart` | 5.4.1.9 |
| **TestMulticastResponseUpdate** | `test_5_4_1_10_multicast_answer_updates_value` | 5.4.1.10 |
| **TestMulticastWriteIgnoredLoading** | `test_5_4_1_11_write_ignored_in_loading` | 5.4.1.11 |
| **TestOwnGroupObjectUpdate** | `test_5_4_1_12_self_update_on_transmit` | 5.4.1.12 |
| **TestLongGAReceive** | `test_5_4_1_13_long_ga_write_ga0` | 5.4.1.13 |
| | `test_5_4_1_13_long_ga_write_ga256` | 5.4.1.13 |
| | `test_5_4_1_13_long_ga_write_ga65535` | 5.4.1.13 |
| | `test_5_4_1_13_long_ga_write_ga_max32` | 5.4.1.13 |
| **TestLongGASend** | `test_5_4_1_14_sends_long_ga` | 5.4.1.14 |
| **TestUnicastConfirmable** | `test_5_4_1_15_unicast_sends_con` | 5.4.1.15 |

---

### test_5_5_fp_tables.py — FP Table CRUD (38 tests)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestGroupObjectTableWrite** | `test_5_5_1_1_write_single_go_read_flag` | 5.5.1.1 |
| | `test_5_5_1_2_write_single_go_input_flags` | 5.5.1.2 |
| | `test_5_5_1_3_write_single_go_output_flags` | 5.5.1.3 |
| | `test_5_5_1_4_write_multiple_gos_input` | 5.5.1.4 |
| | `test_5_5_1_5_write_multiple_gos_output` | 5.5.1.5 |
| | `test_5_5_1_6_update_single_go` | 5.5.1.6 |
| | `test_5_5_1_6b_update_nonexisting_go` | 5.5.1.6b |
| | `test_5_5_1_7_update_single_go_output` | 5.5.1.7 |
| | `test_5_5_1_8_delete_go_via_post` | 5.5.1.8 |
| | `test_5_5_1_9_invalid_write_go_empty_ga` | 5.5.1.9 |
| | `test_5_5_1_9b_invalid_update_go_empty_ga` | 5.5.1.9b |
| **TestGroupObjectTableReadList** | `test_5_5_2_1_read_go_list` | 5.5.2.1 |
| **TestGroupObjectTableReadSingle** | `test_5_5_3_1_read_single_go` | 5.5.3.1 |
| **TestGroupObjectTableDelete** | `test_5_5_4_1_delete_single_go` | 5.5.4.1 |
| **TestRecipientTableWrite** | `test_5_5_5_1_write_single_mc_recipient` | 5.5.5.1 |
| | `test_5_5_5_3_write_single_uc_recipient` | 5.5.5.3 |
| | `test_5_5_5_4_write_multiple_recipients` | 5.5.5.4 |
| | `test_5_5_5_5_update_multiple_recipients` | 5.5.5.5 |
| | `test_5_5_5_5a_update_partial_recipients` | 5.5.5.5a |
| | `test_5_5_5_5b_update_nonexisting_recipient` | 5.5.5.5b |
| | `test_5_5_5_6_delete_recipient_via_post` | 5.5.5.6 |
| | `test_5_5_5_7a_write_empty_ga_list` | 5.5.5.7a |
| | `test_5_5_5_8a_write_large_ga_list` | 5.5.5.8a |
| **TestRecipientTableReadList** | `test_5_5_6_1_read_recipient_list` | 5.5.6.1 |
| **TestRecipientTableReadSingle** | `test_5_5_7_1_read_single_recipient` | 5.5.7.1 |
| **TestRecipientTableDelete** | `test_5_5_8_1_delete_single_recipient` | 5.5.8.1 |
| **TestPublisherTableWrite** | `test_5_5_9_1_write_single_mc_publisher` | 5.5.9.1 |
| | `test_5_5_9_3_write_single_uc_publisher` | 5.5.9.3 |
| | `test_5_5_9_4_write_multiple_publishers` | 5.5.9.4 |
| | `test_5_5_9_5_update_multiple_publishers` | 5.5.9.5 |
| | `test_5_5_9_5b_update_nonexisting_publisher` | 5.5.9.5b |
| | `test_5_5_9_6_delete_publisher_via_post` | 5.5.9.6 |
| | `test_5_5_9_7a_write_empty_ga_list` | 5.5.9.7a |
| | `test_5_5_9_8a_write_large_ga_list` | 5.5.9.8a |
| **TestPublisherTableReadList** | `test_5_5_10_1_read_publisher_list` | 5.5.10.1 |
| **TestPublisherTableReadSingle** | `test_5_5_11_1_read_single_publisher` | 5.5.11.1 |
| **TestPublisherTableDeleteSingle** | `test_5_5_12_1_delete_single_publisher` | 5.5.12.1 |
| | `test_5_5_12_2_delete_nonexisting_publisher` | 5.5.12.2 |

---

### test_5_6_app_program.py — Load State Machine (9 tests)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestLsmTransitions** | `test_5_6_1_1_start_loading` | 5.6.1.1 |
| | `test_5_6_1_2_load_complete` | 5.6.1.2 |
| | `test_5_6_1_3_unload` | 5.6.1.3 |
| | `test_5_6_1_4_lsm_persist_loading` | 5.6.1.4 |
| | `test_5_6_1_4_lsm_persist_loaded` | 5.6.1.4 |
| | `test_5_6_1_4_lsm_persist_unloaded` | 5.6.1.4 |
| **TestLsmRead** | `test_5_6_2_1_read_lsm_all_states` | 5.6.2.1 |
| **TestAppProgramList** | `test_5_6_3_1_read_ap_list` | 5.6.3.1 |
| **TestProgramVersion** | `test_5_6_4_1_write_read_pv` | 5.6.4.1 |

---

### test_5_7_functional_blocks.py — Functional Blocks (2 tests)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestFunctionalBlockList** | `test_5_7_1_1_read_fb_list` | 5.7.1.1 |
| **TestFunctionalBlockDatapoints** | `test_5_7_2_1_read_fb_datapoints` | 5.7.2.1 |

---

### test_5_8_parameters.py — Parameters (7 tests)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestParameterPostWrite** | `test_5_8_1_1_write_parameter_via_post` | 5.8.1.1 |
| **TestParameterList** | `test_5_8_2_1_read_parameter_list` | 5.8.2.1 |
| **TestParameterRead** | `test_5_8_3_1_read_parameter` | 5.8.3.1 |
| **TestParameterMetadata** | `test_5_8_3_2_read_all_metadata` | 5.8.3.2 |
| | `test_5_8_3_2_read_metadata_id` | 5.8.3.2 |
| | `test_5_8_3_2_read_metadata_value` | 5.8.3.2 |
| **TestParameterWrite** | `test_5_8_4_1_write_parameter` | 5.8.4.1 |

---

### test_5_10_generic.py — Generic Protocol Behavior (14 tests)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestPaginationPageNumber** | `test_5_10_1_1_page_zero_returns_entries` | 5.10.1.1 |
| | `test_5_10_1_1_page_beyond_range_returns_400` | 5.10.1.1 |
| **TestPaginationPageSize** | `test_5_10_1_2_first_page_with_ps1` | 5.10.1.2 |
| | `test_5_10_1_2_second_page_with_ps2` | 5.10.1.2 |
| | `test_5_10_1_2_beyond_range` | 5.10.1.2 |
| **TestListMetadata** | `test_5_10_1_4_total_fpg` | 5.10.1.4 |
| | `test_5_10_1_4_total_p` | 5.10.1.4 |
| | `test_5_10_1_4_total_auth_at` | 5.10.1.4 |
| **TestInvalidListMetadata** | `test_5_10_1_5_invalid_list_metadata` | 5.10.1.5 |
| **TestPaginationNextPage** | `test_5_10_1_6_follow_next_links` | 5.10.1.6 |
| **TestDptMetadata** | `test_5_10_4_1_read_dpt` | 5.10.4.1 |
| **TestDefaultOptions** | `test_5_10_5_1_accept_omitted` | 5.10.5.1 |
| | `test_5_10_5_2_content_format_omitted` | 5.10.5.2 |
| | `test_5_10_5_3_unknown_critical_option` | 5.10.5.3 |

---

## Known Skips

| Test | Reason |
|------|--------|
| `test_5_1_2_3b_ia_unconfigured_subtype` | Requires fully unconfigured DUT (no IA at all); cannot be achieved mid-suite |

## Known Bugs (from unit tests)

| ID | Module | Description |
|----|--------|-------------|
| ~~F-001~~ | ~~`oc_list`~~ | ~~`oc_list_add_block()` circular link~~ — RESOLVED (function removed upstream) |
| F-002 | `oc_ri` | `oc_ri_get_interface_mask()` returns 1 for empty input |
