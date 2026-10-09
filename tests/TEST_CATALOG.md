# KNX IoT Point API Stack - Test Catalog

> **1718 unit tests** (55 files) + **629 runtime tests** across three tiers = **2347 total**
>
> Runtime tiers, separated into sibling directories on 2026-09-14: **eitt**
> 187 in `tests/eitt/test_eitt.py` (generated from the conformance template) +
> **runtime** 245 in `tests/runtime/` (hand-written, 19 files) + **harness**
> 194 in `tests/eitt/harness_tests/` (interpreter/GUI, no DUT). The shared
> infrastructure they build on is `tests/common/`. The runtime tier was 385 until 2026-09-11, when
> every test the template already covers was removed — see "Runtime tier cleanup"
> below.
>
> Branch: `dev_hname_handling` | Unit (2026-10-09): 1718/1718 passed on `linux-test-gcc` (Docker CI image, serial CTest run) and 1682/1682 on `windows-test-gcc` (GCC/CTest). The 36 `test_dns_sd_mdns.cpp` cases are registered under `if(UNIX)` only, so a windows-test-gcc run has 36 fewer. With `ctest -j8` on Linux, 4 storage tests (`OcStorage` 3, `AtSsnStorage` 1) failed and passed again when run serially: parallel runs share the storage directory.
> Runtime (Docker `linux-test-gcc`, full suite, committed HEAD): last full run was 380 passed, 5 skipped, 0 failed, 0 errors (385 collected, 849.06s) - fully green. Re-verified in this session after starting Docker Desktop; matches the prior baseline exactly. The 5 skips are the documented expected ones: `test_5_3_1_6d_unblocks_after_timeout` and `test_5_4_1_9_init_flag_sends_read_on_restart` (both gated), plus 3 `malformed-discovery` timing-artifact skips under cumulative load (see README notes). See "Last runtime run status" at the end of Part 2.
> Runtime observe suite (`test_observe.py`): 19 tests, anchored to spec 2.5.9.x / 2.6.10.1 (non-EITT approximation)

---

## Part 1: Unit Tests (Google Test / C++)

Built with GCC, run via CTest. 55 `.cpp` files in `tests/unit/`.

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

### test_oc_uuid.cpp — UUID Handling (19 tests)

**Source:** `api/oc_uuid.c`

| Test | What it verifies |
|------|-----------------|
| `UuidStrToUuid.KnownUuid` | Standard UUID string parses correctly |
| `UuidStrToUuid.UppercaseHex` | Uppercase hex digits accepted |
| `UuidStrToUuid.WildcardStar` | `*` produces all-zeros UUID |
| `UuidStrToUuid.ShorterThanUuidLenDoesNotOverread` | String shorter than 36 bytes leaves `id` zeroed, no OOB read |
| `UuidStrToUuid.HighBitByteDoesNotMisbehave` | High-bit byte with no canonical dashes is rejected, `id` zeroed |
| `UuidStrToUuid.TooFewDashesDoesNotOverwrite` | Non-canonical dash placement rejected before any byte write; stack canary intact |
| `UuidStrToUuid.InvalidHexDigitIsRejected` | Canonical dashes but a non-hex character → `id` zeroed |
| `UuidToStr.KnownUuid` | Known UUID → correct string with hyphens |
| `UuidToStr.AllZeros` | All-zero UUID → "00000000-..." |
| `UuidToStr.AllOnes` | All-0xFF UUID → "ffffffff-..." |
| `UuidToStr.BufferTooSmall` | Short buffer handled safely |
| `UuidToStr.WildcardStar` | All-zeros → "*" |
| `UuidToStr.NullUuidDoesNotCrash` | `NULL` uuid pointer leaves the buffer untouched |
| `UuidToStr.IdZeroButNotAllZeroRemainderFormatsNormally` | `id[0]==0x2a` with a non-zero remainder formats as normal hex, not "*" |
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

### test_oc_mmem.cpp — Managed Memory (11 tests)

**Source:** `util/oc_mmem.c`

| Test | What it verifies |
|------|-----------------|
| `AllocByte_ReturnsNonNull` | Byte allocation succeeds |
| `AllocByte_MemoryIsWritable` | Allocated memory can be written/read |
| `AllocInt_ReturnsNonNull` | Int allocation succeeds |
| `AllocFloat_ReturnsNonNull` | Float allocation succeeds; regression for missing `break` falling through to `DOUBLE_POOL` |
| `AllocFloat_MemoryIsWritable` | Allocated float memory can be written/read |
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

### test_oc_rep.cpp — CBOR Representation (67 tests)

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
| `StringArray/ByteStringArray truncation` | 5 | Text/byte string array items at exactly the 32-byte slot limit round-trip unmodified; oversized items are truncated (not left as heap-uninitialized garbage) and never overflow the fixed-size slot (F-021 follow-up) |
| `ToJson` | 18 | JSON serialization: compact/pretty/size-query (3, pre-existing) plus per-type coverage of `oc_rep_to_json_format`'s switch: bool-false, double, text string, base64-encoded byte string, nested object, int/float/double/bool/string arrays (compact + pretty for string array), byte-string array (compact + pretty), object array (compact + pretty) |
| `AddLineToBuffer` | 3 | Buffer append helpers |
| `EncodeRaw` | 1 | Raw CBOR append |
| `FreeRep/Parse` | 2 | Cleanup and empty-map parse |
| `TextKey getters` | 6 | Text-keyed getters (int, bool, double, float, string, byte string NULL checks) |
| `GetString/ByteString` | 2 | NULL size parameter handling |
| `GetCborErrno` | 2 | CBOR errno reporting (clean encode, overflow) |
| `EncodeRawEncoder` | 1 | `oc_rep_encode_raw_encoder()` writes bytes and advances |
| `GetObject/Array` | 7 | Nested object + object-array getters (text/int keys, wrong key, absent mixed array) |

---

### test_coap.cpp — CoAP Parser/Serializer (95 tests)

**Source:** `messaging/coap/coap.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `InitMessage` | 4 | CON/NON/ACK/RST + ClearsFields |
| `Token` | 3 | 4-byte, 8-byte, truncation at 8 |
| `ContentFormat` | 4 | Set/get, not-set default, and the `CONTENT_NONE` marker clearing the option instead of being stored - including a serialize/parse round trip proving it never reaches the wire (F-019) |
| `Accept` | 3 | Set/get, not-set default (caller supplied default survives), `CONTENT_NONE` clears the option (F-019) |
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
| `Parse_Truncated*` | 5 | Malformed option headers rejected with 4.02 before any out-of-bounds read: 1-/2-byte extended delta, 1-/2-byte extended length, truncated option value |
| `Parse_NullData` / `Parse_TruncatedFixedHeader` | 2 | NULL data and `data_len` 0..3 rejected with 4.00 before the fixed CoAP header is read |
| `Parse_TokenAnnouncedBeyondMessage` | 1 | Token length nibble 1..8 announcing more token bytes than the datagram carries is rejected with 4.00; `packet->token` stays wiped (no stale heap bytes copied) |
| `Parse_TokenExactlyFitsMessage` | 1 | Positive boundary: datagram ending exactly after the announced token (`data_len == 4 + token_len`) parses and the token matches the wire |
| `Parse_ShortMessageDoesNotLeakBytesBehindTheDatagram` | 1 | Recycled-buffer regression: bytes past `data_len` pre-filled with a sentinel must never reach `packet->token` |
| `Parse_*Extended*` | 2 | Well-formed extended encodings decode correctly (delta nibble 13 -> Uri-Query, length nibble 13 -> 13-byte Uri-Path) |
| `Connection/QueryOptions` | 12 | MID seeding, query-variable lookup, proxy-uri, uri-query + location-query option set/get |
| `CoapPayloadLimitTest` | 6 | Payload vs `OC_MAX_APP_DATA_SIZE`: accepted below/at max-1 (incl. null-terminator), 4.13 at/above max, no silent truncation, empty payload |
| `CoapFullPduTest` | 1 | Datagram of exactly `OC_PDU_SIZE` with >328 bytes of options: parser null-terminator lands on the extra byte reserved by `oc_allocate_message()` (one-byte heap overflow regression) |

---

### test_coap_oscore.cpp — OSCORE Option (44 tests)

**Source:** `messaging/coap/oscore.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `PivToSsn` | 5 | Big-endian PIV bytes → uint64 SSN (1/3/5 bytes, zero-length, single zero) |
| `SsnToPiv` | 6 | uint64 SSN → PIV bytes (zero, small, 3-byte, max-4, wrap, power-of-32) |
| `PivSsnRoundTrip` | 2 | Conversion integrity (small, large) |
| `OscoreOption` | 6 | Set/parse OSCORE option (PIV-only, PIV+KID, full, inner variants) |
| `OscoreSerialize` | 2 | Serialization to wire format (PIV-only, full round-trip) |
| `OscoreIsOscoreMessage` | 11 | Detects OSCORE option presence (true, non-OSCORE option, no options, OSCORE followed by extended-delta option, OSCORE after a full 8-byte token) and rejects malformed input without reading past the buffer: truncated 1-/2-byte extended delta, truncated extended length, truncated option value, `length` 0..3 (fixed header incomplete), token nibble 1..8 announcing more bytes than the datagram carries |
| `OscoreSendError` | 3 | `oscore_send_error` builds+sends a CoAP error response (network send faked via `--wrap=coap_send_message`): CON request → ACK with cleared OSCORE flag, NON request with zero-length token → fresh-mid NON, secured=true leaves the OSCORE flag untouched |

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

### test_oc_oscore_context.cpp — OSCORE Context (25 tests)

**Source:** `security/oc_oscore_context.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `ContextDeriveParam` | 4 | HKDF key derivation (sender key, IV, with context, uniqueness) |
| `OscoreContextTest` | 16 | Add/find/free contexts, bad params, kid_context, find-by-group-address (sender/recipient), LRU recipient eviction, free-by-AT-id, and the recipient/sender context builders that take their ID from the access token |
| `OscoreContextByToken` | 5 | `oc_oscore_find_context_by_token_mid`: match by token (returns the request PIV), MID fallback, optional out-parameters, unknown transaction → NULL, transaction without an access token → NULL |

---

### test_oc_knx_sec.cpp — Security Credentials (70 tests)

**Source:** `api/oc_knx_sec.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `AtProfileToString` | 5 | Profile enum → string (oscore, dtls, tls, pase, unknown) |
| `OscoreConfig` | 2 | Replay-window size is RFC default; OSN delay round-trips |
| `AuthAtEntry` | 3 | AT-table entry access (out-of-bounds NULL, in-bounds entry, positive table size) |
| `GetAtIndex` | 2 | Entry pointer → slot index (NULL → -1) |
| `AtTable` | 5 | Items-used count, find-by-osc-id, delete entry, delete NULL, find-and-remove PASE token |
| `AtSsnStorage` | 2 | SSN persistence round-trip; entry outside the table is skipped |
| `SecHandlers` | 20 | CoAP handlers of `/auth`, `/auth/o`, `/auth/o/osndelay`, `/auth/o/replwdo`, `/auth/at`, `/auth/at/*`: wrong-Accept rejection, invalid paging (`ps=0`, `pn=-1`), CBOR GET payloads, OSN-delay PUT (no payload → 4.00, valid → 4.04 CHANGED), AT-table DELETE, wildcard entry lookup (missing/unknown token → 4.00) |
| `SecAcl` | 26 | `oc_knx_sec_check_acl`, the authorization gate: NULL resource and unsupported method → deny; per-method handler scope resolution (GET/POST/PUT/DELETE); OSCORE requirement (`OSCORE` alone and `OSCORE_DECRYPTED` alone both denied); scope matching (disjoint → deny, partial overlap → allow, missing access token → deny); `<ga>` per-group-address authorization incl. empty/NULL ga list, absent ga in payload, non-object payload, first/last list entry, broadcast address, and the F-026 regression (a foreign ga anywhere in the payload denies, in either order) |
| `SecAtPost` | 5 | `/auth/at` rejects incomplete or malformed credentials: id + one scope, repeated mandatory property, short/all-zero master secret, and sender ID longer than the OSCORE limit |

---

### test_oc_replay.cpp — Replay Protection (12 tests)

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
| `FullPool_EvictsOldestRecordOnOverflow` | Filling all 20 record slots then adding one more evicts the oldest record (deterministic: first-added client, ties broken by index) |

---


### test_oc_oscore_echo_tx.cpp — OSCORE Engine Security (49 tests)

