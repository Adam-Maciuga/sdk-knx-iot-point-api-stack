# KNX IoT Point API Stack - Test Catalog

> **1350 unit tests** (52 files) + **352 runtime tests** (17 files) = **1702 total**
>
> Branch: `work_in_progress` | Unit: 1350 registered and passed (windows-test-gcc, GCC/CTest, ~38s, 100% passed out of 1350).
> Runtime (Docker `linux-test-gcc`, full suite, working tree incl. uncommitted changes): last full run was 347 passed, 5 skipped, 0 failed, 0 errors (352 collected, 814.53s) - fully green. This run verified the DNS-SD TXT record conformance fix (RFC 6763 clause 6) together with the `zeroconf>=0.151.3` pin. See "Last runtime run status" at the end of Part 2.
> Runtime observe suite (`test_5_4_observe.py`): 19 tests, anchored to spec 2.5.9.x / 2.6.10.1 (non-EITT approximation)

---

## Part 1: Unit Tests (Google Test / C++)

Built with GCC, run via CTest. 52 `.cpp` files in `tests/unit/`.

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

### test_oc_uuid.cpp — UUID Handling (13 tests)

**Source:** `api/oc_uuid.c`

| Test | What it verifies |
|------|-----------------|
| `UuidStrToUuid.KnownUuid` | Standard UUID string parses correctly |
| `UuidStrToUuid.UppercaseHex` | Uppercase hex digits accepted |
| `UuidStrToUuid.WildcardStar` | `*` produces all-zeros UUID |
| `UuidToStr.KnownUuid` | Known UUID → correct string with hyphens |
| `UuidToStr.AllZeros` | All-zero UUID → "00000000-..." |
| `UuidToStr.AllOnes` | All-0xFF UUID → "ffffffff-..." |
| `UuidToStr.BufferTooSmall` | Short buffer handled safely |
| `UuidToStr.WildcardStar` | All-zeros → "*" |
| `UuidRoundTrip.StringToBinaryToString` | str→uuid→str round-trip |
| `UuidGenerate.ProducesValidFormat` | `oc_gen_uuid` output is canonical 8-4-4-4-12 hex |
| `UuidGenerate.ProducesVersion4` | Generated UUID has version nibble set to 4 |
| `UuidGenerate.RoundTripsThroughString` | Generated UUID survives uuid→str→uuid |
| `UuidGenerate.ProducesDistinctValues` | Two generations do not collide |

---

### test_oc_list.cpp — Linked List (27 tests)

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

### test_oc_helpers.cpp — String & Hex Utilities (76 tests)

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
| `OcStringTest` | 24 | String alloc/free/copy/compare/concat/url-compare, hex-string conversion, is-hex-array checks |
| `OcStringArrayTest` | 6 | String-array alloc, add-item (overflow/NULL), join, byte-array add |
| `OcArrayPool` | 1 | Int-array allocation yields usable storage |
| `PrintUint64` | 3 | uint64 decimal/hex printing |

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

### test_oc_rep.cpp — CBOR Representation (46 tests)

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
| `GetCborErrno` | 2 | CBOR errno reporting (clean encode, overflow) |
| `EncodeRawEncoder` | 1 | `oc_rep_encode_raw_encoder()` writes bytes and advances |
| `GetObject/Array` | 7 | Nested object + object-array getters (text/int keys, wrong key, absent mixed array) |

---

### test_coap.cpp — CoAP Parser/Serializer (69 tests)

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
| `RoundTrip` | 10 | Serialize→parse (empty, token, CF, payload, path, observe, ETag) + Block1/Block2 parse independence (F-014) |
| `Parse_WrongVersion` | 1 | Rejects non-CoAP-1.0 |
| `Connection/QueryOptions` | 12 | MID seeding, query-variable lookup, proxy-uri, uri-query + location-query option set/get |
| `CoapPayloadLimitTest` | 6 | Payload vs `OC_MAX_APP_DATA_SIZE`: accepted below/at max-1 (incl. null-terminator), 4.13 at/above max, no silent truncation, empty payload |

---

### test_coap_oscore.cpp — OSCORE Option (24 tests)

**Source:** `messaging/coap/oscore.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `PivToSsn` | 5 | Big-endian PIV bytes → uint64 SSN (1/3/5 bytes, zero-length, single zero) |
| `SsnToPiv` | 6 | uint64 SSN → PIV bytes (zero, small, 3-byte, max-4, wrap, power-of-32) |
| `PivSsnRoundTrip` | 2 | Conversion integrity (small, large) |
| `OscoreOption` | 6 | Set/parse OSCORE option (PIV-only, PIV+KID, full, inner variants) |
| `OscoreSerialize` | 2 | Serialization to wire format (PIV-only, full round-trip) |
| `OscoreIsOscoreMessage` | 3 | Detects OSCORE option presence (true, non-OSCORE option, no options) |

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

### test_oc_oscore_context.cpp — OSCORE Context (18 tests)

**Source:** `security/oc_oscore_context.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `ContextDeriveParam` | 4 | HKDF key derivation (sender key, IV, with context, uniqueness) |
| `OscoreContextTest` | 14 | Add/find/free contexts, bad params, kid_context, find-by-group-address (sender/recipient), LRU recipient eviction, free-by-AT-id |

---

### test_oc_knx_sec.cpp — Security Credentials (17 tests)