**Source:** `security/oc_oscore_engine.c` (unified anchor/seen-responder echo ring and inbound OSCORE process path)

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
| `InvalidOuterVersionIsRejectedAndMessageIsReleased` | Version-0 outer CoAP message is rejected and its event reference released |
| `OverlongTokenIsRejectedAndMessageIsReleased` | Outer token length beyond COAP_TOKEN_LEN is rejected before copying |
| `AckWithRequestCodeIsRejectedAndMessageIsReleased` | ACK carrying a request code is not accepted as an OSCORE carrier |
| `NonResetEmptyMessageIsRejected` | Non-RST empty OSCORE message is rejected |
| `ResetWithTokenIsRejected` | RST empty message carrying a token is rejected |
| `UnknownUnicastSenderReceivesUnauthorized` | Unknown unicast OSCORE kid receives an unsecured 4.01 |
| `MissingOscoreOptionRequestReceivesBadOption` | Request without the mandatory OSCORE option receives 4.02 |
| `RequestWithoutKidReceivesBadOption` | OSCORE request without a kid receives 4.02 |
| `RequestWithKidButWithoutPivReceivesBadOption` | OSCORE request with a kid but no PIV receives 4.02 |
| `ResponseWithoutMatchingContextIsIgnored` | Response without a matching OSCORE context is ignored |
| `EchoResponseWithoutAccessTokenIsIgnored` | Echo response without a matching access token is ignored |
| `UnknownMulticastSenderIsSilentlyDropped` | Unknown multicast kid is discarded without a reflection response |
| `AuthenticatedRequestIsDecryptedAndQueuedForRi` | Valid AEAD request is decrypted, marked OSCORE_DECRYPTED and queued for RI |
| `TamperedAuthenticatedRequestReceivesBadRequest` | Modified AEAD tag is rejected with 4.00 |
| `InnerOscoreOptionReceivesSecuredBadOption` | OSCORE option in authenticated inner options receives secured 4.02 |
| `OutboundUnicastRequestIsEncryptedAndSent` | Sender context encrypts a unicast request, advances SSN and dispatches the OSCORE frame |
| `OutboundMulticastRequestIsEncryptedAndSent` | Sender context encrypts a multicast NON request, advances SSN and dispatches the OSCORE frame |
| `OutboundResponseIsEncryptedAndSent` | Recipient context encrypts a normal response without consuming a sender SSN |
| `EmptyDecryptedPayloadReceivesBadRequest` | Authenticated request with an empty inner plaintext receives 4.00 |
| `OutboundAckRequestIsDropped` | ACK carrying a request code is dropped on the outbound path |
| `OutboundMulticastWithoutSModeFlagIsDropped` | Multicast message without the S-mode flag is dropped |
| `OutboundUnicastWithoutContextIsDropped` | Unicast message without an OSCORE context is dropped |
| `OutboundMalformedCoapIsDropped` | Malformed outbound CoAP is dropped |
| `OutboundUnicastEchoResponseIsEncryptedAndSent` | Unicast echo response is encrypted and sent |
| `OutboundMulticastEchoResponseIsEncryptedAndSent` | Multicast-caused echo response is encrypted and sent |

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

### test_oc_endpoint.cpp — Endpoint Handling (38 tests)

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
| `Ipv6AddressScope` | 5 | RFC 6724 scope classification: loopback, link-local, deprecated site-local, ULA (fc00::/7), global |
| `Ipv6EndpointScope` | 3 | Endpoint wrapper: delegates to address scope, NULL endpoint, non-IPv6 endpoint both return 0 |
| `SetLocalAddress` | 1 | NULL endpoint is a no-op (found-match path needs a live port-layer connectivity list) |
| `EndpointPrint` | 1 | Smoke test — does not crash on a valid endpoint |

---

### test_oc_knx_fp.cpp — Group Object & FP Tables (81 tests)

**Source:** `api/oc_knx_fp.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `CflagsAsString` | 7 | Comm-flag bitmask → "rwitu" string |
| `PrintCflags` | 1 | Smoke test — does not crash for any cflag mask |
| `CreateUnicastGroupAddressWithPort` | 1 | Builds a unicast `oc_endpoint_t` from a recipient table entry's stored IPv6 address/port/interface |
| `GoTableSearch` | 17 | GO-table size/bounds, find-first/next by GA (incl. negative-index underflow guard, commit b677b04f), empty-slot search, cflags/ga-len getters |
| `FpTableSizes` | 5 | Recipient/publisher table size + entry bounds, no-IID default |
| `FpFindIdFromPayload` | 5 | id-from-payload lookup (in-range, above-range, negative, no-match, NULL) |
| `GoTableHref` | 7 | href getter + find-first/next by href, sending-GA lookup |
| `RecipientTable` | 1 | Recipient index-from-id match/miss |
| `BelongsHref` | 3 | href ownership check, non-discoverable skipped |
| `FpHandlers` | 27 | CoAP handlers of `/fp/g`, `/fp/p`, `/fp/r` and their `/*` entry resources: wrong-Accept rejection, empty table → 2.05, invalid paging (`ps`/`pn`) → 4.00, populated link-format listing incl. comma separator and next-page indicator, entry GET (unknown id → 4.04, known id → 2.05 CBOR), entry DELETE outside loading state → 4.00 |
| `GroupTableStorage` | 7 | Recipient/publisher entry encode-decode round-trip (NON flag as boolean, empty/multiple GA arrays, absent-NON default) |

---

### test_oc_buffer_settings.cpp — Buffer/PDU Settings (43 tests)

**Source:** `api/oc_main.c` (declared in `include/oc_buffer_settings.h`)

Contract under test (see D-001 in `tests/FINDINGS.md`): the argument of
`oc_set_pdu_size()` is the **total CoAP PDU size**, i.e. payload plus the CoAP
header budget. Since the header split (RFC 8613 clause 4.1.1) that budget is
`OC_COAP_HEADER_BUDGET = COAP_MAX_OUTER_HEADER_SIZE + COAP_MAX_INNER_HEADER_SIZE`,
not a multiple of a single constant. The argument is stored verbatim, the payload
budget used to derive the block size is `pdu_size - OC_COAP_HEADER_BUDGET`, and the
accepted range is `OC_COAP_HEADER_BUDGET + 16 .. COAP_MAX_MESSAGE_SIZE`.

Current values: outer 54, inner 214, plain 268, `KNX_PAYLOAD_SIZE` 768,
default PDU 1036, `COAP_MAX_MESSAGE_SIZE` 1152, default block 512.

**Defaults (`PduDefaults`, 5 tests)**

| Test | What it verifies |
|------|-----------------|
| `DefaultPduReservesTwoCoapHeaders` | Default PDU == `KNX_PAYLOAD_SIZE + OC_COAP_HEADER_BUDGET` |
| `DefaultBlockSizeMatchesPayloadSizeCappedAt1024` | Default block == `EXPECTED_DEFAULT_BLOCK_SIZE` (512 at the current payload) and is a power of two. Name retains the historical 1024 cap; the value is now derived from `KNX_PAYLOAD_SIZE` and the RFC 7252 clause 4.6 ceiling |
| `DefaultBlockSizeIsAnEncodableRfc7959Size` | Default block size is a legal SZX 0..6 value (power of two, 16..1024) |
| `DefaultBlockSizeMatchesWhatSetPduSizeWouldDerive` | Static initializer agrees with the runtime setter |
| `DefaultPduExceedsPayloadSize` | Default PDU strictly larger than the payload budget |

**Accessors (`BufferSettings`, 10 tests)**

| Test | What it verifies |
|------|-----------------|
| `MaxAppDataSizeReturnsKnxPayloadSize` | `oc_get_max_app_data_size()` == `KNX_PAYLOAD_SIZE` |
| `MaxAppDataSizeIsStable` | Repeated calls return the same value |
| `SetPduSizeBelowMinimumReturnsError` | PDU below `OC_COAP_HEADER_BUDGET+16` returns -1 |
| `SetPduSizeBelowMinimumDoesNotChangeState` | PDU below `OC_COAP_HEADER_BUDGET+16` is refused and leaves PDU and block size unchanged |
| `SetPduSizeValidReturnsZero` | Valid PDU accepted |
| `GetPduSizeReturnsExactlyWhatWasSet` | `oc_get_pdu_size()` returns the stored value verbatim |
| `SetPduSizeFromGetPduSizeIsIdempotent` | Feeding the getter back into the setter is a fixed point (5 iterations) |
| `BlockSizeIsPowerOfTwoInRange` | Block size is a power of two in 16..1024 |
| `LargePduYieldsMaxBlockSize` | The largest accepted PDU (`MAXIMUM_PDU_ARG`) yields exactly `ON_WIRE_BLOCK_CEILING` (512 today) with no clamp needed |
| `SmallerPduYieldsSmallerBlockSize` | Smaller PDU → smaller block size |

**Worst-case OSCORE frame (`PduWorstCaseFrame`, 11 tests)**

| Test | What it verifies |
|------|-----------------|
| `MeasuredOuterHeaderFitsOuterBudget` | Serialized max-option outer frame ≤ `COAP_MAX_OUTER_HEADER_SIZE` |
| `MeasuredInnerHeaderFitsInnerBudget` | Serialized max-option inner frame ≤ `COAP_MAX_INNER_HEADER_SIZE` |
| `DefaultPduHoldsWorstCaseOscoreFrame` | Default PDU holds outer + inner headers + a full encrypted payload |
| `PayloadCarryingFrameStaysWithinMeasuredBudget` | A payload-carrying frame stays within the measured budget |
| `SetPduSizeToDefaultEquivalentReproducesDefaultPduSize` | Setting the default-equivalent argument restores the default PDU |
| `SetPduSizeToDefaultEquivalentReproducesDefaultBlockSize` | ...and restores the default block size |
| `PduBelowTwoHeadersPlusMinBlockIsRejected` | Guard rejects anything below `OC_COAP_HEADER_BUDGET + 16` (284 bytes) |
| `MinimumAcceptedPduYieldsMinimumBlockSize` | The smallest accepted PDU yields a 16-byte block |
| `GuardAndBlockBudgetUseSameHeaderMultiple` | Guard and payload-budget subtraction use the same header budget |
| `BlockSizeIsMonotonicInPduSize` | Block size never decreases as the PDU grows |
| `LargePduAlwaysHoldsWorstCaseFrame` | Any accepted large PDU still holds the worst-case frame |

**Header budgets (`CoapHeaderBudgets`, 7 tests)**

| Test | What it verifies |
|------|-----------------|
| `PlainIsTheSumOfOuterAndInner` | `COAP_MAX_HEADER_SIZE` == outer + inner |
| `OuterIsSmallerThanInner` | The Class U budget is the smaller of the two |
| `OuterCoversTheOscoreOption` | The outer budget leaves room for a full OSCORE option value |
| `InnerCoversEchoAndTheUris` | The inner budget covers Echo plus the KNX Uri-Path/Uri-Query maxima |
| `StringOptionGroupChargesOneExtensionPerLongPartOnly` | `COAP_STRING_OPTION_GROUP_SIZE` charges a length extension byte only for parts of 13+ characters, capped by both the character and the part budget |
| `SegmentHeavyUriPathFitsTheTightenedGroupBudget` | A measured count-heavy Uri-Path split (3 long + 3 short parts) stays inside the tightened group budget |
| `PlainFrameFitsThePlainBudget` | A flat inner+outer frame stays within `COAP_MAX_HEADER_SIZE` |

**Message size (`CoapMessageSize`, 3 tests)**

| Test | What it verifies |
|------|-----------------|
| `DefaultBlockKeepsDatagramWithinGuidance` | The default block keeps a full datagram within `COAP_MAX_MESSAGE_SIZE` |
| `DerivedBlockNeverExceedsTheCeiling` | The derived block never exceeds `OC_MAX_ON_WIRE_BLOCK_SIZE` (RFC 7252 clause 4.6) |
| `SetPduSizeRejectsBothOutOfRangeDirections` | Both `MINIMUM-1` and `MAXIMUM+1` → -1, `MAXIMUM` → 0 |

**Serialization modes (`CoapSerializationModes`, 4 tests)**

Pins the three thin wrappers in `messaging/coap/oscore.c` to the mode triple they
must forward. Verified non-vacuous: transposing one argument in
`oscore_serialize_plaintext()` makes `PlaintextWrapperUsesInnerOnlyMode` fail.

| Test | What it verifies |
|------|-----------------|
| `PlaintextWrapperUsesInnerOnlyMode` | `oscore_serialize_plaintext()` == `(inner, !outer, !oscore)` |
| `OscoreWrapperUsesOuterOnlyMode` | `oscore_serialize_message()` == `(!inner, outer, oscore)` |
| `PlainCoapWrapperUsesBothOptionSets` | `coap_serialize_message()` == `(inner, outer, !oscore)` |
| `TheThreeModesProduceDifferentFrames` | Inner and outer frames differ, guarding against a degenerate serializer |

**Message buffer (`PduMessageBuffer`, 2 tests + `PduZzzRestore`, 1 test)**

| Test | What it verifies |
|------|-----------------|
| `AllocatedMessageBufferMatchesConfiguredPduSize` | `oc_allocate_message()` sizes its buffer from `OC_PDU_SIZE` at the maximum accepted PDU |
| `AllocatedMessageBufferHoldsWorstCaseFrame` | The allocated buffer holds the worst-case OSCORE frame |
| `RestoreDefaultEquivalentPduSize` | Restores global PDU state for subsequent suites |

---

### test_oc_core_res.cpp — Core Resources & Device Info (47 tests)

**Source:** `api/oc_core_res.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `DeviceHostname` | 12 | Set stores the label (store first), overwrite; NULL, empty, over-`HNAME_MAX_SIZE` and with-domain (`.`) rejected (previous name kept); load uses the stored name, no or an invalid (old `.local`) stored name gives the default `knx-<serial>` and stores it; `oc_core_reset_device_hostname_to_default` sets and stores the default (returns 0); a failed store returns -1 and RAM still holds the default, at reset and at load without storage |
| `DeviceInfo` | 19 | Firmware/hardware/app version (hwv stored by reference), manufacturer/model/manufacturing-date setters, ia/iid/fid set-and-store range validation |
| `CoreResourceByIndex` | 6 | Resource-by-index bounds (negative, out-of-range, first); every `oc_core_resource_t` value names the resource at that position in the linked list (URI per index, with and without `OC_PUBLISHER_TABLE`), the table covers all `OC_NUM_CORE_RESOURCES`, the list ends after `/.well-known/core` |
| `CheckUrnKnxQuery` | 2 | `urn:knx` query-value prefix match |
| `CheckResourceByRt` | 4 | Resource type match (exact, wildcard, mismatch, no-rt default) |
| `CheckResourceByIf` | 4 | Interface match (exact, wildcard, mismatch, no-if default) |

---

### test_oc_knx_dev.cpp — Device Programming Mode & Resource Handlers (49 tests)

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

**Resource handlers (`KnxDevHandlers`, 43 tests)** — GET/PUT handlers for the
device resources (`/dev/sn`, `/dev/hwv`, `/dev/fwv`, `/dev/hwt`, `/dev/model`,
`/dev/hostname`, `/dev/iid`, `/dev/pm`, `/dev/sa`, `/dev/da`, `/dev/fid`,
`/dev/port`, `/dev/mport`, `/dev/ap/pv`, `/dev/mid`, `/dev/ipv6`, dev/ap lists)
exercised directly at handler level: wrong-Accept → bad request, happy-path
content, empty-payload PUT → bad request, and array-size validation.
`/dev/hname` is read-only (PUT is optional in the specification), so only its
GET handlers are tested plus a check that no PUT handler is registered.

**Coverage note:** `oc_knx_load_device`, `oc_knx_device_storage_reset`, and
`oc_knx_device_restart` still require a live request pipeline (storage + OSCORE
init / DNS-SD re-register) and are covered by the runtime suite
(`test_device_resources.py`, `test_reset.py`, `test_load_state_machine.py`).

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

### test_oc_knx.cpp — KNX Core (28 tests)

**Source:** `api/oc_knx.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `LsmStateString` | 7 | State enum → string (unloaded, loaded, loading, unloading, load-completing, unknown, error) |
| `LsmEventString` | 5 | Event enum → string (nop, start-loading, load-complete, unload, unknown) |
| `IsRedirectedRequestFrom` | 5 | URL routing classification (NULL, empty, /k, /p, other) |
| `KnxLsm` | 1 | `oc_knx_lsm_state()` returns the device LSM field |
| `KnxRuntime` | 3 | Runtime-ready true only when loaded and IID set |
| `KnxHandlers` | 7 | `/.well-known/knx`, `/a/lsm`, `/f` fingerprint GET handlers (JSON/CBOR/accept errors) |

---

### test_oc_knx_fb.cpp — Functional Block Helpers (18 tests)

**Source:** `api/oc_knx_fb.c`

**Functional-block discovery (18 tests)**

| Group | Tests | What it covers |
|-------|-------|----------------|
| `CheckIfFunctionalBlocksNeedToAdd` | 7 | rt/if query + wildcard matching against `urn:knx:fb`/`ll` |
| `CountFunctionalBlocks` | 11 | Distinct FB counting, page-full behavior, `/f` listing, and `/f/{id}` datapoint pagination and unknown-block handling |

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

### test_coap_transactions.cpp — Transaction Lifecycle (20 tests)

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
| `ZeroLengthNullTokenIsSafe` | A zero-length NULL token is accepted without calling `memcpy` with invalid arguments |
| `FreeAllClearsEverything` | Empties entire list |
| `FreeByEndpointSelectiveRemoval` | Only removes matching endpoint |
| `MultipleTransactionsCoexist` | Multiple tracked simultaneously |
| `ZeroLengthTokenMatch` | Both token_len==0 matches correctly |
| `NewTransactionWithDataCopiesPayload` | `coap_new_transaction_with_data()` copies the payload |
| `NewTransactionWithDataRegistersInList` | Data transaction registered in the list |
| `RegisterAsTransactionHandlerIsCallable` | Transaction-handler registration is callable |
| `InitEngineRegistersHandler` | Engine init registers the transaction handler |

---

### test_coap_observe.cpp — Observer Management + Notify (24 tests)

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
| `HandlerRejectsMissingObserveOptionAndBadCodes` | Rejects missing Observe option and unsuccessful GET responses |
| `RemovalByMidNoMatchReturnsZero` | Unmatched MID leaves the observer registered |
| `RegistrationRejectsMissingInvalidAndOverlongLifetime` | Rejects missing, malformed, and overlong `lt` values |
| `RegistrationAcceptsNonTrue` | Accepts `non=true` observe registration |
| `NotifyObserverCanFilterByEndpoint` | Endpoint-filtered notification does not notify another endpoint |
| `NotifyObserverBuildsAndSendsNotification` | Builds and sends a notification for a matching observer |
| `NotifyKObserversNullPayloadIsSafe` | `coap_notify_k_observers(NULL, …)` is a no-op |
| `NotifyKObserversZeroLengthIsSafe` | Zero-length payload is a no-op |
| `NotifyKObserversNoObserversIsSafe` | Empty /k observer list completes safely |
| `NotifyObserversNullResourceReturnsZero` | NULL resource → returns 0 |
| `NotifyObserversNoObserversReturnsZero` | Zero observers short-circuits → 0 |
| `GetObserveCounterIsAtLeastFirstNotificationValue` | Observe counter ≥ first-notification value |
| `GetObserveCounterIsStableWithoutNotifications` | Counter stable when no notifications sent |

---

### test_oc_ri.cpp — Resource Interface (107 tests)

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
| `ScopeMask` | 10 | Interface scope bit positions (if.i, if.a, if.swu, unknown, length mismatch, empty name, non-scope interfaces, internal `<ga>` unreachable by name, all known names round-trip) |
| `RiMask` | 7 | Scope/interface name framing (single/multiple bits, truncated URNs) |
| `PrintAclScopes` | 1 | ACL scope printer runs without crashing |
| `NewRequestFromInbound` | 1 | Inbound request rewires response and copies buffer |
| `QueryParsing` | 1 | NULL and empty query input is rejected before pointer arithmetic |
| `AcceptHeader` | 4 | Accept-header validation (exact, content-none, NULL, mismatch) |
| `QueryValue` | 7 | Query value lookup including key-only and empty-value cases |
| `QueryExists` | 4 | Query key existence including empty-query handling |
| `QueryNthKeyValue` | 4 | Nth query fragment, key-only, missing-fragment, and zero-index cases |
| `QueryNthKeyExists` | 2 | Nth query fragment key including key-only fragments |
| `RiAlloc` | 2 | Allocated resource/resource-data is zeroed |
| `RiAddResource` | 6 | Add-resource validation including mixed input/output interfaces |
| `RiDeleteResource` | 4 | Delete-resource including observer cleanup |
| `RiResourceLookup` | 1 | NULL and empty application-resource paths |
| `RiClientCb` | 9 | Client-callback find-by-mid/token, list traversal, validity, get-by-uri/endpoint/method |
| `RiEntityHandlerBlockwiseLifetime` | 2 | `oc_ri_invoke_coap_entity_handler`'s own internal blockwise scrub must not free the current request's buffer before the caller (`coap_receive`) is done with it |
| `RiEntityHandlerPaths` | 6 | Registered-resource GET, PUT, POST, DELETE, malformed payload, and unsupported-method paths |

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

### test_oc_blockwise.cpp — Blockwise Transfer (26 tests)

**Source:** `api/oc_blockwise.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `BlockwiseDispatch` | 4 | Outgoing block slicing (offset beyond payload, advances offset, partial/mid slices) |
| `BlockwiseHandle` | 5 | Incoming block reassembly (sequential append, offset/size overflow, gap, duplicate) |
| `BlockwiseAlloc` | 11 | Request/response buffer alloc + lookup by href/token/mid/client-cb (both request- and response-list variants of each `_by_token`/`_by_mid`/`_by_client_cb` wrapper), including a NULL empty-query lookup |
| `BlockwiseScrub` | 6 | Free/scrub buffers (incl. NULL-buffer no-op), ref-count-based reclamation, scrub-for-client-cb on both the request and response lists |

---

### test_oc_buffer.cpp — Message Buffer Pool (17 tests)

**Source:** `api/oc_buffer.c`

| Test | What it verifies |
|------|-----------------|
| `AllocateMessageReturnsInitialisedMessage` | Allocated message is initialised |
| `AllocateMessageDataIsWritableForFullPdu` | Data buffer is writable for a full PDU |
| `AllocateMessageReservesTerminatorByteBehindFullPdu` | `data[OC_PDU_SIZE]` is inside the allocation (extra byte for the CoAP null-terminator) |
| `AllocateMessageReturnsDistinctBuffers` | Successive allocations are distinct |
| `AddRefIncrementsCounter` | `oc_message_add_ref()` increments ref count |
| `AddRefNullIsNoOp` | `oc_message_add_ref(NULL)` is a no-op (F-004 RESOLVED) |
| `UnrefDecrementsWithoutFreeingWhenStillReferenced` | Unref decrements without freeing while referenced |
| `UnrefAtZeroFreesMessage` | Unref at zero frees the message |
| `UnrefNullIsNoOp` | `oc_message_unref(NULL)` is a no-op |
| `ReceiveMessageForwardsPlainMessageToCoapEngine` | Inbound plain messages are classified and forwarded to the CoAP engine |
| `ReceiveMessageForwardsOscoreMessageToOscoreLayer` | Inbound OSCORE messages are classified and forwarded to the OSCORE layer |
| `SendMessageRoutesPlainUnicastToNetwork` | Plain unicast messages reach the network transport |
| `SendMessageRoutesDiscoveryToDiscoveryTransport` | Discovery messages use the discovery transport |
| `SendMessageRoutesUnicastOscoreToOscoreLayer` | Unicast OSCORE messages use the unicast OSCORE event |
| `SendMessageRoutesMulticastOscoreToOscoreLayer` | Multicast OSCORE messages use the multicast OSCORE event |
| `EncryptedOscoreMessageIsSentToNetwork` | Encrypted OSCORE messages reach the wrapped network send |
| `EncryptedEventWithoutOscoreFlagIsDropped` | Encrypted events without the OSCORE flag are not sent |

---

### test_oc_client_api.cpp — Client API (19 tests)

**Source:** `api/oc_client_api.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `ClientApiLF` | 12 | Link-format response parsing: entry count, entry URI, rt/if params, missing param, NULL/too-short payload guards, single-record (no trailing comma) fallback |
| `ClientApiInitMsg` | 4 | s-mode / well-known message init (confirmable + non-confirmable) |
| `ClientApiDoUpdate` | 3 | `oc_do_s_mode_message_update` / `oc_do_well_known_message_update` real send path (network faked via `--wrap=coap_send_message`/`oc_main_initialized`/`oc_etimer_restart`, same pattern as `test_smode_send_refcount`): payload present → transaction sent, no payload → early false, well-known GET → message sent |

---

### test_oc_discovery.cpp — Discovery (`/.well-known/core`) (236 tests)

**Source:** `api/oc_discovery.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `DiscoveryBase` (add-payload) | 10 | Link-format framing (URI angle brackets, leading comma, rt truncation/urn:knx strip, content types) and the `include_if` flag in both directions (`if=` framed / omitted, no empty attribute slot left behind) |
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

### test_oc_knx_p.cpp — KNX `/p` Properties Resource (9 tests)

**Source:** `api/oc_knx_p.c`

| Test | What it verifies |
|------|-----------------|
| `KnxPResource.DefinitionUriIsP` | Resource definition URI is `/p` |
| `KnxPGet.WrongAcceptReturnsBadRequest` | GET with wrong Accept → bad request |
| `KnxPGet.NoAppResourcesReturnsBadRequest` | GET with no app resources → bad request |
| `KnxPGet.ContentNoneAcceptIsAccepted` | GET with content-none Accept is accepted |
| `KnxPAppResources.GetReturnsRegisteredDatapointsWithPagination` | GET lists registered datapoints and emits pagination metadata |
| `KnxPAppResources.PostDispatchesValueToMatchingPutHandler` | POST dispatches a collection value to the matching datapoint PUT handler |
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

### test_oc_knx_swu.cpp — Software Update (77 tests)

**Source:** `api/oc_knx_swu.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `KnxSwuGetInt` | 10 | Integer-valued `/swu` resource GETs (protocol, method, result, state, bytes) + wrong-Accept |
| `KnxSwuGetString` | 4 | String-valued GETs (hwref, lastupdate) + wrong-Accept |
| `KnxSwuGetGated` | 10 | Download-gated GETs (pkgv, pkgname, update) returning not-found / bad-request / OK |
| `KnxSwuPut` | 11 | PUT validation per resource (wrong Accept, missing/non-typed payload, happy path, upgrade-cb invocation) |
| `KnxSwuABlock` | 9 | `/swu` block transfer (Accept, ps param, no-callback, fast-path changed) + `po=0` accepted, leading zero is decimal not octal, value is not read beyond its length |
| `KnxSwuABlockInvalidQuery` | 21 | Parameterised `po`/`ps`/`pkgs` rejection: non-numeric, trailing garbage, negative, zero (ps/pkgs), above INT_MAX, too many digits, empty → all 4.00 with the callback never invoked |
| `KnxSwuList` | 3 | List GET wrong-Accept, invalid page size (`ps=0`), invalid page number (`pn=-1`) — all rejected before the core resource table is touched |
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

### test_oc_main.cpp — Main Callbacks / State (9 tests)

**Source:** `api/oc_main.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `MainCallbacks` | 7 | swu / factory-presets / reset / restart / programming-mode / lsm-change setter+getter, overwrite |
| `MainState` | 2 | Not-initialised before bootstrap; signal-event-loop without callbacks is a no-op |

---

### test_oc_network_events.cpp — Network Events (5 tests)

**Source:** `api/oc_network_events.c`

| Test | What it verifies |
|------|-----------------|
| `EventWhenProcessNotRunningUnrefsMessage` | Event with process not running unrefs the message |
| `EventWhenProcessNotRunningIsRepeatable` | Repeatable without leaking |
| `RunningProcessDrainsQueuedMessage` | Running process drains and receives queued messages |
| `InterfaceEventsArePostedAndDispatched` | Interface up/down events are posted and dispatched |
| `UnknownInterfaceEventIsIgnored` | Unknown interface event is ignored |

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

### test_oc_process.cpp — Process / Event Loop (16 tests)

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
| `ExitOnNeverStartedProcessIsNoOp` | Exiting a process never added to the list is a no-op (`exit_process`'s not-found branch) |
| `ExitNotifiesSiblingAndUnlinksFromListMiddle` | With two processes running, exiting the list head notifies the surviving sibling with `OC_PROCESS_EVENT_EXITED` and unlinks correctly |
| `PostBeyondCapacityGrowsQueueAndPreservesOrder` | Posting well past the initial ring capacity forces the realloc + wraparound-copy growth path in `oc_process_post`, preserving FIFO order |

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

### test_oc_server_api.cpp — Server API (41 tests)

**Source:** `api/oc_server_api.c`

| Group | Tests | What it covers |
|-------|-------|----------------|
| `ServerApiQuery` | 9 | Query value/exists lookups, empty and non-empty iteration, NULL request |
| `ServerApiResponse` | 9 | Response preparation (CBOR/JSON/link-format/no-format), ignore-request, missing-buffer guards |
| `ServerApiResource` | 23 | Resource allocation failure, separate setup, delayed callbacks, bind/handler/interface/ACL/property and const/null guards |

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

### test_separate.cpp — Separate Responses (7 tests)

**Source:** `messaging/coap/separate.c`

| Test | What it verifies |
|------|-----------------|
| `AcceptNonRequestCreatesStore` | Accepting a non-request creates a store |
| `AcceptSameTokenAndObserveReusesStore` | Same token + observe reuses the store |
| `AcceptDifferentObserveCreatesSecondStore` | Different observe creates a second store |
| `AcceptConRequestSendsEmptyAckAndCreatesStore` | CON request sends the empty ACK (via `--wrap=coap_send_message`) and creates the store |
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

### test_dns_sd_mdns.cpp — DNS-SD Announcements and Query Responder (36 tests)

**Source:** `port/dns-sd_mdns.c`

Linker `--wrap` of `sendto`, `recvmsg`, `get_ip_context_for_device`,
`oc_clock_time` and `oc_main_initialized`: no packet leaves the host, the
listener thread receives only the queries the test queues, one fake loopback
endpoint stands in for the connectivity layer, a test-controlled clock drives
the RI timed-event list through `oc_main_poll()`, and a test flag stands in for
the stack initialization (no `oc_main_init()`, the service update announces
nothing while it is false). The fixture sets the device hostname while the flag
is false, then runs initialized. The target is registered under `if(UNIX)`,
since the source is POSIX-only and `--wrap` is a GNU ld feature.

| Group | Tests | What it covers |
|-------|-------|----------------|
| `knx_dns_sd_update_service` | 10 | First announcement, RFC 6762 section 8.3 repeat at 1 s and only once, section 8.4 restart on a change, goodbye for the old record, NULL serial, rollback on send failure, no interface, nothing before the stack is initialized |
| host name (`oc_core_set_device_hostname`) | 3 | No packet for a change before the stack is initialized; the first announcement after it carries the latest host name set before (no goodbye); a change afterwards sends the goodbye with the old name, then announces the new one |
| programming mode (`oc_knx_device_set_programming_mode`) | 3 | No packet for a change before the stack is initialized; the first announcement after it carries the `_pm` subtype set before (no goodbye); on and off afterwards each send the goodbye for the old record, then announce the new one with or without `_pm` |
| `knx_dns_sd_set_sleep_period` | 5 | SP in TXT, repeat after the change, TXT goodbye on clear, no goodbye for an unset SP, nothing before a publish |
| `knx_dns_sd_stop` | 2 | Goodbye and cancel of the pending repeat; nothing without a publish |
| getters | 2 | `knx_dns_sd_get_used_port`, `knx_dns_sd_get_current_record` before, after and past a publish |
| query responder | 11 | PTR for service type, serial, IA and `_pm` subtype (programming mode only), ANY, SRV, AAAA, TXT, foreign names, unhandled type, application-set host name in SRV target and AAAA |

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
19 `test_*.py` files in `tests/runtime/`, 245 tests collected
(with parametrize), plus the generated eitt tier and the DUT-less harness tier.
The per-section headers below count unique test functions; the extra collected
items come from parametrized cases in `test_device_resources.py` and
`test_swu.py`.

### Runtime tier cleanup (2026-09-11)

The rows below marked *(replaced by the EITT template case — eitt tier)* are
tests that were **deleted** from the runtime tier. Their requirement is
exercised by a `TestCase` in
`tests/eitt/data/KnxConformanceTestTemplate-PointAPI.xml`, which the eitt
tier runs directly, so keeping a hand-written copy meant maintaining the same
assertion twice. 135 test functions (143 collected items) went; the per-test
mapping to EITT case ids is in the commit "runtime tier: drop template-covered
tests, mark overlaps", and the classification behind it is in
`tests/docs/runtime-test-inventory.md`.

The section headers in Part 2 still give the **pre-cleanup** counts, so that
this catalog keeps documenting what each EITT case is checked by, wherever it
now runs. For the live count of what the runtime tier collects today, run
`pytest tests/runtime --collect-only -q`.

Tests that survived but overlap a template case — because they assert
materially more than it does — carry `@pytest.mark.eitt_overlap(<id>)`; list
them with `pytest tests/runtime -m eitt_overlap --collect-only -q`.

**DUT config:** Serial=`00fa10020800`, Password=`2X4W3TE0DFLLS19Y1FCH`, MID=667, IA=`0x1101`, IID=`0x1199887766`

---

### Expected behaviour: block-wise responses (RFC 7959)

Any response body larger than the DUT's configured application buffer
(`KNX_PAYLOAD_SIZE`, currently 768, which floors to a 512-byte block) is delivered
block-wise. This is normal, conformant behaviour and **not** truncation:

- The DUT sets **Block2 (option 23)** with the `M` ("more") bit on every block but the
  last, and **Size2 (option 28)** on the first block giving the total body length.
- Because a block ends at a fixed byte offset, the payload of a non-final block
  routinely ends mid-token, e.g. `...;rt="urn:knx:dpa.65500.201";i`. That is a block
  boundary, not a half-written buffer.
- Block2 is a **Class E** option under OSCORE, so on protected exchanges it is carried
  inside the encrypted inner message, never as an outer option.

`coap_client.py` handles this for the test author:

| Helper | Behaviour |
|---|---|
| `get()` | Reassembles Block2 into one complete body before returning |
| `oscore_get()` | Same, with Block2 encoded as an inner Class E option |
| `request()` / `oscore_request()` | **One request, one block.** Use these only when a test deliberately wants a single block; pass `block2=` to select which |
| `multicast_get()` | Does **not** reassemble - see below |

Multicast is deliberately excluded: RFC 7959 section 2.8 requires the remaining blocks
to be fetched by **unicast**, so a multicast responder legitimately returns only the
first block. Tests asserting over a multicast link-format body must therefore check the
Block2 `M` bit and discard the trailing partial entry, as
`test_5_1_1_3e_if_wildcard` does. Its unicast twin, `test_5_1_1_4e_if_wildcard`,
validates the complete reassembled body.

Measured link-format sizes at `KNX_PAYLOAD_SIZE` 768 (512-byte blocks):

| Request | Blocks | Full body |
|---|---|---|
| `GET /.well-known/core` | 1 | 396 bytes (no Block2) |
| `GET /.well-known/core?if=*` | 2 | 688 bytes |
| `GET /.well-known/core?rt=*` | 2 | 688 bytes |
| `GET /dev` (OSCORE) | 2 | 553 bytes |

Historical note: these four bodies crossing the block boundary is what surfaced the
client's missing Block2 support. See F-017 in `FINDINGS.md` (withdrawn - the stack was
correct, the client was not).

---

### test_discovery_pagination.py — Unicast Discovery (10 tests, 19 collected)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestUnicastDiscovery** | `test_5_1_1_2_unicast_discovery` | 5.1.1.2 *(replaced by the EITT template case — eitt tier)* |
| **TestUnicastDiscoveryFilters** | `test_5_1_1_4_step1_rt_known_value` | 5.1.1.4 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_1_1_4_step2_rt_wildcard` | 5.1.1.4 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_1_1_4_step3_if_known_value` | 5.1.1.4 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_1_1_4_step4_ep_serial_number` | 5.1.1.4 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_1_1_4_step5_ep_sn_wildcard` | 5.1.1.4 *(replaced by the EITT template case — eitt tier)* |
| **TestInvalidUnicastQueries** | `test_5_1_1_4b_invalid_query_key` | 5.1.1.4b *(replaced by the EITT template case — eitt tier)* |
| | `test_5_1_1_4c_invalid_interface` | 5.1.1.4c *(replaced by the EITT template case — eitt tier)* |
| | `test_5_1_1_4d_invalid_resource_type` | 5.1.1.4d *(replaced by the EITT template case — eitt tier)* |
| | `test_5_1_1_4e_if_wildcard` | 5.1.1.4e *(replaced by the EITT template case — eitt tier)* |

---

### test_discovery_multicast.py — Multicast Discovery (19 tests)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestMulticastDiscovery** | `test_5_1_1_1_site_local_discovery` | 5.1.1.1 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_1_1_1_second_request` | 5.1.1.1 *(replaced by the EITT template case — eitt tier)* |
| **TestMulticastDiscoveryFilters** | `test_5_1_1_3_step1_rt_known_value` | 5.1.1.3 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_1_1_3_step2_rt_wildcard` | 5.1.1.3 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_1_1_3_step3_if_known_value` | 5.1.1.3 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_1_1_3_step4_ep_serial_number` | 5.1.1.3 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_1_1_3_step5_ep_sn_wildcard` | 5.1.1.3 *(replaced by the EITT template case — eitt tier)* |
| **TestInvalidMulticastQueries** | `test_5_1_1_3b_invalid_query_key` | 5.1.1.3b *(replaced by the EITT template case — eitt tier)* |
| | `test_5_1_1_3c_invalid_interface` | 5.1.1.3c *(replaced by the EITT template case — eitt tier)* |
| | `test_5_1_1_3d_invalid_resource_type` | 5.1.1.3d *(replaced by the EITT template case — eitt tier)* |
| **TestMulticastInterfaceWildcard** | `test_5_1_1_3e_if_wildcard` | 5.1.1.3e *(replaced by the EITT template case — eitt tier)* |
| **TestMulticastProgrammingModeDiscovery** | `test_5_1_1_5_pm_on_returns_response` | 5.1.1.5 *(replaced by the EITT template case — eitt tier)* |
| | `test_multicast_pm_query_silent_when_pm_off` | 5.1.1.5b |
| **TestMulticastPMEndpointDiscovery** | `test_5_1_1_6_pm_ep_correct_sn` | 5.1.1.6 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_1_1_6b_pm_off_no_response` | 5.1.1.6b *(replaced by the EITT template case — eitt tier)* |
| | `test_5_1_1_6c_pm_on_wrong_sn` | 5.1.1.6c *(replaced by the EITT template case — eitt tier)* |
| **TestMulticastIADiscovery** | `test_5_1_1_9a_correct_iid_ia` | 5.1.1.9a *(replaced by the EITT template case — eitt tier)* |
| | `test_5_1_1_9b_wrong_iid` | 5.1.1.9b *(replaced by the EITT template case — eitt tier)* |
| | `test_multicast_endpoint_query_silent_for_wrong_ia` | 5.1.1.9c |

---

### test_discovery_programming_mode.py — Extended Discovery (14 tests)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestProgrammingModeDiscovery** | `test_5_1_1_5c_pm_unicast_device_in_pm` | 5.1.1.5c *(replaced by the EITT template case — eitt tier)* |
| | `test_pm_unicast_device_not_in_pm_returns_404` | 5.1.1.5d |
| | `test_5_1_1_6d_pm_ep_correct_sn` | 5.1.1.6d *(replaced by the EITT template case — eitt tier)* |
| | `test_pm_with_endpoint_device_not_in_pm_returns_404` | 5.1.1.6e |
| | `test_pm_with_endpoint_wrong_serial_returns_404` | 5.1.1.6f |
| **TestGroupAddressDiscovery** | `test_5_1_1_7_multicast_ga_single_go` | 5.1.1.7 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_1_1_7b_multicast_ga_multiple_go` | 5.1.1.7b *(replaced by the EITT template case — eitt tier)* |
| | `test_5_1_1_7c_multicast_ga_no_match` | 5.1.1.7c *(replaced by the EITT template case — eitt tier)* |
| | `test_5_1_1_7d_multicast_multi_ga_per_go` | 5.1.1.7d *(replaced by the EITT template case — eitt tier)* |
| | `test_5_1_1_7e_unicast_matching_ga` | 5.1.1.7e *(replaced by the EITT template case — eitt tier)* |
| | `test_5_1_1_7f_unicast_multiple_go` | 5.1.1.7f *(replaced by the EITT template case — eitt tier)* |
| | `test_5_1_1_7g_unicast_no_matching_ga` | 5.1.1.7g *(replaced by the EITT template case — eitt tier)* |
| | `test_5_1_1_7h_unicast_multi_ga_per_go` | 5.1.1.7h *(replaced by the EITT template case — eitt tier)* |
| **TestGAWildcard** | `test_5_1_1_8_ga_wildcard_rejected` | 5.1.1.8 *(replaced by the EITT template case — eitt tier)* |

---

### test_discovery_mdns.py — mDNS Discovery (16 EITT tests, 32 collected)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestMdnsDiscoverySerialUnconfigured** | `test_browse_knx_service_by_serial` | 5.1.2.1 |
| | `test_5_1_2_1_browse_serial_subtype` | 5.1.2.1 *(replaced by the EITT template case — eitt tier)* |
| | `test_srv_resolution_of_unconfigured_device` | 5.1.2.1 |
| | `test_aaaa_resolution_of_unconfigured_device` | 5.1.2.1 |
| | `test_coap_reachability_of_unconfigured_device` | 5.1.2.1 |
| **TestMdnsDiscoverySerialConfigured** | `test_5_1_2_2_serial_subtype_configured` | 5.1.2.2 *(replaced by the EITT template case — eitt tier)* |
| | `test_full_chain_by_serial_when_configured` | 5.1.2.2 |
| **TestMdnsDiscoveryIA** | `test_5_1_2_3_ia_subtype_discovery` | 5.1.2.3 *(replaced by the EITT template case — eitt tier)* |
| | `test_full_chain_by_individual_address` | 5.1.2.3 |
| **TestMdnsDiscoveryIAZero** | `test_5_1_2_3a_ia_zero_subtype` | 5.1.2.3a *(replaced by the EITT template case — eitt tier)* |
| **TestMdnsDiscoveryIAUnconfigured** | `test_unconfigured_device_advertises_default_ia_subtype` | 5.1.2.3b |
| **TestMdnsDiscoveryPM** | `test_5_1_2_4_pm_subtype_enabled` | 5.1.2.4 *(replaced by the EITT template case — eitt tier)* |
| | `test_full_chain_via_programming_mode_subtype` | 5.1.2.4 |
| | `test_programming_mode_subtype_withdrawn_when_disabled` | 5.1.2.4 |
| **TestMdnsUnsolicitedPM** | `test_programming_mode_toggle_announcements` | 5.1.2.5a |
| **TestMdnsUnsolicitedIA** | `test_individual_address_change_announcement` | 5.1.2.5b |
| **TestMdnsStateSync** | `test_state_sync_mdns_alive_in_unloaded` | - (non-EITT) |

> The table above lists the EITT-numbered tests. The module collects 32 tests in
> total; the remainder are the non-EITT `5_1_901_x` (transition audit) and
> `5_1_902_x` (state sync) additions.
>
> `test_state_sync_mdns_alive_in_unloaded` guards the deliberate
> absence of a `knx_dns_sd_stop()` call when the LSM enters UNLOADED
> (`api/oc_knx.c`): the unload clears the group tables but leaves sn/ia/iid
> intact, so the device must remain discoverable. The test asserts that no mDNS
> goodbye is emitted, that the advertised subtypes/address/port are unchanged,
> and that the advertised endpoint still answers CoAP.

---

### test_ia_assignment.py — API Version & IA (5 tests)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestApiVersion** | `test_5_2_1_1_api_version` | 5.2.1.1 *(replaced by the EITT template case — eitt tier)* |
| **TestRestart** | `test_5_2_2_3_restart_pm_resets` | 5.2.2.3 *(replaced by the EITT template case — eitt tier)* |
| **TestIaAssignment** | `test_set_ia_and_iid` | 5.2.3.1 |
| | `test_get_ia_rejected` | 5.2.3.1b |
| | `test_set_ia_iid_and_fabric_id` | 5.2.3.2 |

---

### test_reset.py — Reset Commands (2 tests)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestFactoryReset** | `test_factory_reset_clears_all_state` | 5.2.2.1 |
| **TestResetCode7** | `test_reset_code_7_preserves_network_info` | 5.2.2.2 |

---

### test_fingerprint.py — Fingerprint Resource (5 tests)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestFingerprintChanges** | `test_fingerprint_changes_after_each_modification` | 5.2.4.1 |
| **TestFingerprintInvalidWrite** | `test_post_fingerprint_rejected` | 5.2.4.1b |
| | `test_put_fingerprint_rejected` | 5.2.4.1c |
| **TestFingerprintLoadingState** | `test_5_2_4_2_fingerprint_loading` | 5.2.4.2 *(replaced by the EITT template case — eitt tier)* |
| **TestFingerprintUnloadedState** | `test_5_2_4_3_fingerprint_unloaded` | 5.2.4.3 *(replaced by the EITT template case — eitt tier)* |

---

### test_device_resources.py — Device Resources (16 tests, 24 collected)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestDevResourceList** | `test_dev_link_format_lists_sub_resources` | 5.2.5.1 |
| **TestDevResourceRead** | `test_read_serial_number` | 5.2.6.1 |
| | `test_read_firmware_version` | 5.2.6.1 |
| | `test_5_2_6_1_hardware_type` | 5.2.6.1 *(replaced by the EITT template case — eitt tier)* |
| | `test_read_model` | 5.2.6.1 |
| | `test_read_subnet_address` | 5.2.6.1 |
| | `test_read_device_address` | 5.2.6.1 |
| | `test_5_2_6_1_iid` | 5.2.6.1 *(replaced by the EITT template case — eitt tier)* |
| | `test_read_programming_mode` | 5.2.6.1 |
| | `test_read_manufacturer_id` | 5.2.6.1 |
| | `test_read_coap_port` | 5.2.6.1 |
| | `test_read_ipv6_address` | 5.2.6.1 |
| | `test_read_hostname` | 5.2.6.1 |
| | `test_5_2_6_1_fid` | 5.2.6.1 *(replaced by the EITT template case — eitt tier)* |
| **TestDevResourceWrite** | `test_5_2_7_1_write_pm` | 5.2.7.1 *(replaced by the EITT template case — eitt tier)* |
| **TestDevReadOnlyPutRejected** | `test_5_2_7_1b_put_readonly_rejected` ×9 | 5.2.7.1b *(replaced by the EITT template case — eitt tier)* |

---

### test_swu.py — Software Update (8 tests, 29 collected)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestSwuResourceList** | `test_5_2_8_1_swu_link_format` | 5.2.8.1 *(replaced by the EITT template case — eitt tier)* |
| **TestSwuFirmwareUpdate** | `test_push_based_firmware_update` | 5.2.9.1 |
| **TestSwuLastUpdateLifecycle** | `test_lastupdate_is_mfg_date_before_any_update` | - |
| **TestSwuLastUpdateLifecycle** | `test_lastupdate_changes_after_swu_update` | - |
| **TestSwuLastUpdateLifecycle** | `test_lastupdate_survives_reboot` | - |
| **TestSwuReadOnlyPutRejected** | `test_put_readonly_swu_rejected` ×5 | 5.2.10.1b |
| **TestSwuBlockQueryValidation** | `test_invalid_block_query_returns_400` ×18 | - |
| **TestSwuBlockQueryValidation** | `test_leading_zero_is_accepted` | - |

---

### test_security.py — Security & OSCORE (76 tests)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestSpake2PlusProtocol** | `test_spake_full_handshake` | 5.3.1.1 |
| | `test_spake_wrong_password_fails_verification` | 5.3.1.2 |
| | `test_spake_wrong_confirm_p_rejected` | 5.3.1.2b |
| **TestSpake2PlusResetTypes** | `test_spake_across_reset_types` | 5.3.1.3 |
| **TestSecondarySpake** | `test_secured_spake_post_rejected` | 5.3.1.4a |
| **TestSpake2PlusStateMachine** | `test_spake_state_credential_before_parameter` | 5.3.1.5a |
| | `test_spake_state_verification_before_parameter` | 5.3.1.5b |
| | `test_spake_state_repeated_parameter_mid_handshake` | 5.3.1.5c |
| | `test_spake_state_parameter_missing_id` | 5.3.1.5d |
| | `test_spake_state_parameter_missing_rnd` | 5.3.1.5e |
| | `test_spake_state_ordered_handshake_completes` | 5.3.1.5f |
| | `test_spake_state_parameter_restart_then_completes` | 5.3.1.5g |
| | `test_spake_state_parameter_after_credential_restarts` | 5.3.1.5h |
| | `test_spake_state_repeated_credential_request` | 5.3.1.5i |
| | `test_spake_state_verification_after_parameter` | 5.3.1.5j |
| | `test_spake_state_restart_from_credentials_completes` | 5.3.1.5k |
| **TestSpake2PlusBruteForce** | `test_spake_lockout_block_after_ten_failures` | 5.3.1.6a |
| | `test_spake_lockout_factory_reset_clears_block` | 5.3.1.6b |
| | `test_spake_lockout_counter_decreases_over_time` | 5.3.1.6c |
| | `test_spake_lockout_unblocks_after_timeout` | 5.3.1.6d |
| | `test_spake_lockout_partial_step_does_not_reset_counter` | 5.3.1.6e |
| | `test_spake_lockout_full_handshake_resets_counter` | 5.3.1.6f |
| **TestAuthResourceList** | `test_get_auth_without_oscore_returns_403` | 5.3.4.1 |
| | `test_5_3_4_1_get_auth_link_format` | 5.3.4.1 *(replaced by the EITT template case — eitt tier)* |
| **TestWriteAccessToken** | `test_write_at_and_verify` | 5.3.8.1 |
| | `test_write_at_with_group_scope` | 5.3.8.1 |
| | `test_5_3_8_1b_post_auth_at_without_oscore_fails` | 5.3.8.1b *(replaced by the EITT template case — eitt tier)* |
| | `test_5_3_8_1c_put_auth_at_without_oscore_fails` | 5.3.8.1c *(replaced by the EITT template case — eitt tier)* |
| | `test_post_at_without_if_sec_scope_fails` | 5.3.8.1d |
| | `test_5_3_8_1e_put_at_without_if_sec_scope_fails` | 5.3.8.1e *(replaced by the EITT template case — eitt tier)* |
| | `test_5_3_8_1f_put_at_with_if_sec_scope_still_fails` | 5.3.8.1f *(replaced by the EITT template case — eitt tier)* |
| | `test_5_3_8_2_mixed_scope_rejected` | 5.3.8.2 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_3_8_3_multi_group_scope` | 5.3.8.3 *(replaced by the EITT template case — eitt tier)* |
| | `test_overwrite_existing_at` | 5.3.8.4 |
| | `test_create_multiple_ats_single_write` | 5.3.8.5 |
| | `test_update_multiple_ats` | 5.3.8.6 |
| **TestReadAccessTokenList** | `test_get_auth_at_link_format` | 5.3.9.1 |
| | `test_get_auth_at_without_oscore_returns_403` | 5.3.9.3a |
| | `test_5_3_9_3b_get_auth_at_without_oscore_configured_device` | 5.3.9.3b *(replaced by the EITT template case — eitt tier)* |
| **TestReadAccessTokenById** | `test_get_at_by_id` | 5.3.10.1 |
| | `test_get_nonexistent_at_returns_error` | 5.3.10.1 |
| | `test_get_at_by_id_without_oscore_returns_403` | 5.3.10.2a |
| | `test_5_3_10_2b_get_at_with_wrong_scope_returns_403` | 5.3.10.2b *(replaced by the EITT template case — eitt tier)* |
| **TestDeleteAccessToken** | `test_delete_at` | 5.3.11.1 |
| | `test_delete_nonexistent_at` | 5.3.11.1 |
| **TestOscoreResourceList** | `test_get_auth_o_without_oscore_returns_403` | 5.3.12.1 |
| | `test_5_3_12_1_get_auth_o_link_format` | 5.3.12.1 *(replaced by the EITT template case — eitt tier)* |
| **TestReplayWindowSize** | `test_get_replwdo_without_oscore_returns_403` | 5.3.13.1 |
| | `test_get_replwdo` | 5.3.13.1 |
| **TestOscoreDelay** | `test_get_osndelay_without_oscore_returns_403` | 5.3.15.1 |
| | `test_get_osndelay` | 5.3.15.1 |
| | `test_put_osndelay` | 5.3.16.1 |
| **TestAntiReplayWindow** | `test_old_ssn_within_window_accepted` | 5.3.17.1 |
| **TestGroupAntiReplayWindow** | `test_group_anti_replay_window` | 5.3.17.3 |
| **TestSyncDelayEcho** | `test_sync_delay_echo` | 5.3.17.4 |
| **TestInvalidEchoReply** | `test_invalid_echo_reply` | 5.3.17.5 |
| **TestGroupOscoreMulticast** | `test_receive_device_multicast` | 5.3.17.6 |
| **TestEchoChallenge** | `test_echo_on_new_context` | 5.3.17.7 |
| **TestEchoPaseToken** | `test_echo_on_pase_at_post` | 5.3.17.8 |
| **TestMessageIntegrity** | `test_corrupted_auth_tag_rejected` | 5.3.18.1 |
| | `test_different_auth_tag_rejected` | 5.3.18.1 |
| **TestEmptyMessageIsNotOscore** | `test_empty_message_with_oscore_option_rejected` (3 params: CON/NON/ACK) | 5.3.18.2 |
| | `test_empty_reset_does_not_break_oscore` | 5.3.18.2 |
| | `test_empty_reset_with_token_rejected` | 5.3.18.2 |
| **TestOscoreEncryption32ByteMs** | `test_oscore_with_32_byte_master_secret` | 5.3.19.1 |
| | `test_5_3_19_1_oscore_with_16_byte_master_secret` | 5.3.19.1 *(replaced by the EITT template case — eitt tier)* |
| **TestOscoreMasterSalt** | `test_5_3_19_2_oscore_with_master_salt` | 5.3.19.2 *(replaced by the EITT template case — eitt tier)* |
| **TestGroupOscoreMasterSaltCtxId** | `test_group_oscore_with_salt_and_ctx_id` | 5.3.19.5 |
| **TestOscoreContextId** | `test_context_ids_with_master_salt` | 5.3.19.6 |
| **TestAccessControlByScope** | `test_5_3_20_1_limited_scope_can_access_matching_resource` | 5.3.20.1 *(replaced by the EITT template case — eitt tier)* |
| | `test_limited_scope_rejected_for_post_auth_at` | 5.3.20.1 |
| | `test_limited_scope_rejected_for_dev_pm` | 5.3.20.1 |
| | `test_if_p_scope_can_access_dev_pm` | 5.3.20.1 |
| | `test_if_d_scope_can_read_device_info` | 5.3.20.1 |

---

### test_group_comm.py — Group Communication (34 tests)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestUnicastWrite** | `test_5_4_1_1_unicast_write_updates_value` | 5.4.1.1 *(replaced by the EITT template case — eitt tier)* |
| **TestUnicastWriteUnauthorized** | `test_unauthorized_ga_returns_forbidden` | 5.4.1.1b |
| **TestUnicastWriteDuplicateGroupAddress** | `test_control_authorized_ga_writes_only_its_own_datapoint` | F-026 (non-EITT) |
| | `test_duplicate_ga_does_not_bypass_scope` | F-026 (non-EITT) |
| **TestForeignGaBindingIsLive** | `test_authorized_write_to_ga_one_reaches_p4` | F-026 control (non-EITT) |
| **TestMulticastWrite** | `test_5_4_1_2_multicast_write_updates_value` | 5.4.1.2 *(replaced by the EITT template case — eitt tier)* |
| **TestMulticastRead** | `test_multicast_read_gets_answer` | 5.4.1.3 |
| **TestUnicastRead** | `test_unicast_read_gets_answer` | 5.4.1.4 |
| **TestUnicastWriteMultipleGO** | `test_5_4_1_5_write_updates_both_datapoints` | 5.4.1.5 *(replaced by the EITT template case — eitt tier)* |
| **TestTriggerMulticastWrite** | `test_trigger_multicast_write` | 5.4.1.6 |
| **TestTriggerFirstGA** | `test_5_4_1_7_sends_first_ga` | 5.4.1.7 *(replaced by the EITT template case — eitt tier)* |
| **TestNotSendingInLoadingState** | `test_5_4_1_8_no_send_in_loading` | 5.4.1.8 *(replaced by the EITT template case — eitt tier)* |
| **TestInitFlagStartup** | `test_init_flag_sends_read_on_restart` | 5.4.1.9 |
| **TestReadOnInitNetworkGate** | `test_read_on_init_up_from_start_sends_read` | 5.4.1.9b (non-EITT) |
| | `test_read_on_init_withheld_while_down` | 5.4.1.9b (non-EITT) |
| | `test_read_on_init_released_after_network_up` | 5.4.1.9b (non-EITT) |
| | `test_read_on_init_abandoned_after_max_retries` | 5.4.1.9b (non-EITT) |
| **TestMulticastResponseUpdate** | `test_5_4_1_10_multicast_answer_updates_value` | 5.4.1.10 *(replaced by the EITT template case — eitt tier)* |
| **TestMulticastResponseUpdateOnly** | `test_answer_updates_update_only_go` | 5.4.1.10b (non-EITT) |
| **TestMulticastWriteIgnoredLoading** | `test_5_4_1_11_write_ignored_in_loading` | 5.4.1.11 *(replaced by the EITT template case — eitt tier)* |
| **TestOwnGroupObjectUpdate** | `test_self_update_on_transmit` | 5.4.1.12 |
| **TestLongGAReceive** | `test_5_4_1_13_long_ga_write_ga0` | 5.4.1.13 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_4_1_13_long_ga_write_ga256` | 5.4.1.13 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_4_1_13_long_ga_write_ga65535` | 5.4.1.13 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_4_1_13_long_ga_write_ga_max32` | 5.4.1.13 *(replaced by the EITT template case — eitt tier)* |
| **TestLongGASend** | `test_sends_long_ga` | 5.4.1.14 |
| **TestUnicastConfirmable** | `test_unicast_sends_con` | 5.4.1.15 |
| **TestUnicastDiscoveryMalformedLengths** | `test_unicast_discovery_iid_too_long` | non-EITT |
| | `test_unicast_discovery_iid_too_short` | non-EITT |
| | `test_unicast_discovery_ia_too_long` | non-EITT |
| | `test_unicast_discovery_ia_too_short` | non-EITT |
| | `test_unicast_discovery_sn_too_long` | non-EITT |
| | `test_unicast_discovery_sn_too_short` | non-EITT |
| **TestUnicastNonReResolution** | `test_unicast_non_re_resolution` | non-EITT |

---

### test_observe.py — CoAP Observe / Notifications (19 tests)

> **Spec-backed approximation, NOT an EITT replication.** There is no EITT
> (08_10_5) observe section and `EittProject.xml` has no observe telegrams;
> these tests are anchored to spec 2.5.9.x / 2.6.10.1 and RFC 7641/8613, and
> the `5.4.2.x+` numbering / `OBS-x` IDs are placeholders pending an official
> EITT observe section. See the file header for the documented /k vs /p
> behaviour.

| Group | Test | Spec/ref |
|-------|------|----------|
| **A — Subscription lifecycle** | `test_observe_register_returns_current_value` | 2.6.10.1 |
| | `test_observe_value_change_notifies` | 2.6.10.1 |
| | `test_observe_deregister_stops_notifications` | 2.6.10.1 |
| | `test_observe_delete_sub_removes_subscription` | 2.6.10.1 |
| | `test_observe_resubscribe_replaces_by_ip_port` | 2.6.10.1 |
| **B — lt validation** | `test_observe_missing_lt_rejected` | 2.5.9.3 / 2.5.11.6 |
| | `test_observe_lt_zero_rejected` | 2.5.9.3 / 2.5.11.6 |
| | `test_observe_expired_lt_stops_notifications` | 2.5.9.3 / 2.5.11.6 |
| **C — CON/NON** | `test_observe_default_confirmable` | 2.5.9.4 |
| | `test_observe_non_true_non_confirmable` | 2.5.9.4 |
| **D — /k S-Mode** | `test_observe_first_k_notification_sia_only` | 2.5.9.1 |
| | `test_observe_subsequent_k_notification_has_s_object` | 2.5.9.1 |
| | `test_observe_k_deregister_stops_notifications` | 2.5.9.1 / 2.6.10.1 |
| | `test_observe_inbound_post_k_not_echoed` | 2.5.9.1 |
| | `test_observe_plain_get_k_no_subsequent` | 2.5.9.1 |
| **E — OSCORE notifications** | `test_observe_encrypted_notifications_decrypt` | RFC 8613 |
| | `test_observe_notification_aad_binds_request_piv` | RFC 8613 §8.3 |
| **F — Multi-observer** | `test_observe_independent_seq_per_observer` | 2.6.10.1 |
| | `test_observe_stale_observer_pruned` | RFC 7641 |

---

### test_fp_tables.py — FP Table CRUD (38 tests)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestGroupObjectTableWrite** | `test_5_5_1_1_write_single_go_read_flag` | 5.5.1.1 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_5_1_2_write_single_go_input_flags` | 5.5.1.2 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_5_1_3_write_single_go_output_flags` | 5.5.1.3 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_5_1_4_write_multiple_gos_input` | 5.5.1.4 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_5_1_5_write_multiple_gos_output` | 5.5.1.5 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_5_1_6_update_single_go` | 5.5.1.6 *(replaced by the EITT template case — eitt tier)* |
| | `test_post_nonexisting_go_creates_entry` | 5.5.1.6b |
| | `test_5_5_1_7_update_single_go_output` | 5.5.1.7 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_5_1_8_delete_go_via_post` | 5.5.1.8 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_5_1_9_invalid_write_go_empty_ga` | 5.5.1.9 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_5_1_9b_invalid_update_go_empty_ga` | 5.5.1.9b *(replaced by the EITT template case — eitt tier)* |
| **TestGroupObjectTableReadList** | `test_5_5_2_1_read_go_list` | 5.5.2.1 *(replaced by the EITT template case — eitt tier)* |
| **TestGroupObjectTableReadSingle** | `test_5_5_3_1_read_single_go` | 5.5.3.1 *(replaced by the EITT template case — eitt tier)* |
| **TestGroupObjectTableDelete** | `test_5_5_4_1_delete_single_go` | 5.5.4.1 *(replaced by the EITT template case — eitt tier)* |
| **TestRecipientTableWrite** | `test_5_5_5_1_write_single_mc_recipient` | 5.5.5.1 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_5_5_3_write_single_uc_recipient` | 5.5.5.3 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_5_5_4_write_multiple_recipients` | 5.5.5.4 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_5_5_5_update_multiple_recipients` | 5.5.5.5 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_5_5_5a_update_partial_recipients` | 5.5.5.5a *(replaced by the EITT template case — eitt tier)* |
| | `test_5_5_5_5b_update_nonexisting_recipient` | 5.5.5.5b *(replaced by the EITT template case — eitt tier)* |
| | `test_5_5_5_6_delete_recipient_via_post` | 5.5.5.6 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_5_5_7a_write_empty_ga_list` | 5.5.5.7a *(replaced by the EITT template case — eitt tier)* |
| | `test_5_5_5_8a_write_large_ga_list` | 5.5.5.8a *(replaced by the EITT template case — eitt tier)* |
| **TestRecipientTableReadList** | `test_5_5_6_1_read_recipient_list` | 5.5.6.1 *(replaced by the EITT template case — eitt tier)* |
| **TestRecipientTableReadSingle** | `test_5_5_7_1_read_single_recipient` | 5.5.7.1 *(replaced by the EITT template case — eitt tier)* |
| **TestRecipientTableDelete** | `test_5_5_8_1_delete_single_recipient` | 5.5.8.1 *(replaced by the EITT template case — eitt tier)* |
| **TestPublisherTableWrite** | `test_5_5_9_1_write_single_mc_publisher` | 5.5.9.1 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_5_9_3_write_single_uc_publisher` | 5.5.9.3 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_5_9_4_write_multiple_publishers` | 5.5.9.4 *(replaced by the EITT template case — eitt tier)* |
| | `test_update_multiple_publishers` | 5.5.9.5 |
| | `test_post_nonexisting_publisher_creates_entry` | 5.5.9.5b |
| | `test_5_5_9_6_delete_publisher_via_post` | 5.5.9.6 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_5_9_7a_write_empty_ga_list` | 5.5.9.7a *(replaced by the EITT template case — eitt tier)* |
| | `test_5_5_9_8a_write_large_ga_list` | 5.5.9.8a *(replaced by the EITT template case — eitt tier)* |
| **TestPublisherTableReadList** | `test_5_5_10_1_read_publisher_list` | 5.5.10.1 *(replaced by the EITT template case — eitt tier)* |
| **TestPublisherTableReadSingle** | `test_5_5_11_1_read_single_publisher` | 5.5.11.1 *(replaced by the EITT template case — eitt tier)* |
| **TestPublisherTableDeleteSingle** | `test_5_5_12_1_delete_single_publisher` | 5.5.12.1 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_5_12_2_delete_nonexisting_publisher` | 5.5.12.2 *(replaced by the EITT template case — eitt tier)* |

---

### test_load_state_machine.py — Load State Machine (9 tests)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestLsmTransitions** | `test_5_6_1_1_start_loading` | 5.6.1.1 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_6_1_2_load_complete` | 5.6.1.2 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_6_1_3_unload` | 5.6.1.3 *(replaced by the EITT template case — eitt tier)* |
| | `test_lsm_persist_loading` | 5.6.1.4 |
| | `test_lsm_persist_loaded` | 5.6.1.4 |
| | `test_lsm_persist_unloaded` | 5.6.1.4 |
| **TestLsmRead** | `test_read_lsm_all_states` | 5.6.2.1 |
| **TestAppProgramList** | `test_5_6_3_1_read_ap_list` | 5.6.3.1 *(replaced by the EITT template case — eitt tier)* |
| **TestProgramVersion** | `test_5_6_4_1_write_read_pv` | 5.6.4.1 *(replaced by the EITT template case — eitt tier)* |

---

### test_functional_blocks.py — Functional Blocks (2 tests)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestFunctionalBlockList** | `test_read_fb_list` | 5.7.1.1 |
| **TestFunctionalBlockDatapoints** | `test_read_fb_datapoints` | 5.7.2.1 |

---

### test_parameters.py — Parameters (7 tests)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestParameterPostWrite** | `test_5_8_1_1_write_parameter_via_post` | 5.8.1.1 *(replaced by the EITT template case — eitt tier)* |
| **TestParameterList** | `test_read_parameter_list` | 5.8.2.1 |
| **TestParameterRead** | `test_5_8_3_1_read_parameter` | 5.8.3.1 *(replaced by the EITT template case — eitt tier)* |
| **TestParameterMetadata** | `test_5_8_3_2_read_all_metadata` | 5.8.3.2 *(replaced by the EITT template case — eitt tier)* |
| | `test_read_metadata_id` | 5.8.3.2 |
| | `test_read_metadata_value` | 5.8.3.2 |
| **TestParameterWrite** | `test_5_8_4_1_write_parameter` | 5.8.4.1 *(replaced by the EITT template case — eitt tier)* |

---

### test_generic_resources.py — Generic Protocol Behavior (14 tests, 26 collected)

| Class | Test | EITT ref |
|-------|------|----------|
| **TestPaginationPageNumber** | `test_5_10_1_1_page_zero_returns_entries` | 5.10.1.1 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_10_1_1_page_beyond_range_returns_400` | 5.10.1.1 *(replaced by the EITT template case — eitt tier)* |
| **TestPaginationPageSize** | `test_5_10_1_2_first_page_with_ps1` | 5.10.1.2 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_10_1_2_second_page_with_ps2` | 5.10.1.2 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_10_1_2_beyond_range` | 5.10.1.2 *(replaced by the EITT template case — eitt tier)* |
| **TestListMetadata** | `test_5_10_1_4_total_fpg` | 5.10.1.4 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_10_1_4_total_p` | 5.10.1.4 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_10_1_4_total_auth_at` | 5.10.1.4 *(replaced by the EITT template case — eitt tier)* |
| **TestInvalidListMetadata** | `test_invalid_list_metadata_query_returns_404` | 5.10.1.5 |
| **TestPaginationNextPage** | `test_5_10_1_6_follow_next_links` | 5.10.1.6 *(replaced by the EITT template case — eitt tier)* |
| **TestDptMetadata** | `test_read_dpt_metadata_of_datapoint` | 5.10.4.1 |
| **TestDefaultOptions** | `test_5_10_5_1_accept_omitted` | 5.10.5.1 *(replaced by the EITT template case — eitt tier)* |
| | `test_5_10_5_2_content_format_omitted` | 5.10.5.2 *(replaced by the EITT template case — eitt tier)* |
| | `test_unknown_critical_option_returns_bad_option` | 5.10.5.3 |

---

### test_coap_header_budget.py — Worst-Case CoAP Header Budgets (9 tests)

Not an EITT test — pins the three mode-specific header budgets of D-002 against a
live DUT. Every request is built with each KNX URI option at its declared maximum
(`KNX_MAX_URI_PATH_LEN` 48 over 8 segments, `KNX_MAX_URI_QUERY_LEN` 64 over 4
params). An under-budgeted frame makes `coap_oscore_serialize_message()` return 0
and drop the message **silently**, so every test asserts a real answer came back.
The OSCORE cases also exercise the Echo option implicitly: the DUT challenges the
first protected request and `oscore_request()` auto-retries with the Echo value,
which is the largest inner option in the budget.

| Class | Test | What it covers |
|-------|------|----------------|
| **TestInnerHeaderBudgetWorstCase** | `test_oscore_get_with_maximum_uri_query_is_answered` | `COAP_MAX_INNER_HEADER_SIZE` with max Uri-Query |
| | `test_oscore_get_with_maximum_uri_path_is_answered` | Max Uri-Path; asserts a 4.xx class answer, not a drop |
| | `test_oscore_get_with_maximum_path_and_query_is_answered` | Both at maximum simultaneously |
| | `test_dut_still_responsive_after_worst_case_inner_frame` | DUT survives the worst-case inner frame |
| **TestPlainHeaderBudgetWorstCase** | `test_plain_discovery_with_maximum_uri_query_is_answered` | `COAP_MAX_HEADER_SIZE` (outer+inner) on unprotected `/.well-known/core` |
| | `test_plain_request_with_maximum_uri_path_is_answered` | Max Uri-Path, unprotected |
| | `test_plain_discovery_with_maximum_path_and_query_is_answered` | Both at maximum, unprotected |
| | `test_dut_still_responsive_after_worst_case_plain_frame` | DUT survives the worst-case plain frame |
| **TestOuterHeaderBudgetBlockwise** | `test_oscore_discovery_pages_are_all_delivered` | `COAP_MAX_OUTER_HEADER_SIZE` across a paginated OSCORE discovery |

---

### test_coap_payload_limit.py — Oversized Application Payload (4 tests)

Not an EITT test — verifies that a request whose payload exceeds
`OC_MAX_APP_DATA_SIZE` (`KNX_PAYLOAD_SIZE` = 768) in a single datagram is
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

Latest runs (Docker `linux-test-gcc`, default `run-in-docker.ps1 -IncludeUncommitted`, which runs two
pytest invocations: the EITT plus harness tier, then the hand-written runtime tier).

After the removal of the `/a/sen` resource: EITT plus harness **460 passed, 15 skipped, 2 xfailed**
(787.03s); runtime tier **242 passed, 8 skipped** (751.76s); 0 failed, 0 errors. Unit suite: 1649
passed (windows-test-gcc).

After the additional removal of the `ldevid` and `idevid` resources: runtime tier **241 passed,
9 skipped** (746.93s), 0 failed, 0 errors; the EITT plus harness summary was not captured, but the
container log showed no FAILED or ERROR lines and the same 2 xfails. One test moved from passed to
skipped compared with the previous run. A rerun of the runtime tier alone (`-Tier runtime`, `-rs`) gave
**242 passed, 8 skipped** (738.97s), identical to the run before the removal, so the extra skip was
intermittent, not caused by the removal. The 8 skips are: `test_security.py:3364` (slow, needs
`RUN_SLOW_TESTS`), four `test_group_comm.py` cases that cannot add an IPv6 address on `veth-dut`
(needs `--privileged`), and three `test_group_comm.py` cases (lines 2125 x2 and 2139) where the DUT
did not issue a discovery GET. These three were reproducible with the class run alone (3 passed,
3 skipped) while `iid_too_short` passed on its own, so the skips came from DUT state left by the
earlier tests of the class (per-recipient resolve status and its budget of three attempts), not from
suite load. The class setup fixture in `test_group_comm.py` now runs per test instead of per class;
with it the class gives 6 passed, 0 skipped (49.65s). The full-tier counts above were measured before
that change and have not been re-measured; expect 5 skips instead of 8. Unit suite: 1645 passed
(windows-test-gcc).

The 2 xfails are the documented EITT 5.10.1.2 (multi-IPv6 deviation) and 5.10.1.4 (`/test/*`
endpoints inflate `total` on `GET /p`); a rerun of section 5.10 alone reproduced them. The per-tier
runtime counts in the banner were not re-derived, and the older entries below predate the tier split.

Full runtime suite (Docker `linux-test-gcc`, committed HEAD of `work_in_progress`,
no `-IncludeUncommitted`):
**380 passed, 5 skipped, 0 failed, 0 errors** (385 collected, 849.06s) - fully green.
Unit suite alongside it: 1432 passed, 0 failed (windows-test-gcc).

This run was a plain re-verification (Docker Desktop had not been running; no
stack or test code changed since the previous entry below) - tally, timing and
skip set (`test_5_3_1_6d_unblocks_after_timeout`, `test_5_4_1_9_init_flag_sends_read_on_restart`,
and 3 `malformed-discovery` timing-artifact skips) are identical to the prior run.

The run before that covered the F-020 page-full return-value fix in
`oc_add_functional_blocks_from_application_to_response()` and the `is_first_occurrence_of_fb()`
comment cleanup (see F-020 in `FINDINGS.md`). Tally and skip set are identical to every prior run
in this series, confirming no regression - unsurprising, since neither change alters response
bytes: the fix only corrects a return value on a code path no current caller can reach with a
false result, and the comment change touches no code at all. As noted in F-020, this suite cannot
exercise the corrected branch either (the one caller that checks the return value never enters the
function with an already-full page); the unit tests added test-first are what pin the fix.

The run before that confirmed the array-free functional-block scan (the `NUM_FBS`-capped static
array backing FB counting/emission was replaced with `is_first_occurrence_of_fb()`, a predicate
scan over the live resource list - the change that made F-020 unit-testable in the first place).
Tally and skip set were likewise identical to the run before it (849.64s).

Two runs earlier covered the `CONTENT_NONE` Content-Format work (F-019 in `FINDINGS.md`): the
marker moved from 99999 to 64999 (the IANA documentation-reserved block, and it fits the `uint16_t`
the packet layer uses), both `coap_set_header_content_format()` and `coap_set_header_accept()` now
clear the option instead of storing the marker, `oc_ri.c:1153` resolves an absent Accept to the
resource default per KNX IoT clause 2.2.4, the stray setter call on the well-known GET was removed,
and the accept accessors were retyped to `oc_content_format_t`. Tally and skip set are identical to
the preceding run, so nothing regressed.

Note the suite confirms no regression but does not *prove* the `oc_ri.c:1153` fix: `coap_client.py`
always sends an explicit Accept, so the `CONTENT_NONE` branch is never entered at runtime. The unit
round-trip test `CoapTest.ContentFormat_NoneIsNotSerialized` is what pins that behaviour.

All 5 skips are by design, not failures:
`test_spake_lockout_unblocks_after_timeout` is behind a static `skipif` that needs
`-RunSlowTests`, `test_init_flag_sends_read_on_restart` is conditional, and the
three `test_unicast_discovery_*` malformed-length cases stand down under cumulative suite
load exactly as `tests/runtime/README.md` describes. No stack C code was changed to
make any test pass.

The most recent run covered two changes to the link-format discovery response.

First, `/.well-known/core` now omits the `if` attribute when the request carries no
query parameter, following the KNX IoT RAG clause that the response MAY carry only the
Functional Block resources to reduce the message size. A new `include_if` flag -
independent of the existing `truncate`, which only selects the URN form of an `if` that
IS emitted - is threaded through `oc_add_resource_to_response_payload()`,
`oc_check_request_from_resource()`, `oc_check_request_from_index()` and
`oc_add_functional_blocks_from_application_to_response()`. On the well-known EP the core
resources and the functional blocks pass `query_parameter_present`; the application
resources are only reached when a filter is present and therefore always pass true.
Every other caller (`/p`, `/f`, `/fb/x`, `/dev`, `/auth`, `/swu`) passes true, so those
responses are byte-identical to before.

The EITT discovery patterns survived unchanged because they anchor on `rt=` and `ct=`
rather than `if=`: a plain `</dev>;rt="urn:knx:fb.0";ct=40` still satisfies
`(?=.*;ct=40)(?=.*="urn:knx:)`. Extending the flag to the functional-block step was
load-bearing rather than cosmetic - `test_5_1_1_4e_if_wildcard` and
`test_5_1_1_3e_if_wildcard` assert that for `?if=*` every entry carries `;if=` or is
`p.next`, and the `if` wildcard arm of `oc_check_if_functional_blocks_need_to_add()`
pulls the `/f/*` links into exactly that response.

Second, the Uri-Path over-reservation in the inner header budget was corrected (the
D-002 follow-up in `FINDINGS.md`). `COAP_STRING_OPTION_GROUP_SIZE` now charges a
length-extension byte only for parts of 13 characters or more, capped by the character
budget as well as by the part count, moving `COAP_MAX_INNER_HEADER_SIZE` 219 -> 214,
`COAP_MAX_HEADER_SIZE` 273 -> 268 and the default PDU 1041 -> 1036. All 9 cases of
`test_coap_header_budget.py` pass against the tightened budgets, including the
worst-case Uri-Path and Uri-Query frames and the paginated OSCORE discovery.

The run before that covered two changes. First, the CoAP header budget was split
three ways (D-002 in `FINDINGS.md`): `COAP_MAX_OUTER_HEADER_SIZE` (54),
`COAP_MAX_INNER_HEADER_SIZE` (219) and their sum `COAP_MAX_HEADER_SIZE` (273),
each selected by the serialization mode actually in use. `KNX_PAYLOAD_SIZE` dropped
from 1024 to 768 and `COAP_MAX_MESSAGE_SIZE` became 1152, giving a 1041-byte default
PDU and a 512-byte default block.

Second, that smaller payload pushed the larger link-format bodies
(`/.well-known/core?if=*` and `?rt=*` at 688 bytes, `/dev` at 553) past the 512-byte
block boundary for the first time, which exposed a gap in the **test client**, not the
stack: `coap_client.py` had no Block1/Block2 support and silently returned only the
first block, and `multicast_get()` never populated `CoapResponse.options` so Block2 was
invisible on that path entirely. Four discovery tests failed as a result. The client now
reassembles Block2 in `get()` and `oscore_get()` (Block2 encoded as an inner Class E
option under OSCORE), and `test_5_1_1_3e_if_wildcard` discards the trailing partial
entry because RFC 7959 section 2.8 forbids reassembling a multicast response. All four
recovered with no product change. This was originally filed as a link-format truncation
defect and is now recorded as **F-017 - WITHDRAWN, NOT A DEFECT**; the stack had been
paginating correctly all along, setting the Block2 "more" bit and Size2.

The rest of the suite was unchanged, confirming no regression in block-wise transfer,
discovery, group communication, observe or security.

An earlier run verified the inbound length hardening of both parse entry
points. `coap_parse_udp_message()` in `messaging/coap/coap.c` now rejects a
datagram shorter than the fixed CoAP header and one whose announced token
exceeds the received bytes, so the token `memcpy` can no longer copy stale heap
bytes from behind the datagram into the response. `oscore_parse_outer_message()`
in `security/oc_oscore_engine.c` now applies both EMPTY-message rules of RFC 7252
clause 4.1: an EMPTY message (code 0.00) is rejected outright unless it is a RST,
and a RST that announces a token is rejected as well, so a forged
`RST, code 0.00, TKL=8` can no longer be classified as a reset while carrying 8
attacker-chosen token bytes. The five tests in `TestEmptyMessageIsNotOscore`
(`test_security.py`: CON/NON/ACK forged, the legal EMPTY-RST regression
guard, and the EMPTY RST with a token) passed, and the rest of the suite was
unchanged, confirming no regression in block-wise transfer, discovery, group
communication, observe or security.

The preceding run verified the hardened CoAP option parsing in
`messaging/coap/coap.c` and `messaging/coap/oscore.c`: the option delta/length
extension bytes and the option value are now bounds-checked by the shared
`coap_next_option()` helper before they are dereferenced, so a truncated option
header is answered with `4.02 Bad Option` (respectively routed away from the
OSCORE layer) instead of reading past the end of the datagram. The whole suite
was unchanged, confirming no regression in block-wise transfer, discovery, group
communication, observe or security.

The preceding run verified the oversized-payload handling in
`messaging/coap/coap.c` (a payload of `OC_MAX_APP_DATA_SIZE` or more is now
rejected with `4.13 Request Entity Too Large` instead of being silently
truncated) together with the one-byte-overflow fix in `api/oc_buffer.c`
(`malloc(OC_PDU_SIZE + 1)`, so the parser null-terminator behind a
buffer-filling datagram stays inside the allocation). The four new tests in
`test_coap_payload_limit.py` passed and the rest of the suite was unchanged,
confirming no regression in block-wise transfer, discovery, group
communication, observe or security.

The preceding run re-verified the stack after the buffer/PDU work: the
total-CoAP-PDU contract of `oc_set_pdu_size()`, the power-of-two floor applied
to the default block size in `api/oc_main.c`, the RFC 7959 legality guards added
to `coap_set_header_block1()` / `coap_set_header_block2()` (reject reserved SZX 7
and any non-power-of-two size), and the new `coap_floor_block_size()` helper
applied to the peer-influenced Size1 path in `messaging/coap/engine.c`. All
block-wise traffic (`test_swu.py`, `test_fp_tables.py`,
`test_load_state_machine.py`) passed unchanged, confirming no regression in live
block-wise transfer, discovery, group communication, observe or security.

This run was the first complete verification of the DNS-SD TXT record conformance
fix. Before the fix, five tests in `test_discovery_mdns.py` failed
deterministically (`test_srv_resolution_of_unconfigured_device`, `test_aaaa_resolution_of_unconfigured_device`,
`test_full_chain_by_serial_when_configured`, `test_full_chain_by_individual_address`,
`test_full_chain_via_programming_mode_subtype`), all with
`Could not resolve service info for <sn>._knx._udp.local.`. The cause was not a
code regression: `tests/common/requirements.txt` pinned `zeroconf>=0.100.0` with
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

Every section passed, including `test_group_comm.py` (the trigger and
multicast-provisioning paths), the full `test_observe.py` module, and
`test_fp_tables.py`. The **intermittent** DUT hang observed in an earlier
session (a `set_lsm cmd=2` LSM -> loaded timeout in the `TestTriggerFirstGA`
setup fixture that cascaded into the rest of `5_4` and `5_5`) did **not**
reproduce on this run, confirming it was DUT instability under cumulative load
rather than a code regression.

This run was the first to exercise the `/swu/lastupdate` lifecycle work: the
DUT-side RTC simulation in `swu_upgrade_complete_cb()`, the flipped
`test_push_based_firmware_update` step-4 assertion (timestamp must CHANGE after
an update), and the new `TestSwuLastUpdateLifecycle` class. Result:
`test_lastupdate_changes_after_swu_update` PASSED,
`test_lastupdate_survives_reboot` PASSED, and
`test_lastupdate_is_mfg_date_before_any_update` was renamed after that
run so conftest's numeric sort places it immediately before
`test_push_based_firmware_update`; it now asserts the manufacturing date
instead of skipping. Verified in an isolated Docker run
(`-IncludeUncommitted -k lastupdate`): **3 passed, 0 skipped**, meaning the
full-suite skip count drops from 6 to 5 on the next complete run.

Also added after that run: `test_state_sync_mdns_alive_in_unloaded`
in `test_discovery_mdns.py`, covering the removal of the `knx_dns_sd_stop()`
call on the LSM UNLOADED transition. Verified in an isolated Docker run
(`-IncludeUncommitted -k 902_7`): **1 passed**, 37.50s. Measured evidence:
the unload transition captured `goodbyes=0 announces=0`, and the snapshot was
byte-for-byte identical before and after
(`subtypes=['00fa10020800', 'ia1199887766-1101']`, same AAAA, same port), with
`GET /a/lsm` still answering 4.03 on the advertised endpoint.

Of the 5 skips recorded above, all were subsequently run in isolation to
establish whether they represent unverified behaviour. Four pass when run alone
(the three `bonus_*` malformed-length cases, and `6d_unblocks` once
`-RunSlowTests` is passed). Only `test_init_flag_sends_read_on_restart`
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
| `test_spake_lockout_unblocks_after_timeout` | Static `@pytest.mark.skipif(os.environ.get("RUN_SLOW_TESTS") is None, ...)` | **PASSES.** Waits out the full 10 x 6 s brute-force lockout (~60 s). The runner had no way to set the variable, so it never ran; `run-in-docker.ps1` now takes `-RunSlowTests`. Measured: `1 passed in 70.21s` via `-RunSlowTests -k 6d_unblocks` |
| `test_unicast_discovery_iid_too_short` | Conditional, `_assert_rejected()` -> `if not result.get("got_get")` | **PASSES alone** (`1 passed in 15.33s`). The rejection logic is correct; the skip in a full run is load-related, not a defect |
| `test_unicast_discovery_sn_too_long` | Conditional, same helper | **PASSES alone** (`1 passed in 15.41s`) |
| `test_unicast_discovery_sn_too_short` | Conditional, same helper | **PASSES alone** (`1 passed in 9.58s`) |
| `test_init_flag_sends_read_on_restart` | Conditional, `if len(received) == 0` | **Still skips alone.** Reported reason: `Device did not send init message after restart (may require full process restart)`. `POST /test/restart` re-initialises the stack in-process; it does not reproduce a cold boot, so the init-flag read is not emitted. This one remains genuinely unverified |

**Corrected earlier assessment.** A previous revision of this section claimed the
three `bonus_*` tests were skipped because of a "DUT/transport configuration"
gate; no such gate exists. A later revision then claimed all four conditional
skips were masking possibly-broken behaviour. Isolated runs disprove that for
three of them: they pass. The full-suite skips are a timing artefact - the
`bonus_*` tests wait up to 20 s for a discovery GET, and under cumulative suite
load the DUT does not always answer inside that window. Only
`test_init_flag_sends_read_on_restart` is genuinely not exercised.

Running the four conditional tests alone:

```powershell
.\tests\common\tools\run-in-docker.ps1 -IncludeUncommitted -k 'bonus_iid_too_short'
.\tests\common\tools\run-in-docker.ps1 -IncludeUncommitted -RunSlowTests -k '6d_unblocks'
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
`test_lastupdate_is_mfg_date_before_any_update` was renamed so conftest's
numeric sort places it before `test_push_based_firmware_update`; it now asserts
the manufacturing date and passes. Its `pytest.skip()` guard remains only for
partial `-k` runs where an update may precede it.

## Known Bugs (from unit tests)

| ID | Module | Description |
|----|--------|-------------|
| ~~F-001~~ | ~~`oc_list`~~ | ~~`oc_list_add_block()` circular link~~ — RESOLVED (function removed upstream) |
| ~~F-002~~ | ~~`oc_ri`~~ | ~~`oc_ri_get_interface_mask()` returns 1 for empty input~~ — RESOLVED (function had no production callers; removed with its `GetInterfaceMask` tests) |
| ~~F-003~~ | ~~`oc_core_res`~~ | ~~`oc_core_read_and_set_device_hostname()` reads up to 128 bytes into a 17-byte stack buffer (potential overflow)~~ — RESOLVED (buffer enlarged to `MAX_HNAME_BUFFER_SIZE` = 129; read capped at `MAX_HNAME_BUFFER_SIZE - 1` with a guaranteed trailing `\0`) |
| ~~F-004~~ | ~~`oc_buffer`~~ | ~~`oc_message_add_ref(NULL)` dereferences NULL in the trailing `OC_DBG` (debug builds only)~~ — RESOLVED (`OC_DBG` moved inside the `if (message)` guard; test `BufferPool.AddRefNullIsNoOp` now verifies NULL-safety) |