**Source:** `api/oc_knx_sec.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `AtProfileToString` | 5 | Profile enum → string (oscore, dtls, tls, pase, unknown) |
| `OscoreConfig` | 2 | Replay-window size is RFC default; OSN delay round-trips |
| `AuthAtEntry` | 3 | AT-table entry access (out-of-bounds NULL, in-bounds entry, positive table size) |
| `GetAtIndex` | 2 | Entry pointer → slot index (NULL → -1) |
| `AtTable` | 5 | Items-used count, find-by-osc-id, delete entry, delete NULL, find-and-remove PASE token |

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

### test_oc_oscore_echo_tx.cpp — S-Mode Echo Replay Defence (24 tests)

**Source:** `security/oc_oscore_engine.c` (unified anchor/seen-responder echo ring, `EchoTxRingTest`)

| Test | What it verifies |
|------|-----------------|
| `StoredSSN_FirstEcho_Accepted` | Echo whose PIV/SSN matches a sent multicast anchor is accepted |
| `MultipleStored_EachResponderAccepted` | Several pending SSN anchors each consume independently |
| `TwoResponders_DifferentKidCtx_BothAccepted` | Two responders with distinct `kid_context` for the same SSN both accepted |
| `SameResponder_SecondEcho_IsReplay` | Exact (ssn, kid, kid_ctx) replay from the same responder dropped after first consume |
| `UnknownSSN_NoStore_IsDropped` | Echo with no matching sent SSN dropped |
| `UnknownSSN_AfterStore_IsDropped` | Mismatching SSN dropped while another is pending |
| `WrongKid_SameSSN_IsDropped` | Matching SSN but wrong `kid` content dropped; genuine kid still accepted |
| `SameKid_DifferentSSN_BothAccepted` | Same kid, different SSNs are independent anchors, both accepted |
| `Attacker_ForgedKidCtxFlood_BeforeValidResponse_ValidStillAccepted` | Forged-`kid_ctx` flood before the genuine echo does not consume the anchor |
| `Attacker_ExactReplay_AfterValidResponse_Rejected` | Exact replay of an already-accepted response rejected on every repeat |
| `Attacker_FloodThenReplay_AllIllegalAttemptsRejected` | Combined flood-before + genuine + replay + flood-after all resolve correctly |
| `Attacker_RingExhaustion_AnchorEvicted_ValidResponseRejected` | 2×RING_SIZE forged probes evict the anchor; documents the DoS cost and mitigation note |
| `OldestEntry_CleanedAfterOverflow` | Ring keeps only the most recent RING_SIZE entries |
| `Clear_DropsPendingEntries` | `oc_oscore_free_all_echo_records` wipes all pending entries |
| `Retain_GetByToken_ReturnsSameMessage` | Retained plaintext s-mode message looked up by CoAP token after ring add-ref/unref |
| `GetByToken_UnknownToken_ReturnsNull` | Lookup by an unrelated token returns null |
| `Retain_FreeAll_ReleasesRetainedMessage` | `free_all` drops the ring's retained-message reference (no leak) |
| `WithinCapacity_AllSendersAndResponsesAccepted` | 32 anchors (half capacity) all store and consume without collision |
| `OverflowEviction_LateEchoRejected_NewAnchorAccepted` | Evicted anchor rejects a late echo; a fresh higher-SSN anchor still works |
| `OverflowMixed_EvictedBlSlot_DoesNotFalseReplay` | An evicted seen-responder slot does not cause a false replay reject on reuse |
| `MultiKid_IndependentSenders_AllAccepted` | Three senders with distinct kids and the same SSN value are fully independent |
| `MultiKid_WrongKidForAnchor_Rejected_CorrectKidAccepted` | Echo with another sender's kid for a matching SSN rejected; correct kid accepted |
| `MultiKid_Overflow_EvictedSenderDropped_OthersAccepted` | Ring overflow evicts one sender's anchor while surviving senders remain accepted |
| `Retain_RingRollOverCleansOldestRetainedMessage` | Ring roll-over past capacity frees the oldest retained message, keeps the newest |

---

### test_smode_refcount.cpp — S-Mode Message Ref-Count Lifecycle (8 tests)

**Source:** `security/oc_oscore_engine.c`, `messaging/coap/transactions.c` (manual replay of add_ref/unref primitives)

| Test | What it verifies |
|------|-----------------|
| `M1_TransactionWithDataDoesNotTouchM1RefCount` | `coap_new_transaction_with_data` only memcpy's the caller's plaintext (M1); its ref_count stays 1 |
| `M1_RefCountUnchangedEvenIfTransactionFails` | Caller's unconditional unref of M1 is the sole release regardless of transaction outcome |
| `M2_MulticastNon_FullLifecycle` | mc-NON: birth → add_ref → clear_transaction → echo-ring retain → send-ref drop → ring release, ends at 0 |
| `M2_MulticastNon_RingRolloverReleases` | Echo-ring roll-over past capacity releases an mc-NON M2 via slot eviction instead of `free_all` |
| `M2_UnicastCon_FullLifecycle` | uc-CON: birth → add_ref → OSCORE send-fn unref → ACK clear, ends at 0 |
| `M2_UnicastCon_RetransmitCycleStaysAtOne` | Each CON retransmit cycle (add_ref then send-fn unref) leaves M2 at 1 until final clear |
| `M2_UnicastNon_FullLifecycle` | uc-NON: birth → add_ref → send-fn unref → ~5s echo window → timeout clear, ends at 0 |
| `M1AndM2_IndependentLifetimes` | Releasing M1 early does not disturb M2's independent ref-count lifecycle |

---

### test_smode_send_refcount.cpp — S-Mode Ref-Count via Real `coap_send_transaction()` (4 tests)

**Source:** `messaging/coap/transactions.c`, exercised through the production `coap_send_transaction()` path with linker `--wrap` stubs for `oc_main_initialized`, `coap_send_message`, and `oc_etimer_restart`

| Test | What it verifies |
|------|-----------------|
| `Via_McNon_FullLifecycle` | mc-NON through the real send path: add_ref → wrapped send unref → immediate clear, M2 freed |
| `Via_UcNon_FullLifecycle` | uc-NON through the real send path: send keeps M2 at 1, retransmit-counter bump then timeout clears it |
| `Via_UcCon_FullLifecycle` | uc-CON through the real send path: send keeps M2 alive for ACK, clear on ACK frees it |
| `Via_UcCon_RetransmitCycle` | `COAP_MAX_RETRANSMIT` real send cycles hold M2 at 1; final timeout clear frees it |

---

### test_oc_knx_helpers.cpp - KNX Helpers (35 tests)

**Source:** `api/oc_knx_helpers.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `CollectAndRank` | 6 | Status code ranking (ok, override, lower-no-override, worst-wins, not-found, internal-error) |
| `NextPageTest` | 3 | Pagination URL construction (page 0, 1, large) |
| `FrameBufferTest` | 6 | Frame integer (pos/zero/neg) and `l=` total/ps query fragments |
| `EvaluateQueryPx` | 3 | `pn`/`ps` pagination query → page-index product |
| `QueryLProcessed` | 6 | `l=` query processing (ps-only, total-only, missing, extra-query bad request) |
| `ParseUint16` | 3 | `oc_parse_uint16_from_base10_string`: valid/leading zeros, rejects NULL, empty, `abc`, `2abc`, `-1`, `+1`, 65536, >5 chars (incl. zero-padded `065535`); no read past `value_len` |
| `ParseSint32` | 2 | `oc_parse_sint32_from_base10_string`: full `0..INT_MAX` range and leading zeros, rejects 2147483648, `-1`, trailing garbage, >10 chars (incl. zero-padded `02147483647`) |
| `ParseUint16Base16` | 3 | `oc_parse_uint16_from_base16_string`: hex incl. upper/lower case and leading zeros, rejects NULL value/result, empty, negative len, `1a2z`, `-1`, `+1`, >4 chars, `g`; no read past `value_len` |
| `ParseUint64` | 3 | `oc_parse_uint64_from_base16_string`: hex incl. full 64-bit range and upper/lower case, rejects NULL value/result, empty, `1a2zz`, `-1`, `+1`, >16 chars, `g`; no read past `value_len` |

---

### test_oc_endpoint.cpp — Endpoint Handling (28 tests)

**Source:** `api/oc_endpoint.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `EndpointCompare` | 5 | Full compare (same, different port/addr, multicast, NULL) |
| `EndpointCompareAddress` | 3 | Address-only compare (ignores port) |
| `EndpointCopy` | 3 | Deep copy, NULL src/dst safety |
| `IsLinkLocal` | 5 | fe80:: detection, global, loopback, NULL, non-IPv6 |
| `NewEndpoint` | 1 | Allocates a zeroed endpoint |
| `FreeEndpoint` | 1 | NULL free is a no-op |
| `EndpointToString` | 3 | coap:// URI formatting, NULL args, non-IPv6 error |
| `EndpointParsePath` | 5 | Path extraction, query strip, missing scheme/path errors |
| `EndpointListCopy` | 2 | Multi-node list copy breaks aliasing; NULL source no-op |

---

### test_oc_knx_fp.cpp — Group Object & FP Tables (52 tests)

**Source:** `api/oc_knx_fp.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `CflagsAsString` | 7 | Comm-flag bitmask → "rwitu" string |
| `GoTableSearch` | 17 | GO-table size/bounds, find-first/next by GA (incl. negative-index underflow guard, commit b677b04f), empty-slot search, cflags/ga-len getters |
| `FpTableSizes` | 5 | Recipient/publisher table size + entry bounds, no-IID default |
| `FpFindIdFromPayload` | 5 | id-from-payload lookup (in-range, above-range, negative, no-match, NULL) |
| `GoTableHref` | 7 | href getter + find-first/next by href, sending-GA lookup |
| `RecipientTable` | 1 | Recipient index-from-id match/miss |
| `BelongsHref` | 3 | href ownership check, non-discoverable skipped |
| `GroupTableStorage` | 7 | Recipient/publisher entry encode-decode round-trip (NON flag as boolean, empty/multiple GA arrays, absent-NON default) |

---

### test_oc_buffer_settings.cpp — Buffer/PDU Settings (29 tests)

**Source:** `api/oc_main.c` (declared in `include/oc_buffer_settings.h`)

Contract under test (see D-001 in `tests/FINDINGS.md`): the argument of
`oc_set_pdu_size()` is the **total CoAP PDU size**, i.e. payload plus the outer
and inner CoAP headers (`2 * COAP_MAX_HEADER_SIZE`, RFC 8613 clause 5.3). It is
stored verbatim, and the payload budget used to derive the block size is
`pdu_size - 2 * COAP_MAX_HEADER_SIZE`.

**Defaults (`PduDefaults`, 5 tests)**

| Test | What it verifies |
|------|-----------------|
| `DefaultPduReservesTwoCoapHeaders` | Default PDU == `KNX_PAYLOAD_SIZE + 2*COAP_MAX_HEADER_SIZE` |
| `DefaultBlockSizeMatchesPayloadSizeCappedAt1024` | Default block size follows the payload size, capped at 1024 |
| `DefaultBlockSizeIsAnEncodableRfc7959Size` | Default block size is a legal SZX 0..6 value (power of two, 16..1024) |
| `DefaultBlockSizeMatchesWhatSetPduSizeWouldDerive` | Static initializer agrees with the runtime setter |
| `DefaultPduExceedsPayloadSize` | Default PDU strictly larger than the payload budget |

**Accessors (`BufferSettings`, 10 tests)**

| Test | What it verifies |
|------|-----------------|
| `MaxAppDataSizeReturnsKnxPayloadSize` | `oc_get_max_app_data_size()` == `KNX_PAYLOAD_SIZE` |
| `MaxAppDataSizeIsStable` | Repeated calls return the same value |
| `SetPduSizeBelowMinimumReturnsError` | PDU below `2*COAP_MAX_HEADER_SIZE+16` → -1 |
| `SetPduSizeBelowMinimumDoesNotChangeState` | Failed set leaves PDU and block size unchanged |
| `SetPduSizeValidReturnsZero` | Valid PDU accepted |
| `GetPduSizeReturnsExactlyWhatWasSet` | `oc_get_pdu_size()` returns the stored value verbatim |
| `SetPduSizeFromGetPduSizeIsIdempotent` | Feeding the getter back into the setter is a fixed point (5 iterations) |
| `BlockSizeIsPowerOfTwoInRange` | Block size is a power of two in 16..1024 |
| `LargePduYieldsMaxBlockSize` | Large PDU → 1024-byte block |
| `SmallerPduYieldsSmallerBlockSize` | Smaller PDU → smaller block size |

**Worst-case OSCORE frame (`PduWorstCaseFrame`, 11 tests)**

| Test | What it verifies |
|------|-----------------|
| `MeasuredOuterHeaderFitsCoapMaxHeaderSize` | Serialized max-option outer frame ≤ `COAP_MAX_HEADER_SIZE` |
| `MeasuredInnerHeaderFitsCoapMaxHeaderSize` | Serialized max-option inner frame ≤ `COAP_MAX_HEADER_SIZE` |
| `DefaultPduHoldsWorstCaseOscoreFrame` | Default PDU holds outer + inner headers + a full encrypted payload |
| `PayloadCarryingFrameStaysWithinMeasuredBudget` | A payload-carrying frame stays within the measured budget |
| `SetPduSizeToDefaultEquivalentReproducesDefaultPduSize` | Setting the default-equivalent argument restores the default PDU |
| `SetPduSizeToDefaultEquivalentReproducesDefaultBlockSize` | ...and restores the default block size |
| `PduBelowTwoHeadersPlusMinBlockIsRejected` | Guard rejects anything below 344 bytes |
| `MinimumAcceptedPduYieldsMinimumBlockSize` | The smallest accepted PDU yields a 16-byte block |
| `GuardAndBlockBudgetUseSameHeaderMultiple` | Guard and payload-budget subtraction use the same `2*` multiple |
| `BlockSizeIsMonotonicInPduSize` | Block size never decreases as the PDU grows |
| `LargePduAlwaysHoldsWorstCaseFrame` | Any accepted large PDU still holds the worst-case frame |

**Message buffer (`PduMessageBuffer`, 2 tests + `PduZzzRestore`, 1 test)**

| Test | What it verifies |
|------|-----------------|
| `AllocatedMessageBufferMatchesConfiguredPduSize` | `oc_allocate_message()` sizes its buffer from `OC_PDU_SIZE` |
| `AllocatedMessageBufferHoldsWorstCaseFrame` | The allocated buffer holds the worst-case OSCORE frame |
| `RestoreDefaultEquivalentPduSize` | Restores global PDU state for subsequent suites |

---

### test_oc_core_res.cpp — Core Resources & Device Info (37 tests)

**Source:** `api/oc_core_res.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `DeviceHostname` | 5 | Set/overwrite/empty hostname, default fallback, `HNAME_SIZE` fit |
| `DeviceInfo` | 19 | Firmware/hardware/app version (hwv stored by reference), manufacturer/model/manufacturing-date setters, ia/iid/fid set-and-store range validation |
| `CoreResourceByIndex` | 3 | Resource-by-index bounds (negative, out-of-range, first) |
| `CheckUrnKnxQuery` | 2 | `urn:knx` query-value prefix match |
| `CheckResourceByRt` | 4 | Resource type match (exact, wildcard, mismatch, no-rt default) |
| `CheckResourceByIf` | 4 | Interface match (exact, wildcard, mismatch, no-if default) |

---

### test_oc_knx_dev.cpp — Device Programming Mode & Resource Handlers (50 tests)

**Source:** `api/oc_knx_dev.c`

**Programming mode (`KnxDeviceProgrammingMode`, 6 tests)**

| Test | What it verifies |
|------|-----------------|
| `SetTrueStoresOnDevice` | `oc_knx_device_set_programming_mode(true)` sets `device->pm` |
| `SetFalseStoresOnDevice` | `oc_knx_device_set_programming_mode(false)` clears `device->pm` |
| `InProgrammingModeReflectsSetTrue` | `oc_knx_device_in_programming_mode()` returns true after set-true |
| `InProgrammingModeReflectsSetFalse` | Getter returns false after set-false |
| `GetterMatchesDeviceField` | Getter mirrors `device->pm` exactly |
| `TogglingIsIdempotentPerValue` | Repeated set of same value is stable |

**Resource handlers (`KnxDevHandlers`, 44 tests)** — GET/PUT handlers for the
device resources (`/dev/sn`, `/dev/hwv`, `/dev/fwv`, `/dev/hwt`, `/dev/model`,
`/dev/hostname`, `/dev/iid`, `/dev/pm`, `/dev/sa`, `/dev/da`, `/dev/fid`,
`/dev/port`, `/dev/mport`, `/dev/ap/pv`, `/dev/mid`, `/dev/ipv6`, dev/ap lists)
exercised directly at handler level: wrong-Accept → bad request, happy-path
content, empty-payload PUT → bad request, and array-size validation.

**Coverage note:** `oc_knx_load_device`, `oc_knx_device_storage_reset`, and
`oc_knx_device_restart` still require a live request pipeline (storage + OSCORE
init / DNS-SD re-register) and are covered by the runtime suite
(`test_5_2_*`, `test_5_2_reset.py`, `test_5_6_app_program.py`).

---

### test_oc_clock.cpp — RFC3339 Clock Helpers (12 tests)

**Source:** `api/oc_clock.c`

| Test | What it verifies |
|------|-----------------|
| `ParseEpochZeroIsZeroTicks` | `1970-01-01T00:00:00Z` → 0 ticks |
| `ParseOneSecondAfterEpoch` | `...00:00:01Z` → `OC_CLOCK_SECOND` ticks |
| `ParseInvalidStringReturnsZero` | Garbage string → 0 |
| `ParseMalformedMonthReturnsZero` | Month 13 → 0 |
| `ParseEmptyStringReturnsZero` | Empty input → 0 |
| `EncodeEpochZero` | 0 ticks → `1970-01-01T00:00:00Z` |
| `EncodeOneSecondAfterEpoch` | `OC_CLOCK_SECOND` ticks → `...00:00:01Z` |
| `EncodeBufferTooSmallReturnsZero` | 5-byte buffer → 0 (error) |
| `RoundTripParseEncodeWholeSeconds` | parse→encode reproduces original string |
| `RoundTripEncodeParseIsStable` | encode→parse reproduces original ticks |
| `CurrentTimeFormatsParseableString` | `oc_clock_time_rfc3339()` emits a parseable string |
| `CurrentTimeBufferTooSmallReturnsZero` | Current-time small buffer → 0 |

---

### test_oc_knx.cpp — KNX Core (32 tests)

**Source:** `api/oc_knx.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `LsmStateString` | 7 | State enum → string (unloaded, loaded, loading, unloading, load-completing, unknown, error) |
| `LsmEventString` | 5 | Event enum → string (nop, start-loading, load-complete, unload, unknown) |
| `IsRedirectedRequestFrom` | 5 | URL routing classification (NULL, empty, /k, /p, other) |
| `KnxLsm` | 1 | `oc_knx_lsm_state()` returns the device LSM field |
| `KnxRuntime` | 3 | Runtime-ready true only when loaded and IID set |
| `KnxHandlers` | 11 | `/.well-known/knx`, `/a/lsm`, `/f` fingerprint, `ldevid`/`idevid` GET handlers (JSON/CBOR/accept errors) |

---

### test_oc_knx_fb.cpp — Functional Block Helpers (12 tests)

**Source:** `api/oc_knx_fb.c`

**Functional-block discovery (12 tests)**

| Group | Tests | What it covers |
|-------|-------|----------------|
| `CheckIfFunctionalBlocksNeedToAdd` | 7 | rt/if query + wildcard matching against `urn:knx:fb`/`ll` |
| `CountFunctionalBlocks` | 5 | Distinct FB counting (number/instance, duplicates, non-discoverable skipped) |

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

### test_coap_engine.cpp — Duplicate Detection + Response Cache (12 tests)

**Source:** `messaging/coap/engine.c`

| Test | What it verifies |
|------|-----------------|
| `DuplicateDetection.FreshMessageReturnsFalse` | First message with a MID is not duplicate |
| `DuplicateDetection.SameMessageIsDuplicate` | Same MID+endpoint is duplicate |
| `DuplicateDetection.DifferentMidIsNotDuplicate` | Different MID → fresh |
| `DuplicateDetection.DifferentPortIsNotDuplicate` | Same MID, different port → fresh |
| `DuplicateDetection.DifferentAddressIsNotDuplicate` | Same MID, different addr → fresh |
| `DuplicateDetection.HistoryWrapsAround` | Circular buffer evicts oldest entries |
| `ClearRequestHistory.WipedMessageBecomesFreshAgain` | `oc_coap_clear_request_history()` makes a prior MID fresh again |
| `ClearResponseHistory.SafeOnEmptyCache` | `oc_coap_clear_response_history()` is safe on an empty cache |
| `ResponseCacheStore.PiggybackedAckIsCached` | Piggybacked ACK response is cached |
| `ResponseCacheStore.EmptyAckIsNotCached` | Empty ACK is not cached |
| `ResponseCacheStore.NonAckIsNotCached` | Non-ACK message is not cached |
| `ResponseCacheStore.ShortMessageIsNotCached` | Too-short message is not cached |

---

### test_coap_transactions.cpp — Transaction Lifecycle (19 tests)

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
| `NewTransactionWithDataCopiesPayload` | `coap_new_transaction_with_data()` copies the payload |
| `NewTransactionWithDataRegistersInList` | Data transaction registered in the list |
| `RegisterAsTransactionHandlerIsCallable` | Transaction-handler registration is callable |
| `InitEngineRegistersHandler` | Engine init registers the transaction handler |

---

### test_coap_observe.cpp — Observer Management + Notify (18 tests)

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
| `GetObserveCounterIsAtLeastFirstNotificationValue` | Observe counter ≥ first-notification value |
| `GetObserveCounterIsStableWithoutNotifications` | Counter stable when no notifications sent |

---

### test_oc_ri.cpp — Resource Interface (82 tests)

**Source:** `api/oc_ri.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `StatusCode` | 9 | oc_status_t → CoAP code mapping |
| `CountInterfaces` | 4 | Bit counting in interface mask |
| `CountScopes` | 3 | Scope bit counting |
| `InterfaceStringFullUrn` | 4 | Index → URN string lookup |
| `LsmStateString` | 6 | State enum → string |
| `LsmEventString` | 5 | Event enum → string |
| `StatusFromCoap` | 5 | CoAP code → oc_status_t (round-trips with StatusCode) |
| `ScopeMask` | 5 | Interface scope bit positions (if.i, if.a, if.swu, unknown, length mismatch) |
| `RiMask` | 7 | Scope/interface name framing (single/multiple bits, truncated URNs) |
| `PrintAclScopes` | 1 | ACL scope printer runs without crashing |
| `NewRequestFromInbound` | 1 | Inbound request rewires response and copies buffer |
| `AcceptHeader` | 4 | Accept-header validation (exact, content-none, NULL, mismatch) |
| `QueryValue` | 5 | Query value lookup (first/middle/last/case-insensitive/missing) |
| `QueryExists` | 3 | Query key existence (pair, key-only, missing) |
| `QueryNthKeyValue` | 1 | Nth query fragment key/value |
| `QueryNthKeyExists` | 1 | Nth query fragment key existence |
| `RiAlloc` | 2 | Allocated resource/resource-data is zeroed |
| `RiAddResource` | 5 | Add-resource validation (NULL, const, no handler, zero period, valid) |
| `RiDeleteResource` | 3 | Delete-resource (NULL, const, removes+frees) |
| `RiClientCb` | 8 | Client-callback find-by-mid/token, validity, get-by-uri/endpoint/method |

---

### test_oc_abort.cpp — Abort / Exit / Assert (6 tests)

**Source:** `port/.../oc_assert.h` (`oc_abort`, `oc_exit`, `OC_ASSERT`) — GoogleTest death tests.

| Test | What it verifies |
|------|-----------------|
| `AbortImplRaisesSigabrt` | Abort implementation raises `SIGABRT` |
| `OcAbortWrapperTerminates` | `oc_abort()` terminates the process |
| `ExitImplExitsWithGivenStatus` | Exit implementation exits with the given status |
| `OcExitWrapperExitsWithStatus` | `oc_exit()` exits with the given status |
| `AssertFalseAborts` | `OC_ASSERT(false)` aborts |
| `AssertTrueIsNoOp` | `OC_ASSERT(true)` is a no-op |

---

### test_oc_blockwise.cpp — Blockwise Transfer (20 tests)

**Source:** `api/oc_blockwise.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `BlockwiseDispatch` | 4 | Outgoing block slicing (offset beyond payload, advances offset, partial/mid slices) |
| `BlockwiseHandle` | 5 | Incoming block reassembly (sequential append, offset/size overflow, gap, duplicate) |
| `BlockwiseAlloc` | 6 | Request/response buffer alloc + lookup by href/token/mid/client-cb |
| `BlockwiseScrub` | 5 | Free/scrub buffers, ref-count-based reclamation, scrub-for-client-cb |

---

### test_oc_buffer.cpp — Message Buffer Pool (8 tests)

**Source:** `api/oc_buffer.c`

| Test | What it verifies |
|------|-----------------|
| `AllocateMessageReturnsInitialisedMessage` | Allocated message is initialised |
| `AllocateMessageDataIsWritableForFullPdu` | Data buffer is writable for a full PDU |
| `AllocateMessageReturnsDistinctBuffers` | Successive allocations are distinct |
| `AddRefIncrementsCounter` | `oc_message_add_ref()` increments ref count |
| `AddRefNullIsNoOp` | `oc_message_add_ref(NULL)` is a no-op (F-004 RESOLVED) |
| `UnrefDecrementsWithoutFreeingWhenStillReferenced` | Unref decrements without freeing while referenced |
| `UnrefAtZeroFreesMessage` | Unref at zero frees the message |
| `UnrefNullIsNoOp` | `oc_message_unref(NULL)` is a no-op |

---

### test_oc_client_api.cpp — Client API (13 tests)

**Source:** `api/oc_client_api.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `ClientApiLF` | 9 | Link-format response parsing: entry count, entry URI, rt/if params, missing param |
| `ClientApiInitMsg` | 4 | s-mode / well-known message init (confirmable + non-confirmable) |

---

### test_oc_discovery.cpp — Discovery (`/.well-known/core`) (234 tests)

**Source:** `api/oc_discovery.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `DiscoveryBase` (add-payload) | 8 | Link-format framing (URI angle brackets, leading comma, rt truncation/urn:knx strip, content types) |
| `DiscoveryBase` (check-request) | 7 | Discoverability + rt-query filtering, first-entry skipping, uninitialised core |
| `WellKnownCoreHandler` | 43 | Accept handling; rt/if/ep/d/l/pn/ps queries; PM, IA, sector errors (including no-matching-points and outside-runtime cases), next-page links, combined filters, multicast suppression |
| `WellKnownCorePmEndpoint` | 32 | Synchronized unicast/multicast PM on/off cases with exact, wildcard, prefix wildcard, internal-substring rejection, mismatching, empty, maximum-length, and overlong SN endpoints |
| `WellKnownCoreSerialNumberEndpoint` | 18 | Synchronized unicast/multicast exact, wildcard, prefix wildcard, internal-substring rejection, mismatching, empty, maximum-length, and overlong SN endpoints |
| `WellKnownCoreIndividualAddressLength` (parameterized) | 110 | `ia=` query address matching across `iid` length 0-10 × `ia` length 0-4 × unicast/multicast (11×5×2) |
| `WellKnownCoreInvalidSectorGa` (parameterized) | 16 | Invalid group-address sector queries (empty, overlong, above-maximum, wildcard depths 0-4) × unicast/multicast, all return 4.00 Bad Request |

---

### test_oc_etimer.cpp — Event Timers (12 tests)

**Source:** `util/oc_etimer.c`

| Test | What it verifies |
|------|-----------------|
| `SetAddsTimerToPendingList` | Set adds timer to the pending list |
| `SetRecordsOwningProcess` | Set records the owning process |
| `ExpirationTimeIsStartPlusInterval` | Expiration = start + interval |
| `ExpiredWhenOwnerCleared` | Timer expires when owner cleared |
| `StopRemovesTimerAndMarksExpired` | Stop removes timer and marks expired |
| `StopMiddleOfListKeepsOthers` | Stopping a middle timer keeps others |
| `AdjustShiftsStartTime` | Adjust shifts the start time |
| `ResetAdvancesStartByInterval` | Reset advances start by one interval |
| `ResetWithNewIntervalChangesInterval` | Reset with a new interval changes it |
| `RestartKeepsTimerPending` | Restart keeps the timer pending |
| `NextExpirationZeroWhenEmpty` | Next-expiration is 0 when none pending |
| `NextExpirationNonZeroWhenPending` | Next-expiration is non-zero when pending |

---

### test_oc_knx_client.cpp — KNX Client Redirect (8 tests)

**Source:** `api/oc_knx_client.c`

| Test | What it verifies |
|------|-----------------|
| `NullRequestReturnsMinusOne` | NULL request → -1 |
| `ZeroLengthPathReturnsMinusOne` | Zero-length path → -1 |
| `KPathIsSmodeReturnsZero` | `/k` path classified as s-mode → 0 |
| `KWithLeadingCharacterStillMatchesFirstByte` | First-byte match drives classification |
| `PPathIsPropertyReturnsOne` | `/p` path classified as property → 1 |
| `PPointPathIsPropertyReturnsOne` | `/p/...` path → 1 |
| `OtherPathReturnsTwo` | Other path → 2 |
| `WellKnownPathReturnsTwo` | `/.well-known/...` → 2 |

---

### test_oc_knx_p.cpp — KNX `/p` Properties Resource (7 tests)

**Source:** `api/oc_knx_p.c`

| Test | What it verifies |
|------|-----------------|
| `KnxPResource.DefinitionUriIsP` | Resource definition URI is `/p` |
| `KnxPGet.WrongAcceptReturnsBadRequest` | GET with wrong Accept → bad request |
| `KnxPGet.NoAppResourcesReturnsBadRequest` | GET with no app resources → bad request |
| `KnxPGet.ContentNoneAcceptIsAccepted` | GET with content-none Accept is accepted |
| `KnxPPost.WrongAcceptReturnsBadRequest` | POST with wrong Accept → bad request |
| `KnxPPost.EmptyPayloadReturnsOk` | POST with empty payload → OK |
| `KnxPPost.UnknownHrefReturnsNotFound` | POST to unknown href → not found |

---

### test_oc_knx_sub.cpp — KNX `/sub` Delete (7 tests)

**Source:** `api/oc_knx_sub.c`

| Test | What it verifies |
|------|-----------------|
| `SetsDeletedStatus` | Delete sets the deleted status |
| `ReportsNoContentFormatAndZeroLength` | Reports no content-format, zero length |
| `RemovesSingleObserver` | Removes a single observer |
| `RemovesAllObserversAcrossResources` | Removes all observers across resources |
| `NoObserversStillSucceeds` | Delete with no observers still succeeds |
| `NullResponseDoesNotCrash` | NULL response does not crash |
| `ResourceDefinitionUriIsSub` | Resource definition URI is `/sub` |

---

### test_oc_knx_swu.cpp — Software Update (75 tests)

**Source:** `api/oc_knx_swu.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `KnxSwuGetInt` | 10 | Integer-valued `/swu` resource GETs (protocol, method, result, state, bytes) + wrong-Accept |
| `KnxSwuGetString` | 4 | String-valued GETs (hwref, lastupdate) + wrong-Accept |
| `KnxSwuGetGated` | 10 | Download-gated GETs (pkgv, pkgname, update) returning not-found / bad-request / OK |
| `KnxSwuPut` | 11 | PUT validation per resource (wrong Accept, missing/non-typed payload, happy path, upgrade-cb invocation) |
| `KnxSwuABlock` | 9 | `/swu` block transfer (Accept, ps param, no-callback, fast-path changed) + `po=0` accepted, leading zero is decimal not octal, value is not read beyond its length |
| `KnxSwuABlockInvalidQuery` | 21 | Parameterised `po`/`ps`/`pkgs` rejection: non-numeric, trailing garbage, negative, zero (ps/pkgs), above INT_MAX, too many digits, empty → all 4.00 with the callback never invoked |
| `KnxSwuList` | 1 | List GET wrong-Accept |
| `KnxSwuSetters` | 7 | Setter values become visible in matching GETs |
| `KnxSwuCbReg` | 2 | Upgrade/download callback registration then trigger |

---

### test_oc_log.cpp — Hex Logging (4 tests)

**Source:** `port/.../oc_log.h` (`OC_LOGbytes` formatting)

| Test | What it verifies |
|------|-----------------|
| `FormatsSingleLineLowercaseHex` | Bytes formatted as single-line lowercase hex |
| `EmptyBufferProducesNoOutput` | Empty buffer produces no output |
| `SixteenBytesStayOnOneLine` | 16 bytes stay on one line |
| `WrapsAfterThirtyTwoBytes` | Output wraps after 32 bytes |

---

### test_oc_main.cpp — Main Callbacks / State (10 tests)

**Source:** `api/oc_main.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `MainCallbacks` | 8 | swu / factory-presets / reset / restart / hostname / programming-mode / lsm-change setter+getter, overwrite |
| `MainState` | 2 | Not-initialised before bootstrap; signal-event-loop without callbacks is a no-op |

---

### test_oc_network_events.cpp — Network Events (2 tests)

**Source:** `api/oc_network_events.c`

| Test | What it verifies |
|------|-----------------|
| `EventWhenProcessNotRunningUnrefsMessage` | Event with process not running unrefs the message |
| `EventWhenProcessNotRunningIsRepeatable` | Repeatable without leaking |

---

### test_oc_network_interface.cpp — Network Interface Enumeration (5 tests)

**Source:** `port/.../ipadapter` (`oc_get_network_interfaces`, filter)

| Test | What it verifies |
|------|-----------------|
| `EnumerateRejectsNullBuffer` | NULL buffer rejected |
| `EnumerateRejectsNonPositiveMax` | Non-positive max rejected |
| `EnumerateReturnsWithinBounds` | Returns a count within bounds |
| `EnumerateRespectsMaxInterfaces` | Respects the max-interfaces cap |
| `FilterSetAndGetRoundTrip` | Interface filter set/get round-trips |

---

### test_oc_process.cpp — Process / Event Loop (13 tests)

**Source:** `util/oc_process.c`

| Test | What it verifies |
|------|-----------------|
| `StartDeliversSynchronousInitEvent` | Start delivers a synchronous init event |
| `StartTwiceDoesNotReinitialize` | Starting twice does not reinitialise |
| `PostQueuesEventDeliveredByRun` | Posted event delivered by run |
| `PostSynchDeliversImmediately` | Synchronous post delivers immediately |
| `PostPropagatesData` | Post propagates the event data |
| `PollSchedulesPollEventDrainedByRun` | Poll schedules a poll event drained by run |
| `PollOnStoppedProcessIsIgnored` | Poll on a stopped process is ignored |
| `NEventsReflectsQueueDepth` | `oc_process_nevents()` reflects queue depth |
| `RunReturnsRemainingEventCount` | Run returns remaining event count |
| `AllocEventReturnsIncreasingIds` | Event allocation returns increasing ids |
| `AllocEventStartsAfterReservedRange` | Allocation starts after the reserved range |
| `ExitStopsRunningProcess` | Exit stops a running process |
| `BroadcastEventReachesRunningProcess` | Broadcast event reaches a running process |

---

### test_oc_random.cpp — Random (5 tests)

**Source:** `port/.../oc_random`

| Test | What it verifies |
|------|-----------------|
| `ValueProducesVaryingOutput` | `oc_random_value()` produces varying output |
| `FillReturnsZeroAndWritesBytes` | Fill returns 0 and writes bytes |
| `FillZeroLengthSucceeds` | Zero-length fill succeeds |
| `TwoFillsDiffer` | Two fills differ |
| `InitIsIdempotent` | Init is idempotent |

---

### test_oc_server_api.cpp — Server API (29 tests)

**Source:** `api/oc_server_api.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `ServerApiQuery` | 8 | Query value/exists lookups, iterate-query walking, NULL request |
| `ServerApiResponse` | 7 | Response preparation (CBOR/JSON/link-format/no-format), ignore-request, NULL-safe builders |
| `ServerApiResource` | 14 | Resource new/bind/handler/interfaces/ACL/periodic/properties/DPT/rt, const-respecting mutators |

---

### test_oc_storage.cpp — Persistent Storage (9 tests)

**Source:** `port/.../oc_storage.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `OcStorageBeforeConfig` | 3 | Config rejects NULL/empty/overlong path; read/write/erase fail without config |
| `OcStorage` | 6 | Write→read round-trip (text/binary), size cap, missing file, erase, overwrite |

---

### test_oc_test_control.cpp — Test Control Trigger (4 tests)

**Source:** `api/oc_test_control.c`

| Test | What it verifies |
|------|-----------------|
| `NullPathReturnsBadRequest` | NULL path → bad request |
| `OverLongPathInvokesSetDpThenBadRequest` | Over-long path invokes set-dp then bad request |
| `OverLongPathWithoutCallbackStillBadRequest` | Over-long path without callback → bad request |
| `DeferredCbWithNoPendingReturnsDone` | Deferred trigger with nothing pending → done |

---

### test_separate.cpp — Separate Responses (6 tests)

**Source:** `messaging/coap/separate.c`

| Test | What it verifies |
|------|-----------------|
| `AcceptNonRequestCreatesStore` | Accepting a non-request creates a store |
| `AcceptSameTokenAndObserveReusesStore` | Same token + observe reuses the store |
| `AcceptDifferentObserveCreatesSecondStore` | Different observe creates a second store |
| `ResumeInitializesResponsePacket` | Resume initialises the response packet |
| `ResumeWithObserveZeroSetsObserveHeader` | Resume with observe 0 sets the observe header |
| `ClearRemovesMiddleStore` | Clear removes a middle store |

---

### test_spake2plus.cpp — SPAKE2+ Handshake (16 tests)

**Source:** `security/oc_spake2plus.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `SpakeEncode` | 5 | uint/string/point encoding (little-endian, length-prefixed, empty) |
| `SpakeLifecycle` | 2 | Init→free succeeds and is repeatable |
| `Spake` | 9 | Parameter exchange, keypair gen, w0/L params, share calc, shared-secret derivation, full handshake agreement, transcript determinism |

---

### test_app_precalculated_spake.cpp — Precalculated SPAKE2+ Record (9 tests)

**Source:** `app_get_precalculated_spake_data()` (`include/oc_knx.h`) + `oc_spake2plus_init_data()` (`api/oc_knx.c`)

| Group | Tests | What it covers |
|-------|-------|----------------|
| `AppPrecalculatedSpake` | 7 | Record getter: non-NULL, valid contents, stable pointer, salt + iteration-count match, L is an uncompressed P-256 point, w0 non-zero |
| `AppSpakeRecord` | 1 | w0 and L re-derived from password + salt + iterations match the record |
| `AppPrecalculatedSpakeInit` | 1 | `oc_spake2plus_init_data()` succeeds with the valid stub record |

---

### Runtime-covered source files (no unit tests)

`api/oc_test_control.c` and `api/oc_knx_p.c` — previously listed here as
runtime-only — now have dedicated unit test files
([test_oc_test_control.cpp](unit/test_oc_test_control.cpp),
[test_oc_knx_p.cpp](unit/test_oc_knx_p.cpp)) that exercise their handlers
directly. End-to-end behaviour over the live CoAP/OSCORE pipeline remains
covered by the runtime conformance suite (Part 2).

---

## Part 2: Runtime Tests (Python / pytest)

Tests exercise the **full stack** over real CoAP/OSCORE on IPv6.
The `runtime_test_server` binary runs as a subprocess - no mocks.
17 `.py` files in `tests/runtime/`. 312 tests collected (with parametrize).
The per-section headers below count unique test functions; the extra collected
items come from parametrized cases in `test_5_2_device_resources.py` (16 -> 24)
and `test_5_2_swu.py` (6 -> 10).

**DUT config:** Serial=`00fa10020800`, Password=`2X4W3TE0DFLLS19Y1FCH`, MID=667, IA=`0x1101`, IID=`0x1199887766`

---

### test_5_1_discovery.py — Unicast Discovery (10 tests, 19 collected)

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

### test_5_1_discovery_mdns.py — mDNS Discovery (16 EITT tests, 32 collected)

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
| **TestMdnsStateSync** | `test_5_1_902_7_state_sync_mdns_alive_in_unloaded` | - (non-EITT) |

> The table above lists the EITT-numbered tests. The module collects 32 tests in
> total; the remainder are the non-EITT `5_1_901_x` (transition audit) and
> `5_1_902_x` (state sync) additions.
>
> `test_5_1_902_7_state_sync_mdns_alive_in_unloaded` guards the deliberate
> absence of a `knx_dns_sd_stop()` call when the LSM enters UNLOADED
> (`api/oc_knx.c`): the unload clears the group tables but leaves sn/ia/iid
> intact, so the device must remain discoverable. The test asserts that no mDNS
> goodbye is emitted, that the advertised subtypes/address/port are unchanged,
> and that the advertised endpoint still answers CoAP.

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

### test_5_2_swu.py — Software Update (8 tests, 29 collected)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestSwuResourceList** | `test_5_2_8_1_swu_link_format` | 5.2.8.1 |
| **TestSwuFirmwareUpdate** | `test_5_2_9_1_swu_firmware_update` | 5.2.9.1 |
| **TestSwuLastUpdateLifecycle** | `test_5_2_9_0_lastupdate_is_mfg_date_before_any_update` | - |
| **TestSwuLastUpdateLifecycle** | `test_lastupdate_changes_after_swu_update` | - |
| **TestSwuLastUpdateLifecycle** | `test_lastupdate_survives_reboot` | - |
| **TestSwuReadOnlyPutRejected** | `test_5_2_10_1b_put_readonly_swu_rejected` ×5 | 5.2.10.1b |
| **TestSwuBlockQueryValidation** | `test_invalid_block_query_returns_400` ×18 | - |
| **TestSwuBlockQueryValidation** | `test_leading_zero_is_accepted` | - |

---

### test_5_3_security.py — Security & OSCORE (71 tests)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestSpake2PlusProtocol** | `test_5_3_1_1_full_handshake` | 5.3.1.1 |
| | `test_5_3_1_2_wrong_password_spake` | 5.3.1.2 |
| | `test_5_3_1_2b_wrong_confirm_p` | 5.3.1.2b |
| **TestSpake2PlusResetTypes** | `test_5_3_1_3_spake_across_reset_types` | 5.3.1.3 |
| **TestSecondarySpake** | `test_5_3_1_4a_secured_spake_post_rejected` | 5.3.1.4a |
| **TestSpake2PlusStateMachine** | `test_5_3_1_5a_credential_before_parameter` | 5.3.1.5a |
| | `test_5_3_1_5b_verification_before_parameter` | 5.3.1.5b |
| | `test_5_3_1_5c_repeated_parameter_mid_handshake` | 5.3.1.5c |
| | `test_5_3_1_5d_parameter_missing_id` | 5.3.1.5d |
| | `test_5_3_1_5e_parameter_missing_rnd` | 5.3.1.5e |
| | `test_5_3_1_5f_ordered_handshake_completes` | 5.3.1.5f |
| | `test_5_3_1_5g_parameter_restart_then_completes` | 5.3.1.5g |
| | `test_5_3_1_5h_parameter_after_credential_restarts` | 5.3.1.5h |
| | `test_5_3_1_5i_repeated_credential_request` | 5.3.1.5i |
| | `test_5_3_1_5j_verification_after_parameter` | 5.3.1.5j |
| | `test_5_3_1_5k_restart_from_credentials_completes` | 5.3.1.5k |
| **TestSpake2PlusBruteForce** | `test_5_3_1_6a_block_after_ten_failures` | 5.3.1.6a |
| | `test_5_3_1_6b_factory_reset_clears_block` | 5.3.1.6b |
| | `test_5_3_1_6c_counter_decreases_over_time` | 5.3.1.6c |
| | `test_5_3_1_6d_unblocks_after_timeout` | 5.3.1.6d |
| | `test_5_3_1_6e_partial_step_does_not_reset_counter` | 5.3.1.6e |
| | `test_5_3_1_6f_full_handshake_resets_counter` | 5.3.1.6f |
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

### test_5_4_group_comm.py — Group Communication (31 tests)

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
| **TestReadOnInitNetworkGate** | `test_read_on_init_up_from_start_sends_read` | 5.4.1.9b (non-EITT) |
| | `test_read_on_init_withheld_while_down` | 5.4.1.9b (non-EITT) |
| | `test_read_on_init_released_after_network_up` | 5.4.1.9b (non-EITT) |
| | `test_read_on_init_abandoned_after_max_retries` | 5.4.1.9b (non-EITT) |
| **TestMulticastResponseUpdate** | `test_5_4_1_10_multicast_answer_updates_value` | 5.4.1.10 |
| **TestMulticastResponseUpdateOnly** | `test_5_4_1_10b_answer_updateonly_go` | 5.4.1.10b (non-EITT) |
| **TestMulticastWriteIgnoredLoading** | `test_5_4_1_11_write_ignored_in_loading` | 5.4.1.11 |
| **TestOwnGroupObjectUpdate** | `test_5_4_1_12_self_update_on_transmit` | 5.4.1.12 |
| **TestLongGAReceive** | `test_5_4_1_13_long_ga_write_ga0` | 5.4.1.13 |
| | `test_5_4_1_13_long_ga_write_ga256` | 5.4.1.13 |
| | `test_5_4_1_13_long_ga_write_ga65535` | 5.4.1.13 |
| | `test_5_4_1_13_long_ga_write_ga_max32` | 5.4.1.13 |
| **TestLongGASend** | `test_5_4_1_14_sends_long_ga` | 5.4.1.14 |
| **TestUnicastConfirmable** | `test_5_4_1_15_unicast_sends_con` | 5.4.1.15 |
| **TestUnicastDiscoveryMalformedLengths** | `test_5_4_1_15_bonus_iid_too_long` | non-EITT |
| | `test_5_4_1_15_bonus_iid_too_short` | non-EITT |
| | `test_5_4_1_15_bonus_ia_too_long` | non-EITT |
| | `test_5_4_1_15_bonus_ia_too_short` | non-EITT |
| | `test_5_4_1_15_bonus_sn_too_long` | non-EITT |
| | `test_5_4_1_15_bonus_sn_too_short` | non-EITT |
| **TestUnicastNonReResolution** | `test_unicast_non_re_resolution` | non-EITT |

---

### test_5_4_observe.py — CoAP Observe / Notifications (19 tests)

> **Spec-backed approximation, NOT an EITT replication.** There is no EITT
> (08_10_5) observe section and `EittProject.xml` has no observe telegrams;
> these tests are anchored to spec 2.5.9.x / 2.6.10.1 and RFC 7641/8613, and
> the `5.4.2.x+` numbering / `OBS-x` IDs are placeholders pending an official
> EITT observe section. See the file header for the documented /k vs /p
> behaviour.

| Group | Test | Spec/ref |
|-------|------|----------|
| **A — Subscription lifecycle** | `test_5_4_2_1_obs_a1_register_returns_current_value` | 2.6.10.1 |
| | `test_5_4_2_2_obs_a2_value_change_notifies` | 2.6.10.1 |
| | `test_5_4_2_3_obs_a3_deregister_stops_notifications` | 2.6.10.1 |
| | `test_5_4_2_4_obs_a3b_delete_sub_removes_subscription` | 2.6.10.1 |
| | `test_5_4_2_5_obs_a4_resubscribe_replaces_by_ip_port` | 2.6.10.1 |
| **B — lt validation** | `test_5_4_3_1_obs_b1_missing_lt_rejected` | 2.5.9.3 / 2.5.11.6 |
| | `test_5_4_3_2_obs_b2_lt_zero_rejected` | 2.5.9.3 / 2.5.11.6 |
| | `test_5_4_3_3_obs_b3_expired_lt_stops_notifications` | 2.5.9.3 / 2.5.11.6 |
| **C — CON/NON** | `test_5_4_4_1_obs_c1_default_confirmable` | 2.5.9.4 |
| | `test_5_4_4_2_obs_c2_non_true_non_confirmable` | 2.5.9.4 |
| **D — /k S-Mode** | `test_5_4_5_1_obs_d1_first_k_notification_sia_only` | 2.5.9.1 |
| | `test_5_4_5_2_obs_d2_subsequent_k_notification_has_s_object` | 2.5.9.1 |
| | `test_5_4_5_3_obs_d3_k_deregister_stops_notifications` | 2.5.9.1 / 2.6.10.1 |
| | `test_5_4_5_4_obs_d4_inbound_post_k_not_echoed` | 2.5.9.1 |
| | `test_5_4_5_5_obs_d5_plain_get_k_no_subsequent` | 2.5.9.1 |
| **E — OSCORE notifications** | `test_5_4_6_1_obs_e1_encrypted_notifications_decrypt` | RFC 8613 |
| | `test_5_4_6_2_obs_e2_notification_aad_binds_request_piv` | RFC 8613 §8.3 |
| **F — Multi-observer** | `test_5_4_7_1_obs_f1_independent_seq_per_observer` | 2.6.10.1 |
| | `test_5_4_7_2_obs_f2_stale_observer_pruned` | RFC 7641 |

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

### test_5_10_generic.py — Generic Protocol Behavior (14 tests, 26 collected)

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

### test_coap_payload_limit.py — Oversized Application Payload (4 tests)

Not an EITT test — verifies that a request whose payload exceeds
`OC_MAX_APP_DATA_SIZE` (`KNX_PAYLOAD_SIZE` = 1024) in a single datagram is
answered with `4.13 Request Entity Too Large` instead of being silently
truncated (KNX IoT Point API spec 2.6.3.3.2, RFC 7252 clause 12.1.2).

| Class | Test | What it covers |
|-------|------|----------------|
| **TestOversizedPayloadRejected** | `test_payload_exactly_max_returns_413` | Payload == max, no room for the null-terminator |
| | `test_payload_above_max_returns_413` | Payload well above max |
| | `test_payload_below_max_is_not_rejected_with_413` | Payload that still fits must not be rejected as too large |
| | `test_dut_still_responsive_after_oversized_request` | DUT survives the oversized request and keeps serving |

---

## Last runtime run status

Full runtime suite (Docker `linux-test-gcc`, working tree of `work_in_progress`
including uncommitted changes via `-IncludeUncommitted`):
**347 passed, 5 skipped, 0 failed, 0 errors** (352 collected, 815.78s) - fully green.

The most recent run re-verified the stack after the buffer/PDU work: the
total-CoAP-PDU contract of `oc_set_pdu_size()`, the power-of-two floor applied
to the default block size in `api/oc_main.c`, the RFC 7959 legality guards added
to `coap_set_header_block1()` / `coap_set_header_block2()` (reject reserved SZX 7
and any non-power-of-two size), and the new `coap_floor_block_size()` helper
applied to the peer-influenced Size1 path in `messaging/coap/engine.c`. All
block-wise traffic (`test_5_2_swu.py`, `test_5_5_fp_tables.py`,
`test_5_6_app_program.py`) passed unchanged, confirming no regression in live
block-wise transfer, discovery, group communication, observe or security.

This run was the first complete verification of the DNS-SD TXT record conformance
fix. Before the fix, five tests in `test_5_1_discovery_mdns.py` failed
deterministically (`test_5_1_2_1_srv_resolution`, `test_5_1_2_1_aaaa_resolution`,
`test_5_1_2_2_full_chain_configured`, `test_5_1_2_3_ia_full_chain`,
`test_5_1_2_4_pm_full_chain`), all with
`Could not resolve service info for <sn>._knx._udp.local.`. The cause was not a
code regression: `tests/runtime/requirements.txt` pinned `zeroconf>=0.100.0` with
no upper bound, so runs silently picked up 0.151.3, which enforces RFC 6763
clause 6 by requiring a TXT record before a service is considered resolvable. The
DUT only emitted a TXT record when a sleep period was configured. The responder
now always emits one - `SP=<seconds>` when sleepy, otherwise the RFC 6763
clause 6.1 empty TXT record - and `requirements.txt` pins
`zeroconf>=0.151.3,<1.0`. See F-012 in `tests/FINDINGS.md`.

Measured progression: the mDNS discovery file went from 5 failed / 27 passed to
**32 passed, 0 failed** (391.06s), and the wider `5_1` slice from 5 failed / 125
passed to **130 passed, 0 failed** (459.31s), before this full-suite run. No test
expectations were changed. The `state_sync` snapshot tests still report
`sp=None` for a non-sleepy device and `sp='300'` after a sleep period is set,
confirming the empty TXT record is read as absence of an `SP` key rather than as
a spurious value.

Every section passed, including `test_5_4_group_comm.py` (`test_5_4_1_7` and the
multicast-provisioning path), the full `test_5_4_observe.py` module, and
`test_5_5_fp_tables.py`. The **intermittent** DUT hang observed in an earlier
session (a `set_lsm cmd=2` LSM -> loaded timeout in the `TestTriggerFirstGA`
setup fixture that cascaded into the rest of `5_4` and `5_5`) did **not**
reproduce on this run, confirming it was DUT instability under cumulative load
rather than a code regression.

This run was the first to exercise the `/swu/lastupdate` lifecycle work: the
DUT-side RTC simulation in `swu_upgrade_complete_cb()`, the flipped
`test_5_2_9_1_swu_firmware_update` step-4 assertion (timestamp must CHANGE after
an update), and the new `TestSwuLastUpdateLifecycle` class. Result:
`test_lastupdate_changes_after_swu_update` PASSED,
`test_lastupdate_survives_reboot` PASSED, and
`test_5_2_9_0_lastupdate_is_mfg_date_before_any_update` was renamed after that
run so conftest's numeric sort places it immediately before
`test_5_2_9_1_swu_firmware_update`; it now asserts the manufacturing date
instead of skipping. Verified in an isolated Docker run
(`-IncludeUncommitted -k lastupdate`): **3 passed, 0 skipped**, meaning the
full-suite skip count drops from 6 to 5 on the next complete run.

Also added after that run: `test_5_1_902_7_state_sync_mdns_alive_in_unloaded`
in `test_5_1_discovery_mdns.py`, covering the removal of the `knx_dns_sd_stop()`
call on the LSM UNLOADED transition. Verified in an isolated Docker run
(`-IncludeUncommitted -k 902_7`): **1 passed**, 37.50s. Measured evidence:
the unload transition captured `goodbyes=0 announces=0`, and the snapshot was
byte-for-byte identical before and after
(`subtypes=['00fa10020800', 'ia1199887766-1101']`, same AAAA, same port), with
`GET /a/lsm` still answering 4.03 on the advertised endpoint.

Of the 5 skips recorded above, all were subsequently run in isolation to
establish whether they represent unverified behaviour. Four pass when run alone
(the three `bonus_*` malformed-length cases, and `6d_unblocks` once
`-RunSlowTests` is passed). Only `test_5_4_1_9_init_flag_sends_read_on_restart`
remains genuinely unexercised. See "Skipped tests" below.

This run was also the first complete verification of the integer-parsing
hardening campaign. Every site that converted a network-supplied string now
routes through the bounded-copy parsers in `api/oc_knx_helpers.c`
(`oc_parse_uint16_from_base10_string`, `oc_parse_uint16_from_base16_string`,
`oc_parse_sint32_from_base10_string`,
`oc_parse_uint64_from_base16_string`), which reject NULL/empty values,
over-length values, a leading sign, trailing garbage and out-of-range values,
and never read past `value_len`. Converted: the 14 `pn`/`ps` pagination guards,
the `?d=urn:knx:g.s.[ga]` group address in `oc_discovery.c`, the hexadecimal
IID/IA of `ep=knx://ia.<iid>.<ia>` in `oc_discovery.c` and `oc_knx_client.c`,
the `po`/`ps`/`pkgs` block parameters in `oc_knx_swu.c`, the observe `lt`
lifetime in `messaging/coap/observe.c`, and both URI-wildcard helpers in
`api/oc_helpers.c`. The run exercised all of them - the logs show live
`knx://ia.1199887766.110f` discovery exchanges plus IID/IA re-resolution -
with zero failures. `get_fb_number_from_dp()` was removed from
`api/oc_knx_fb.c` in the same pass (no production callers).

The unit suite is fully green (1355/1355, ~72s).

---

## Skipped tests

Reasons below are taken from the actual `pytest.skip()` / `skipif` sites in the
test sources, and each was **verified by running the test in isolation** rather
than inferred from the full-suite output. Result: four of the five are not
inherently unrunnable - three pass when run alone, and one is purely a runner
configuration gap that has now been closed.

| Test | Skip mechanism | Verified outcome when run in isolation |
|------|----------------|----------------------------------------|
| `test_5_3_1_6d_unblocks_after_timeout` | Static `@pytest.mark.skipif(os.environ.get("RUN_SLOW_TESTS") is None, ...)` | **PASSES.** Waits out the full 10 x 6 s brute-force lockout (~60 s). The runner had no way to set the variable, so it never ran; `run-in-docker.ps1` now takes `-RunSlowTests`. Measured: `1 passed in 70.21s` via `-RunSlowTests -k 6d_unblocks` |
| `test_5_4_1_15_bonus_iid_too_short` | Conditional, `_assert_rejected()` -> `if not result.get("got_get")` | **PASSES alone** (`1 passed in 15.33s`). The rejection logic is correct; the skip in a full run is load-related, not a defect |
| `test_5_4_1_15_bonus_sn_too_long` | Conditional, same helper | **PASSES alone** (`1 passed in 15.41s`) |
| `test_5_4_1_15_bonus_sn_too_short` | Conditional, same helper | **PASSES alone** (`1 passed in 9.58s`) |
| `test_5_4_1_9_init_flag_sends_read_on_restart` | Conditional, `if len(received) == 0` | **Still skips alone.** Reported reason: `Device did not send init message after restart (may require full process restart)`. `POST /test/restart` re-initialises the stack in-process; it does not reproduce a cold boot, so the init-flag read is not emitted. This one remains genuinely unverified |

**Corrected earlier assessment.** A previous revision of this section claimed the
three `bonus_*` tests were skipped because of a "DUT/transport configuration"
gate; no such gate exists. A later revision then claimed all four conditional
skips were masking possibly-broken behaviour. Isolated runs disprove that for
three of them: they pass. The full-suite skips are a timing artefact - the
`bonus_*` tests wait up to 20 s for a discovery GET, and under cumulative suite
load the DUT does not always answer inside that window. Only
`test_5_4_1_9_init_flag_sends_read_on_restart` is genuinely not exercised.

Running the four conditional tests alone:

```powershell
.\tests\runtime\run-in-docker.ps1 -IncludeUncommitted -k 'bonus_iid_too_short'
.\tests\runtime\run-in-docker.ps1 -IncludeUncommitted -RunSlowTests -k '6d_unblocks'
```

Note the `-k` expression must be a single quoted token; a multi-word expression
such as `-k 'a or b'` is split by PowerShell before it reaches pytest and
collects nothing.

**Resolved: the `bonus_*` asymmetry was load, not payload.** All six
`TestUnicastDiscoveryMalformedLengths` tests share the same fixtures, the same
`_run_reject()` driver and the same `_assert_rejected()` helper. In the full
suite three passed and three skipped, which suggested the malformed payload
itself determined the outcome. Isolated runs disprove that: all three of the
skipping tests pass on their own in 9-16 s. The determining factor is cumulative
suite load against the 20 s discovery-GET window, not the payload. All six
malformed-length rejection cases are therefore **validated**, three of them only
outside a full-suite run.

**Previously listed here, no longer skipped:**
`test_5_2_9_0_lastupdate_is_mfg_date_before_any_update` was renamed so conftest's
numeric sort places it before `test_5_2_9_1_swu_firmware_update`; it now asserts
the manufacturing date and passes. Its `pytest.skip()` guard remains only for
partial `-k` runs where an update may precede it.

## Known Bugs (from unit tests)

| ID | Module | Description |
|----|--------|-------------|
| ~~F-001~~ | ~~`oc_list`~~ | ~~`oc_list_add_block()` circular link~~ — RESOLVED (function removed upstream) |
| ~~F-002~~ | ~~`oc_ri`~~ | ~~`oc_ri_get_interface_mask()` returns 1 for empty input~~ — RESOLVED (function had no production callers; removed with its `GetInterfaceMask` tests) |
| ~~F-003~~ | ~~`oc_core_res`~~ | ~~`oc_core_read_and_set_device_hostname()` reads up to 128 bytes into a 17-byte stack buffer (potential overflow)~~ — RESOLVED (buffer enlarged to `MAX_HNAME_BUFFER_SIZE` = 129; read capped at `MAX_HNAME_BUFFER_SIZE - 1` with a guaranteed trailing `\0`) |
| ~~F-004~~ | ~~`oc_buffer`~~ | ~~`oc_message_add_ref(NULL)` dereferences NULL in the trailing `OC_DBG` (debug builds only)~~ — RESOLVED (`OC_DBG` moved inside the `if (message)` guard; test `BufferPool.AddRefNullIsNoOp` now verifies NULL-safety) |
