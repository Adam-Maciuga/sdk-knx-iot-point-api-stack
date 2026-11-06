# Findings

Defects in the stack found by its tests that are still open.

A finding is removed once it is fixed (its test then asserts the corrected
behaviour) or withdrawn. Numbers are never reused, and tests and comments
keep citing them: `git log -p -S "F-NNN" -- tests/FINDINGS.md` shows a
removed entry. The next number is F-066.

## ~~F-029: `oc_endpoint_print()` frees an uninitialized string for non-IPv6 endpoints~~ - FIXED

- **Files:** `api/oc_endpoint.c`, `oc_endpoint_print()` and `oc_endpoint_to_string()`.
- **Severity:** Medium - `OC_DEBUG` builds can abort by freeing a stack address. Release builds compile `oc_endpoint_print()` out.
- **Description:** `oc_endpoint_print()` passes an uninitialized `oc_string_t` to `oc_endpoint_to_string()`. For endpoints without `IPV6`, that function returns `-1` without initializing the output, after which `oc_endpoint_print()` logs and frees the garbage pointer. Its return value is ignored.
- **Reachability:** the outbound discovery path in `api/oc_buffer.c` calls `oc_endpoint_print()` for every `DISCOVERY` message. A discovery endpoint without `IPV6` can therefore crash the debug build.
- **Evidence:** reproduced through `oc_send_message()` with `endpoint.flags = DISCOVERY`; the process aborted with `double free or corruption (out)`, and the backtrace reached `oc_endpoint_print -> oc_free_string` with a stack address passed to `free()`.
- **Suggested fix:** zero-initialize `ip_str` and only log/free it when `oc_endpoint_to_string()` succeeds. The current test sets `IPV6 | MULTICAST` to avoid masking this production defect.
- **Fix:** `ip_str` is zero-initialized and only logged and freed when `oc_endpoint_to_string()` returns 0; a non-IPv6 or NULL endpoint logs that it has no IPv6 address. Tests: `test_oc_endpoint`, `EndpointPrint.DoesNotFreeAnythingForNonIpv6Endpoint` (fills the stack with garbage first, so the uninitialized string is not zero by chance), `EndpointPrint.DoesNotCrashOnNullEndpoint`.

## ~~F-030: an oversized response (ACK) is answered with a NON 4.13 instead of being ignored~~ - FIXED

- **Files:** `messaging/coap/coap.c`, `coap_parse_udp_message()` (payload `>= OC_MAX_APP_DATA_SIZE` -> 4.13); `messaging/coap/engine.c`, `coap_receive()` (parser-error path, `echo_ctx` type `is_con ? ACK : NON`).
- **Severity:** Low - protocol deviation; the device sends a response to a response, the waiting client callback learns nothing until it times out.
- **Description:** a response the device receives as client (piggybacked ACK) with a payload of `OC_MAX_APP_DATA_SIZE` bytes or more fails in the parser with 4.13. `coap_receive()` treats every parser error alike and sends a NON 4.13 with the response's token and a fresh MID back to the peer. RFC 7252 4.2: rejecting an ACK means silently ignoring it (a rejected NON may get a RST, 4.3).
- **Evidence:** `test_coap_receive_payload`, `CoapReceivePayload.ClientResponseOfMaxAppDataSizeIsRejectedBeforeTheCallback` asserts today's NON 4.13 (the callback is not called).
- **Expected:** a rejected ACK/RST is dropped silently, a rejected NON response answered at most with a RST; then the two send expectations of that test become "nothing sent".
- **Fix:** the parser-error tail of `coap_receive()` returns without a reply when the rejected message is an ACK, a RST or a NON separate response (RFC 7252 4.2, 4.3); a rejected CON (request or separate response) keeps the error reply (ACK), as 4.2 requires an ACK or RST for it. Still open as F-053: a rejected CON separate response gets an ACK 4.13 instead of the RST 4.2 asks for. Test: `CoapReceivePayload.ClientResponseOfMaxAppDataSizeIsRejectedBeforeTheCallback` now expects nothing sent.

## ~~F-031: follow-up blocks of a block-wise /p notification carry Content-Format 0 (text/plain)~~ - FIXED

- **Files:** `messaging/coap/observe.c`, `coap_notify_observers()` (block-wise branch, `oc_blockwise_alloc_response_buffer()`); `messaging/coap/engine.c`, `coap_receive()` (Block2 continuation sets `return_content_type`).
- **Severity:** Low - a client that checks Content-Format per block, or uses the last block's, decodes the body as text/plain.
- **Description:** when a notification is larger than the observer's Block2 size, the first block goes out with the GET handler's Content-Format (application/cbor), the rest is kept in a server response buffer. That buffer's `return_content_type` is never set (calloc -> 0), and the Block2 continuation copies it into every later block, so blocks 1..n say Content-Format 0. All blocks of one representation must carry the same Content-Format (RFC 7959 2.4).
- **Evidence:** `test_coap_observe`, `CoapObserve.NotifyObserversAboveTheBlockSizeSendsTheFirstBlockAndKeepsTheRest` fetches block 1 and leaves its Content-Format unasserted because of this.
- **Expected:** `coap_notify_observers()` stores the GET handler's `content_format` in the response buffer; then that test also asserts application/cbor on block 1.
- **Fix:** `coap_notify_observers()` passes the GET handler's `content_format` to `oc_blockwise_alloc_response_buffer()` (parameter `content_format`, added for all response buffers with F-061), which stores it as `return_content_type`. Test: `test_coap_observe`, `CoapObserve.NotifyObserversAboveTheBlockSizeSendsTheFirstBlockAndKeepsTheRest` now asserts application/cbor on block 1.

## ~~F-032: a second /p notification while a block-wise one is still open loops forever~~ - FIXED

- **Files:** `messaging/coap/observe.c`, `coap_notify_observers()`, block-wise branch: the `continue` taken when a not yet completed response buffer exists for the observer.
- **Severity:** High - the event loop hangs (busy loop, a MID consumed per turn); a notification whose follow-up blocks are not (yet) fetched by the client is enough, e.g. two value changes in a row or a client that never fetches the rest.
- **Description:** the `continue` jumps back to `while (obs)` without `obs = obs->next`, so the same observer is examined again, the same unfinished buffer is found again, and the loop never ends. The buffer's timeout (`OC_EXCHANGE_LIFETIME`) cannot fire because the loop does not return.
- **Evidence:** code reading. No test: it would hang; a test for the fix calls `coap_notify_observers()` twice for an observer with Block2 64 and a 100 byte value (same set-up as `NotifyObserversAboveTheBlockSizeSendsTheFirstBlockAndKeepsTheRest`).
- **Expected:** advance to the next observer (or replace the unfinished buffer with the newer state) instead of re-examining the same one.
- **Fix:** `coap_notify_observers()` frees any earlier response buffer of the observer (finished or not) and builds the new one, so the `continue` is gone; the newer state wins (RFC 7641 4.2) and its new ETag tells the client the old body is gone. Test: `test_coap_observe`, `CoapObserve.NotifyObserversAboveTheBlockSizeTwiceReplacesTheUnfinishedBuffer` (hangs without the fix).

## ~~F-033: a rejected POST /auth/at update still changes the token's scope in RAM~~ - FIXED

- **Files:** `api/oc_knx_sec.c`, POST /auth/at handler, scope (key 9) of an existing entry (same id, update in place).
- **Severity:** Medium - a request answered with 4.00 narrows or clears the scope of a live token; its /k subscribers and its access checks follow the changed entry (the scope is read at use time).
- **Description:** the entry is changed before the scope is validated.
  - string scope: `free(at_entry->ga)`, `ga = NULL`, `ga_len = 0`, then the names are checked; no valid name -> 4.00, but the GA list is already gone.
  - int array scope: `at_entry->scope = OC_ACL_NONE`, then the empty array check -> 4.00, the token keeps no scope.
  - the change is not stored (`oc_store_at_table_entry` is not reached), so RAM and storage differ until the next reboot or a successful update.
- **Evidence:** code reading. A test: token with `<ga>` [1], POST the same id with scope `["if.unknown"]` (or `[]`) -> 4.00, then the entry still has `<ga>` [1] and a /k event for GA 1 still reaches its subscriber.
- **Expected:** validate the new scope first (into locals), change the entry only when the request is accepted.
- **Fix:** string scope: the GA list is freed and reset only after the names gave a valid mask; int array scope: the early `scope = OC_ACL_NONE` is gone (the empty check and the allocation already come before any write). Test: `test_oc_knx_sec`, `SecAtPost.RejectedScopeUpdateKeepsTheGaScope`. The `[]` case has no test: the parser makes an empty array an `OC_REP_NIL` item, so it never reaches the int array branch.
- **Not covered:** a request with a VALID scope that is rejected later still kept its changes in RAM, see F-054 (fixed).

## ~~F-034: a scope update that only changes group addresses re-initializes all OSCORE contexts~~ - FIXED

- **Files:** `api/oc_knx_sec.c`, POST /auth/at handler: `scope_updated` is set only in the string-scope branch, not in the int-array (`<ga>`) branch.
- **Severity:** Low - unnecessary work and a reset of the replay/sequence state of every context on a plain GA update (token rotation of a `<ga>` token).
- **Description:** after the loop the handler skips `oc_init_oscore_from_storage(false)` only for `OC_STATUS_CHANGED && other_updated == false && scope_updated == true`. An update of `<ga>` alone leaves `scope_updated` false and takes the re-initialization path.
- **Evidence:** code reading. A test: token with `<ga>` [1], POST the same id with `<ga>` [2] only -> 2.04, and `oc_init_oscore_from_storage` (wrapped) is not called.
- **Expected:** set `scope_updated = true` in the `<ga>` branch as in the string branch.
- **Fix:** the `<ga>` branch sets `scope_updated = true`. Test: `test_oc_knx_sec`, `SecAtPost.GaOnlyScopeUpdateKeepsTheOscoreContexts` (the token's sender context and its SSN survive a `<ga>`-only update; `oc_init_oscore_from_storage` is in the same file, so it cannot be wrapped).

## ~~F-035: a discovery response after the group object was deleted dereferences a NULL `ga`~~ - FIXED

- **Files:** `api/oc_knx_client.c`, `ipv6_for_ia_is_resolved()` (caches `recipient->ipv6_res.group_object`) and the discovery response handler (`group_object->ga[0]`); `api/oc_knx_fp.c`, `oc_free_group_object_table_entry()`.
- **Severity:** High - crash (NULL read) on a device that resolves a unicast s-mode recipient while the MaC changes the GOT.
- **Description:** the first unicast s-mode send for an unresolved IA stores a pointer to the GOT entry and starts discovery. If that GO is deleted (POST /fp/g with id only) or the device is reset before the discovery response arrives, `oc_free_group_object_table_entry()` sets `ga = NULL`, `ga_len = 0` and frees `href`. Nothing clears `ipv6_res.group_object`, so the response handler reads `ga[0]` from NULL and `oc_string(href)` from an empty string.
- **Evidence:** code reading. A test: unicast send to an unresolved IA (status RESOLVING), delete the GO, then call the discovery response handler with a matching IA/IID; under ASan it faults.
- **Expected:** clear `ipv6_res.group_object` (and reset the status) when a GO or GRT entry is freed, or store the GO id and look it up again in the response handler; check `ga_len > 0` before `ga[0]`.
- **Fix:** the GO table is only written in LOADING, so `oc_reset_unicast_resolvers()` (`api/oc_knx_client.c`) drops every resolver (deferred request, client callback, timeout handler, GO pointer, status) on each load state transition (`api/oc_knx.c`, next to `coap_free_s_mode_transactions()`) and in both reset modes of `oc_knx_device_reset()`; with the client callback gone, a late discovery response no longer reaches the handler. Test: `test_oc_knx`, `KnxHandlers.ALsmStartLoadingResetsPendingUnicastResolvers`.

## ~~F-036: deleting an observed periodic resource leaves its periodic callback behind (use-after-free)~~ - FIXED

- **Files:** `api/oc_ri.c`, `oc_ri_delete_resource()`, `add_periodic_observe_callback()`, `periodic_observe_handler()`.
- **Severity:** High - use-after-free on the next period of a deleted resource.
- **Description:** a GET observe on an `OC_PERIODIC` resource adds an entry to `observe_callbacks` with `data = resource`. `oc_ri_delete_resource()` removes the observers and frees the resource, but not that entry (`remove_periodic_observe_callback()` only runs on deregister). The next expiry calls `oc_notify_observers()` on the freed resource. Related: F-052.
- **Evidence:** code reading. A test: observe a periodic resource, `oc_ri_delete_resource()`, advance the clock past the period and poll; ASan reports heap-use-after-free.
- **Expected:** `oc_ri_delete_resource()` calls `remove_periodic_observe_callback(resource)`; deregister removes it only when the last observer is gone.
- **Fix:** `oc_ri_delete_resource()` calls `remove_periodic_observe_callback(resource)` after removing the observers and before freeing the resource (the deregister part stays open as F-052). Test: `test_oc_ri`, `RiEntityHandlerPaths.DeletingPeriodicObservedResourceStopsItsPeriodicCallback`.

## ~~F-037: the cnf:osc:alg check reads the wrong rep node, a correct token is rejected but still stored~~ - FIXED

- **Files:** `api/oc_knx_sec.c`, POST /auth/at handler, `cnf:osc` alg (8:4:4) branch.
- **Severity:** Medium - a valid request is answered with 4.00; an invalid one (missing `ms`/`osc_id`) is stored anyway.
- **Description:** the check tests `object->value.integer` (the outer `OC_REP_OBJECT`, whose `value` is a union holding a pointer) instead of `oscore_object->value.integer`, so the value 10 is effectively always rejected and `return_status` becomes `BAD_REQUEST` without returning. Because the status is no longer `CREATED`, the mandatory-property check is skipped; the entry gets its id, is stored and `oc_init_oscore_from_storage()` runs, while the client gets 4.00.
- **Evidence:** code reading. A test: POST a new token with alg 10 -> expect 2.01 (today 4.00); POST a new token with alg 10 but without `ms` -> expect 4.00 and no entry (today an entry is stored).
- **Expected:** test `oscore_object->value.integer`, and return 4.00 at once without changing the entry.
- **Fix:** the check reads `oscore_object->value.integer` and answers 4.00 at once (like the other `cnf:osc` checks), so a new entry never gets its id and is not stored; members parsed before alg in an update are still changed in RAM (F-038). Tests: `test_oc_knx_sec`, `SecAtPost.CompleteTokenWithAlgorithm10IsCreated`, `SecAtPost.AlgorithmOtherThan10IsRejectedAndNotStored`.

## ~~F-038: a rejected POST /auth/at update still changes the token's OSCORE key material in RAM~~ - FIXED

- **Files:** `api/oc_knx_sec.c`, POST /auth/at handler, `cnf:osc` members `ms`, `contextId`, `id`, `salt` of an existing entry.
- **Severity:** Medium - rejected key material can become live; complements F-033 (same in-place pattern for the scope).
- **Description:** each member frees and replaces the field of the live entry as it is parsed. A later member that fails validation (e.g. a valid `ms`, then an `osc_id` of a wrong size) returns 4.00, but the earlier fields are already replaced. The change is not stored, but the next successful AT POST or PASE calls `oc_init_oscore_from_storage()`, which builds the contexts from RAM, so the rejected key becomes active.
- **Evidence:** code reading. A test: token with `ms` A, POST the same id with `ms` B and an invalid `osc_id` -> 4.00, then the entry still holds `ms` A.
- **Expected:** parse and validate into locals, write the entry only when the whole request is accepted.
- **Fix:** with F-054: POST /auth/at parses each token into a shadow copy (allocator bits as on fp/r), a rejected request frees only its new values, the entry keeps its key material. Test: `test_oc_knx_sec`, `SecAtPost.UpdateRejectedAfterAValidMasterSecretKeepsTheOldOne` (ms B, then a too long `osc_id` -> 4.00, ms A and the sender ID unchanged).

## F-039: the endpoint list is freed by the network thread while the main thread walks it - OPEN

- **Files:** `port/linux/ipadapter.c`, `refresh_endpoints_list()` / `free_endpoints_list()` and `oc_connectivity_get_endpoints()`; same in `port/windows/ipadapter.c`. Readers: `messaging/coap/engine.c`, `api/oc_endpoint.c`, `api/oc_knx_dev.c`.
- **Severity:** Medium - use-after-free across threads; small window, triggered by an address change while a message is processed.
- **Description:** an interface change makes the network thread free every node of `dev->eps` and rebuild the list under `oc_network_event_handler_mutex`. `oc_connectivity_get_endpoints()` returns the list head and its callers iterate it without the mutex, so they can read freed nodes.
- **Evidence:** code reading.
- **Expected:** readers hold the mutex while iterating, or receive a copy; or the refresh swaps in a new list and frees the old one only when no reader is active.

## ~~F-040: allocation failures in the string/array layer write through NULL~~ - FIXED

- **Files:** `util/oc_mmem.c` (`oc_mmem_alloc()` sets `size` even when `malloc` fails); `api/oc_helpers.c`, `_oc_new_string()`, `oc_concat_strings()`, `_oc_alloc_string_array()`; `include/oc_helpers.h`.
- **Severity:** Medium - crash on heap exhaustion instead of an error; reachable from remote requests (observe registration, blockwise href, uri_query copies in `engine.c`, `separate.c`).
- **Description:** the `malloc` result is not checked; `memcpy`/`strcpy` then write to NULL, and `oc_string_len()` reports a length for a NULL buffer. The API returns `void`, so callers cannot detect the failure.
- **Evidence:** code reading. A test with a failing `malloc` wrap: `oc_new_string()` must leave an empty string (size 0, ptr NULL) instead of faulting.
- **Expected:** on failure leave `ptr = NULL`, `size = 0` and skip the copy; callers that need the string check `oc_string(x) != NULL`.
- **Fix:** `oc_mmem_alloc()` sets `size = 0` and returns 0 when `malloc` fails; `_oc_new_string()`, `_oc_new_byte_string()`, `oc_concat_strings()` and `_oc_alloc_string_array()` return before the copy when the buffer is NULL, the handle stays empty. Callers that use the string afterwards (e.g. `strcmp` on `oc_string(x)`) are not part of this fix. Tests: `test_oc_helpers_alloc_fail` (6, `-Wl,--wrap=malloc`).

## ~~F-041: the CBOR parser writes through NULL when an array or string allocation fails~~ - FIXED

- **Files:** `api/oc_rep.c`, array parsing (`oc_new_int_array`/`oc_new_bool_array`/`oc_new_double_array`/string array, then writes at index `k`), scalar strings (`oc_alloc_string` then `cbor_value_copy_*`), object/mixed arrays (`(*prev)->next = 0` after a failed nested parse).
- **Severity:** Medium - peer-triggered crash under heap pressure; string arrays reserve 32 bytes per item, so a 1 KB payload of 1-byte strings needs ~32 KB.
- **Description:**
  - a failed array allocation is not checked; the next `cbor_value_get_*`/copy writes to `NULL + k`.
  - a failed scalar string allocation leaves `ptr = NULL` with a non-zero size; TinyCBOR treats a NULL buffer as "measure only", so the handler gets NULL with a length.
  - in object/mixed arrays a failed nested parse leaves `*prev` NULL and `(*prev)->next = 0` dereferences it.
  - `*err = cbor_value_advance(&array)` overwrites the error of the nested parse, so an OOM or illegal type inside an element is dropped.
- **Evidence:** code reading. Fuzz harness or unit test with a failing `malloc` wrap at the n-th call.
- **Expected:** check each allocation, set `CborErrorOutOfMemory` and return; combine errors with `|=`.
- **Fix:** `oc_rep_allocated()` checks each string, key name and array buffer after its allocation and ends the parse with `CborErrorOutOfMemory`; a failed nested parse in an object or mixed array returns at once (before `(*prev)->next`), and the advance combines with `|=`. A failing `calloc` of a rep node still hits `oc_assert` in `_alloc_rep()` on debug builds (release builds return `CborErrorOutOfMemory`). Tests: `test_oc_rep_alloc_fail` (8, `-Wl,--wrap=malloc`).

## ~~F-042: a timed callback that removes itself and returns OC_EVENT_DONE is freed twice~~ - FIXED

- **Files:** `api/oc_ri.c`, `poll_event_callback_timers()` and `oc_ri_remove_timed_event_callback()`; `api/oc_server_api.c`, `oc_remove_delayed_callback()`.
- **Severity:** Low - double free; no stack caller does it today, but it is a natural application pattern and is not documented as forbidden.
- **Description:** inside the callback `oc_remove_delayed_callback(data, itself)` removes and frees the running entry; on `OC_EVENT_DONE` the poll loop removes and frees the same pointer again. On `OC_EVENT_CONTINUE` it is worse: the poll loop restarts the timer of the freed entry (use-after-free, the freed timer is linked into the etimer list again).
- **Evidence:** code reading. A test: a delayed callback that calls `oc_remove_delayed_callback()` on itself and returns `OC_EVENT_DONE`; ASan reports a double free.
- **Expected:** mark the running entry and defer the free, or document that a callback must not remove itself.
- **Fix:** `oc_event_callback_t` has an `callback_active` flag toggled with XOR: the poll loop sets it before the callback and toggles it after, `oc_ri_remove_timed_event_callback()` toggles it and frees only when it is then true (outside a callback); a self-removed entry is freed once by the poll loop after the callback and neither removes nor restarts it. Tests: `test_oc_ri_timed_callbacks` (3, `-Wl,--wrap=oc_clock_time`); without the fix both self-removing tests crash.

## ~~F-043: OSCORE engine frees a persistent context or leaks a temporary one on error paths~~ - FIXED

- **Files:** `security/oc_oscore_engine.c`, outbound response encryption (`unicast_echo_response_by_mc` branch).
- **Severity:** Medium - reachable: the 'receiver' side accepted a multicast OSCORE request with the kid of any token, and the engine sends an echo for any unsynced decrypted request; a peer with a valid token without group addresses triggered it.
- **Description:**
  - the temporary response sender context is only created when `auth_at->ga_len > 0`, but `oc_oscore_free_context(oscore_ctx)` runs whenever `unicast_echo_response_by_mc` is set; with `ga_len == 0` it frees the persistent context found earlier.
  - when `oc_oscore_encrypt()` fails, the function returns before that free, so the temporary context stays in the context list (LRU eviction only removes recipient contexts).
  - in that `ga_len == 0` case nothing computes the AEAD nonce and AAD: the echo response is encrypted with an uninitialized stack nonce and an empty AAD.
- **Evidence:** tests below.
- **Expected:** keep a flag "temporary context created" and free exactly that context on every exit path.
- **Fix:** (1) 'receiver' side: the token is taken from the cached context or the AT lookup, and a multicast request whose token has no group addresses is silently discarded together with an unknown kid, before a context is created and before an echo can be sent (spec 2.6.9.1 multicast = s-mode, 2.5.9.6 the token must hold the group address). (2) 'sender' side: the temporary 'Response Sender Context' is always created for a multicast caused echo response (no `ga_len` condition, so the flag `unicast_echo_response_by_mc` always means it replaced `oscore_ctx`); it is freed once after the encrypt `if`/`else`, on success and on an encrypt error. A token that loses its group addresses during the echo delay (POST /auth/at) is accepted: the echo is still encrypted with its own context. Tests: `test_oc_oscore_echo_tx`, `OscoreEngine.MulticastRequestWithTokenWithoutGroupAddressIsSilentlyDropped`, `OscoreEngine.MulticastEchoResponseEncryptFailureFreesTheTemporaryContext` (`-Wl,--wrap=oc_oscore_encrypt`).

## F-044: netlink replies shorter than a header are read past the buffer - OPEN

- **Files:** `port/linux/ipadapter.c`, the two netlink receive loops (interface list and address change).
- **Severity:** Low - heap over-read of the `malloc(response_len)` buffer; data comes only from the kernel.
- **Description:** `recv` can return fewer than `sizeof(struct nlmsghdr)` bytes, and `response->nlmsg_type` is read without checking the length (`NLMSG_OK` is not used).
- **Evidence:** code reading.
- **Expected:** check `NLMSG_OK(response, len)` before each header access.

## ~~F-045: string setters free their source when called with their own value~~ - FIXED

- **Files:** `api/oc_helpers.c`, `oc_string_copy()`, `oc_byte_string_copy()`, `oc_string_copy_from_char*()`; `api/oc_core_res.c`, `oc_core_set_device_hostname()`; `api/oc_knx_swu.c`, `oc_swu_set_package_name()`, `oc_swu_set_last_update()`.
- **Severity:** Low - read after free; no in-tree caller passes its own value today, but these are public APIs (e.g. re-announcing the current host name).
- **Description:** each setter frees the destination first and then copies from the source; when the source points into the destination (`oc_string_copy(&s, s)`, `oc_core_set_device_hostname(oc_string(info->iot_hostname))`), the copy reads freed memory.
- **Evidence:** code reading. A test: `oc_string_copy(&s, s)` under ASan.
- **Expected:** allocate and copy into a new string first, then free the old one (or return early when source and destination are the same).
- **Fix:** `oc_string_copy_from_char_with_size()` and `oc_byte_string_copy_from_char_with_size()` copy into a local `tmp` first, free the old value after it and take over `tmp`; `oc_string_copy()`, `oc_byte_string_copy()` and `oc_string_copy_from_char()` call them, so the order lives in one place. `oc_core_set_device_hostname()`, `oc_swu_set_package_name()` and `oc_swu_set_last_update()` use these setters instead of their own free + new. Tests (`--wrap=free` poisons the freed block, so a read after free shows on every platform, not only under ASan): `test_oc_helpers_alloc_fail`, `AllocFail.StringCopyOfItselfKeepsTheValue`, `AllocFail.ByteStringCopyOfItselfKeepsTheValue`; `test_oc_knx_swu`, `KnxSwuSetOwnValue.PackageNameSetWithItsOwnValueKeepsIt`, `KnxSwuSetOwnValue.LastUpdateSetWithItsOwnValueKeepsIt` (`--wrap=malloc` captures the setter's buffer).

## ~~F-046: blockwise buffer lookup matches on an href prefix~~ - FIXED

- **Files:** `api/oc_blockwise.c`, `oc_blockwise_find_buffer()`.
- **Severity:** Low - a request for `k` can be attached to the buffer of `k/1` (same endpoint, method, role, query).
- **Description:** `strncmp(href, buffer->href, href_len)` compares only the length of the incoming href, so a shorter href matches a longer stored one.
- **Evidence:** test below.
- **Expected:** compare the lengths too (`href_len == oc_string_len(buffer->href)`).
- **Fix:** `oc_blockwise_find_buffer()` compares the href in length and content (`href_len == oc_string_len(buffer->href) && memcmp(...)`), as the query. Test: `test_oc_blockwise`, `BlockwiseAlloc.FindByHrefPrefixDoesNotMatch` (buffer for `p/1`: `p` and `p/1/x` -> NULL, `p/1` -> the buffer).

## F-047: unicast s-mode loopback parses with a negative size when the value does not fit - OPEN

- **Files:** `api/oc_knx_client.c`, unicast s-mode send: `oc_s_mode_get_resource_value()` then `oc_parse_rep(buffer, resource_value_size, ...)`.
- **Severity:** Low - undefined behaviour (pointer overflow); the parse then fails and the loopback PUT handler gets `request_payload = NULL`.
- **Description:** when the application's GET handler encodes more than `max_value_size`, the size is -1; it is passed to `oc_parse_rep()` and on to `cbor_parser_init(size_t)`, where it becomes `SIZE_MAX`.
- **Evidence:** code reading.
- **Expected:** skip the parse (and the send) when `resource_value_size <= 0`.

## F-048: `oc_join_string_array()` overflows by one byte when all items are empty - OPEN

- **Files:** `api/oc_helpers.c`, `oc_join_string_array()`.
- **Severity:** Low - 1-byte heap overflow (or a NULL write if `malloc(0)` returns NULL); no production caller.
- **Description:** with only empty items `len` stays 0, `oc_alloc_string(ocstring, 0)` is called, and the final `strcpy(..., "")` writes the terminating NUL past the block.
- **Evidence:** code reading. A test: join an array of empty strings under ASan.
- **Expected:** allocate `len + 1` (or handle `len == 0` as an empty string).

## F-049: helper conversions with unchecked sizes - OPEN

- **Files:** `api/oc_helpers.c`, `oc_copy_byte_string_to_array_internal()`, `oc_conv_hex_string_to_oc_string()`.
- **Severity:** Low - no production caller today.
- **Description:**
  - `oc_copy_byte_string_to_array_internal()` asserts `strlen(str) < 32` on raw bytes but copies `str_len` bytes unchecked into a 32-byte slot.
  - `oc_conv_hex_string_to_oc_string()` allocates `len / 2` bytes, so an odd-length hex string always fails, and the allocation stays in `out` with undefined content.
- **Evidence:** code reading.
- **Expected:** bound `str_len` by the slot size; allocate `(len + 1) / 2` and free `out` on failure.

## ~~F-050: the process event queue grows without limit and is used after shutdown~~ - FIXED

- **Files:** `util/oc_process.c`, `oc_process_post()`, `oc_process_shutdown()`; `api/oc_buffer.c` (ERR_FULL unref paths).
- **Severity:** Low - a flood of events grows the queue until `oc_abort`; a post after shutdown uses freed memory.
- **Description:** `oc_process_post()` doubles the queue with `realloc` and never returns `OC_PROCESS_ERR_FULL`, so the callers' ERR_FULL paths that unref the message are dead code. `oc_process_shutdown()` frees `events` without setting it to NULL or resetting the counters.
- **Evidence:** code reading.
- **Expected:** cap the queue (return ERR_FULL at the cap) and reset `events`/counters in `oc_process_shutdown()`.
- **Fix:** `oc_process_post()` doubles the queue only up to `OC_PROCESS_MAX_EVENTS` and returns `OC_PROCESS_ERR_FULL` at the maximum, when `realloc` fails (old queue kept) or after shutdown (`events == NULL`), so the callers' unref paths are live. `oc_process_shutdown()` sets `events` to NULL and resets `nevents`, `fevent` and the queue size. Tests: `test_oc_process`, `OcProcess.PostBeyondCapacityGrowsQueueAndPreservesOrder`, `OcProcess.PostAfterShutdownIsRejected`, `OcProcess.PostAtTheMaximumIsRejectedAndTheQueueRecovers`.

## F-051: a failed block-wise buffer allocation stops notifying all remaining observers - OPEN

- **Files:** `messaging/coap/observe.c`, `coap_notify_observers()`, block-wise branch: the `break` taken when `oc_blockwise_alloc_response_buffer()` returns NULL.
- **Severity:** Low - under heap pressure the observers after the current one silently miss this notification (no error, no retry).
- **Description:** when a notification is larger than the observer's Block2 size and no response buffer can be allocated, the `break` leaves the whole `while (obs)` loop instead of skipping only this observer. Related: F-032 (the `continue` in the same branch).
- **Evidence:** code reading. A test: two observers of one resource, Block2 64, a 100 byte value, a failing `malloc` wrap for the first buffer only -> the second observer must still get its first block.
- **Expected:** skip only the current observer (`obs = obs->next; continue;`).

## ~~F-052: one observer's deregister stops the periodic notifications for all observers of the resource~~ - FIXED

- **Files:** `api/oc_ri.c`, GET with Observe = 1 (deregister) on an `OC_PERIODIC` resource, `remove_periodic_observe_callback()`.
- **Severity:** Low - the remaining observers of a periodic resource get no more periodic notifications.
- **Description:** the periodic callback exists once per resource, not per observer. When `coap_observe_handler()` removes one observer, the handler calls `remove_periodic_observe_callback(resource)` without checking whether other observers are left. Related: F-036.
- **Evidence:** test below.
- **Expected:** remove the periodic callback on deregister only when `num_observers` of the resource has dropped to 0.
- **Fix:** the deregister path in `oc_ri_invoke_coap_entity_handler()` removes the periodic callback only when `num_observers` is 0 after `coap_observe_handler()` removed this client's observer (endpoint + token); each tick notifies every observer still in the list. Tests: `test_oc_ri_periodic_observe`, `PeriodicObserve.DeregisterOfOneObserverKeepsThePeriodicNotificationsForTheOthers` (A deregisters, B is still notified, A not) and `PeriodicObserve.DeregisterOfTheLastObserverRemovesThePeriodicCallback` (the last deregister removes the callback at once, no further tick).

## F-053: a rejected CON separate response is answered with ACK 4.13 instead of a RST - OPEN

- **Files:** `messaging/coap/engine.c`, `coap_receive()`, parser-error tail (`coap_send_response_with_empty_application_payload()` with `echo_ctx` type `is_con ? ACK : NON`).
- **Severity:** Low - protocol deviation; the peer's separate response is acknowledged with an error code it does not expect for a response.
- **Description:** a CON separate response the device receives as client (e.g. payload `>= OC_MAX_APP_DATA_SIZE`, rejected by the parser with 4.13) gets the generic parser-error reply: an ACK carrying 4.13. RFC 7252 4.2: rejecting a Confirmable message is done by sending a matching Reset (empty, same MID) and otherwise ignoring it. The parser-error reply only builds ACK or NON messages, not a RST. Remainder of F-030 (rejected ACK, RST and NON responses are now ignored).
- **Evidence:** code reading. A test: like `CoapReceivePayload.ClientResponseOfMaxAppDataSizeIsRejectedBeforeTheCallback`, but the response is a CON separate response -> expect one RST, code 0.00, the response's MID, no token.
- **Expected:** for a rejected CON separate response send an empty RST with its MID instead of the ACK 4.13.

## ~~F-054: a POST /auth/at update rejected after a valid scope still changes the token in RAM~~ - FIXED

- **Files:** `api/oc_knx_sec.c`, POST /auth/at handler: the entry (same id, update in place) is written field by field while the payload is parsed.
- **Severity:** Medium - a request answered with 4.00 changes scope, profile or OSCORE fields of a live token; access checks read the entry at use time.
- **Description:** each property is written to the entry as soon as it is parsed. A later reject (e.g. `cnf:osc:alg` other than 10, short or all-zero master secret, too long sender ID, missing mandatory properties) returns 4.00 without undoing the earlier writes. The change is not stored (`oc_store_at_table_entry` is not reached), so RAM and storage differ until the next reboot or a successful update. Remainder of F-033 (an invalid scope itself no longer changes the entry).
- **Evidence:** code reading. A test: token "tok" with scope ["if.sec"], POST the same id with scope ["if.swu"] and `cnf:osc:alg` 11 -> 4.00, then the entry still has scope `OC_ACL_SEC`.
- **Expected:** parse and validate into a local copy, write the entry only when the whole request is accepted.
- **Fix:** shadow copy as on POST fp/g, fp/p, fp/r: each token is parsed into a copy of its entry, a new value gets its own allocation flagged in an allocator bitmask; a rejected request (4.00/5.00) frees only the flagged values of the copy and leaves the entry (or the empty slot) as it was, an accepted one frees the flagged values of the entry and takes over the copy (`oc_free_allocated_at_elements()`). A repeated key frees only a value of the same request (bit already set). Tests: `test_oc_knx_sec`, `SecAtPost.UpdateRejectedAfterAValidScopeKeepsTheToken`, `SecAtPost.UpdateRejectedAfterAValidMasterSecretKeepsTheOldOne`, `SecAtPost.RepeatedMasterSecretInAnUpdateKeepsTheLastOne`.

## ~~F-055: an id-only POST /auth/at (delete) keeps the token's OSCORE context when the same request also updates a scope~~ - FIXED

- **Files:** `api/oc_knx_sec.c`, POST /auth/at handler: id-only branch (`oc_delete_at_table_entry()`) and the final `scope_updated` / `other_updated` decision before `oc_init_oscore_from_storage(false)`.
- **Severity:** Medium - the OSCORE context of a deleted token stays usable; requests protected with its key still decrypt although its AT table slot is empty.
- **Description:** the id-only branch removes the token's observers, storage and RAM entry, but not its OSCORE context; the POST relies on the full re-initialization after the loop to drop it. The flags are shared by all entries of one request, so a request that deletes one token and changes only the scope of another (`[{0: "A"}, {0: "B", 9: ["if.sec"]}]` or `9: [1]`) ends with `OC_STATUS_CHANGED`, `scope_updated` and no `other_updated` -> no re-initialization, the context of "A" survives. DELETE /auth/at/{id} does not have this gap, it calls `oc_oscore_free_contexts_at_id()` itself.
- **Evidence:** code reading. A test: create tokens "A" and "B", POST `[{0: "A"}, {0: "B", 9: [1]}]` -> 2.04, then no OSCORE context refers to "A"'s slot (`oc_oscore_find_context_by_access_token`).
- **Expected:** the id-only branch frees the token's OSCORE contexts as DELETE /auth/at/{id} does (or forces the re-initialization).
- **Fix:** `oc_delete_at_table_entry()` itself frees all OSCORE contexts of the token (see F-057), so the id-only branch no longer depends on the re-initialization. Test: `test_oc_knx_sec`, `SecAtPost.IdOnlyDeleteWithScopeOnlyUpdateFreesTheDeletedTokensContext`.

## ~~F-056: a POST /auth/at that changes the profile together with the scope does not re-initialize the OSCORE contexts~~ - FIXED

- **Files:** `api/oc_knx_sec.c`, POST /auth/at handler: profile (key 38) does not set `other_updated`.
- **Severity:** Low - a token switched away from `coap_oscore` keeps its OSCORE context, a token switched to it gets none, until the next re-initialization (next key change, create, or reboot).
- **Description:** the re-initialization is skipped for `OC_STATUS_CHANGED && !other_updated && scope_updated`. The profile decides in `oc_init_oscore_from_storage()` whether a context is built at all, but setting it does not mark `other_updated`, so an update of profile + scope only takes the "scope only" path.
- **Evidence:** code reading. A test: token "tok" with profile `coap_oscore` (context exists), POST the same id with profile `coap_dtls` and scope ["if.sec"] -> 2.04, then no OSCORE context refers to the token.
- **Expected:** setting the profile sets `other_updated = true`.
- **Fix:** the profile branch sets `other_updated = true`. Test: `test_oc_knx_sec`, `SecAtPost.ProfileAndScopeUpdateReinitializesTheOscoreContexts`.

## ~~F-057: deleting AT table entries on a device reset keeps the recipient OSCORE contexts of the deleted tokens~~ - FIXED

- **Files:** `api/oc_knx_sec.c`, `oc_delete_at_table_entry()`, `oc_delete_at_table_except_sec_scope_entries()`; `oc_init_oscore_from_storage()` -> `oc_oscore_free_sender_contexts()`.
- **Severity:** Low - stale contexts that point to an empty AT table slot stay in the context list (the access check finds no scope for them, so no access); they occupy context buffers and a later token in the same slot would find them.
- **Description:** `oc_delete_at_table_entry()` did not free OSCORE contexts; each caller had to do it. The device reset path (`oc_delete_at_table_except_sec_scope_entries()` in `oc_knx_device_reset()`) relied on `oc_init_oscore_from_storage(true)` afterwards, which frees sender contexts only (`recipient_id_len == 0`), so recipient contexts of the deleted non-`if.sec` tokens survived. F-055 was the same gap on the POST id-only path.
- **Evidence:** test below, fails without the fix.
- **Fix:** `oc_delete_at_table_entry()` calls `oc_oscore_free_contexts_at_id()` (all contexts of the slot, sender and recipient); the separate calls in DELETE /auth/at/{id}, the POST id-only branch and the PASE token removal are gone. Test: `test_oc_knx_sec`, `SecAtPost.DeletingAnEntryFreesItsSenderAndRecipientContexts`.

## ~~F-058: a periodic observe callback whose GET handler deletes its own resource is freed twice~~ - FIXED

- **Files:** `api/oc_ri.c`, `poll_event_callback_timers()` (observe list), `periodic_observe_handler()`, `remove_periodic_observe_callback()`; reached through `oc_ri_delete_resource()`.
- **Severity:** Low - needs an application GET handler that deletes its own resource while a periodic notification is built; no stack code does it.
- **Description:** the same pattern as F-042 on the observe list. `periodic_observe_handler()` runs `oc_notify_observers()`, which calls the resource's GET handler; if that handler deletes the resource, `oc_ri_delete_resource()` calls `remove_periodic_observe_callback()`, which unlinks and frees the running entry. Back in the poll loop the entry is freed again (`OC_EVENT_DONE`) or its timer restarted (`OC_EVENT_CONTINUE`). The deregister path (`OC_OBSERVE_DEREGISTER`) only runs for an incoming request, never inside the poll loop. Note: in this path the resource itself is freed while the notification code still uses it, see F-059.
- **Evidence:** test below.
- **Expected:** `remove_periodic_observe_callback()` uses the `callback_active` toggle of F-042 and frees only outside the running callback.
- **Fix:** `remove_periodic_observe_callback()` toggles `callback_active` (as `oc_ri_remove_timed_event_callback()`, F-042) and frees only when it is then true; a running entry is freed once by the poll loop. Tests: `test_oc_ri_periodic_observe` (2, the notification is wrapped and deletes the resource, so the freed resource of F-059 is not touched); without the fix the DONE test crashes, the CONTINUE case shows under ASan.

## ~~F-059: a resource deleted from its own handler is used after it is freed; `oc_delayed_delete_resource()` is declared but not implemented~~ - FIXED (by contract)

- **Files:** `api/oc_ri.c`, `oc_ri_invoke_coap_entity_handler()` (after the method handler call) and `oc_ri_delete_resource()`; `messaging/coap/observe.c`, `coap_notify_observers()` (after the GET handler call); `include/oc_api.h`, `oc_delayed_delete_resource()`.
- **Severity:** Low - needs an application handler that deletes its own resource; no stack code does it, but the API offers no safe way to do so.
- **Description:**
  - a GET/PUT/POST/DELETE handler that calls `oc_ri_delete_resource()` on its own resource frees it (strings, runtime data, the resource); `oc_ri_invoke_coap_entity_handler()` then still reads `matching_resource` (observe handling: `properties`, `OC_PERIODIC`, observer registration).
  - during a notification `coap_notify_observers()` calls the GET handler and then still reads `resource->runtime_data` and compares `obs->resource` with the freed pointer; the periodic observe entry of that resource is freed twice as well (F-058).
  - `oc_delayed_delete_resource()` ("Schedule a callback to remove a resource") is declared in `oc_api.h` but has no implementation, a caller gets a link error.
- **Evidence:** code reading.
- **Expected:** implement `oc_delayed_delete_resource()` (a timed callback that calls `oc_ri_delete_resource()` outside any handler) and document that a handler must use it instead of deleting its resource directly; or remove the declaration and document the rule.
- **Fix:** the declaration of `oc_delayed_delete_resource()` is removed from `oc_api.h` (no implementation, no caller); the new public `oc_delete_resource()` in `oc_api.h` (counterpart of `oc_add_resource()`, wraps `oc_ri_delete_resource()`) states that it must not be called from the resource's own handler or notification and that a delete as the result of a request is scheduled with `oc_set_delayed_callback()`. No behaviour change of the delete itself. Test: `test_oc_server_api`, `ServerApiResource.AddThenDeleteResource`.

## ~~F-060: a new master secret written with POST /auth/at does not reach the cached recipient context, the old key still decrypts~~ - FIXED

- **Files:** `api/oc_knx_sec.c`, POST /auth/at handler -> `oc_init_oscore_from_storage()`; `security/oc_oscore_context.c`, `oc_oscore_free_sender_contexts()`; `security/oc_oscore_engine.c`, `oc_oscore_receive_message()` (context lookup by `kid` + `kid_context`).
- **Severity:** Medium - a key rotation through an AT update does not take effect for inbound traffic: requests protected with the OLD key keep being accepted until the cached recipient context is evicted (LRU) or the table is deleted.
- **Description:** an update with key material (`cnf:osc:ms`, `id`, `contextId`, `salt`, profile) sets `other_updated` and calls `oc_init_oscore_from_storage()`, which frees and rebuilds only the sender contexts (`recipient_id_len == 0`). A recipient context created for an earlier inbound request keeps its keys derived from the old master secret; the next inbound request with the same `kid` / `kid_context` finds it (`oc_oscore_find_context_by_kid_and_kid_context`) and is decrypted with the old key. Replay records and the echo ring are not touched by an update either (only DELETE /auth/at clears them).
- **Evidence:** code reading.
- **Expected:** an update with key material frees all contexts of that token (sender AND recipient, e.g. `oc_oscore_free_contexts_at_id()` for the entry) before the re-initialization.
- **Fix:** spec 2.6.7.3 does not allow updating the `cnf` attributes; the conformance template still overwrites an existing token with a full token carrying a new key and expects 2.04 (5.3.8.4, 5.3.8.6, 5.4.1.5). So (1) cnf is accepted only in a full token (all mandatory elements, an overwrite); a partial update carrying cnf is rejected with 4.00 and the token stays as it is (new bit `AT_PROP_CNF`, same check as an incomplete create). (2) an existing token rewritten with a NEW key (the values of `osc:id`, `ms`, `salt`, `contextId` or `profile` differ from the live entry, compared with `oc_byte_string_cmp()`) gets ALL its contexts freed with `oc_oscore_free_contexts_at_id()` before the shadow copy is taken over; the sender contexts are rebuilt by `oc_init_oscore_from_storage()`, a recipient context again from the new key on the next request. A full token re-POSTed with the SAME key keeps its contexts: conformance 5.10.5.2 re-POSTs `TestCon` protected with `TestCon` itself, a freed recipient context could not encrypt the response (no response, request timeout; found by the conformance tier with a first version that freed on every cnf/profile write). A scope-only update keeps the contexts. Tests: `test_oc_knx_sec`, `SecAtPost.FullOverwriteWithNewKeyFreesTheCachedRecipientContext`, `SecAtPost.FullOverwriteWithTheSameKeyKeepsTheCachedRecipientContext`, `SecAtPost.PartialUpdateWithCnfIsRejected`, `SecAtPost.ScopeOnlyUpdateKeepsTheCachedRecipientContext`.

## ~~F-061: a block-wise separate response keeps neither the Content-Format nor the query in its response buffer~~ - FIXED

- **Files:** `api/oc_server_api.c`, separate response with a payload above the block size (`oc_blockwise_alloc_response_buffer()` for the separate request, two call sites); served by `messaging/coap/engine.c` (follow-up GET with Block2, `coap_set_header_content_format(..., response_buffer->return_content_type)`); `api/oc_blockwise.c`, `oc_blockwise_find_buffer()` (query match).
- **Severity:** Low - a block-wise separate response is rare; the client gets a wrong Content-Format on the follow-up blocks or a lookup miss.
- **Description:**
  - `oc_blockwise_init_buffer()` leaves `return_content_type` and `uri_query` to the caller. The normal response path (`oc_ri.c`) and the notification path (`observe.c`, F-031) set the Content-Format; the separate response path sets neither.
  - `return_content_type` stays 0, so the follow-up blocks served from the buffer carry Content-Format 0 (text/plain) instead of the format of the first block (RFC 7959 2.4: all blocks of a body carry the same Content-Format).
  - `uri_query` stays empty; when the original request had a query and the client repeats it in the follow-up GET, `oc_blockwise_find_buffer()` (`query_len == oc_string_len(uri_query)`) does not find the buffer.
  - a block-wise separate response is never sent: the lookup for a running earlier transfer finds the build buffer `handle->response_state` (the buffer the application writes the response body into) (same href, endpoint, method, role, no query; `payload_size` set, `next_block_offset` 0) and takes the 'earlier transfer still served' branch; no response is serialized, the transaction created before stays in the transaction list, the client gets nothing.
- **Evidence:** test below.
- **Expected:** after allocating the separate response buffer, store the Content-Format of the response (as `observe.c` does) and the query of the separate request (store it with the separate request if needed); the lookup must not take the build buffer for an earlier transfer, and an unused transaction is cleared.
- **Fix:** (1) `oc_blockwise_alloc_response_buffer()` takes the Content-Format as a parameter and stores it as `return_content_type`; every caller passes it (normal response, notification, separate transfer buffer), buffers that are not served pass `CONTENT_NONE`. (3) the build buffer of a separate response (`handle->response_state`, the application writes the body into it) gets its own role `OC_BLOCKWISE_SEPARATE_BUILD`, so no lookup finds it; the response is sent and a transfer buffer is allocated; a transaction that is not used (earlier transfer still served, alloc failed) is cleared. (2) the query is not stored: today no separate response with a payload above one block answers a request with a query (SPAKE has none, `/a/swu` answers 2.04 without payload); the reason is noted in `coap_separate_t`. Test: `test_separate`, `Separate.BlockwiseResponseBufferKeepsTheContentFormat` (100 bytes, block size 64: first block sent, transfer buffer with offset 64 and CBOR; `--wrap=oc_main_initialized`).

## ~~F-062: follow-up blocks of a block-wise GET response carry the guessed Content-Format instead of the handler's~~ - FIXED

- **Files:** `api/oc_ri.c`, `oc_ri_invoke_coap_entity_handler()` (block-wise response buffer allocated before the handler; first block Content-Format after it); served by `messaging/coap/engine.c` (follow-up GET with Block2, `response_buffer->return_content_type`).
- **Severity:** Low - a client that checks Content-Format per block, or uses the last block's, decodes the body with the wrong format.
- **Description:** the response buffer is allocated BEFORE the handler runs, with the Accept option or the resource default (`content_type[0]`) as its Content-Format. The first block goes out with the format the handler actually set (`response_buffer.content_format`), the follow-up blocks with the guessed one. Without Accept and without a bound content type (`content_type[0] = 0`) the follow-up blocks carry Content-Format 0 (text/plain) while the first block is application/cbor (RFC 7959 2.4: all blocks of a body carry the same Content-Format). Same class as F-031 (notifications) and F-061 (separate response).
- **Evidence:** test below, fails without the fix (block 1 carries the resource default instead of the handler's CBOR).
- **Expected:** the buffer takes the handler's Content-Format once the handler has run.
- **Fix:** after the handler, `oc_ri_invoke_coap_entity_handler()` stores `response_buffer.content_format` as `return_content_type` of the response buffer (next to `payload_size`). Test: `test_coap_blockwise_server`, `CoapBlockwiseServer.Block2FollowUpCarriesTheHandlersContentFormat` (resource default octet-stream, the handler answers CBOR: block 0 and block 1 carry CBOR). Found by `Block2FollowUpWithTheSameQueryIsServedFromTheBuffer` with an unbound resource; since F-063 an unbound resource defaults to CBOR, so that test no longer depends on this fix.

## ~~F-063: an application resource without a bound Content-Format defaults to 0 (text/plain), not to application/cbor~~ - FIXED

- **Files:** `api/oc_server_api.c`, `oc_new_resource()` (resource from `calloc`, `content_type[]` not set) and `oc_resource_bind_content_type()` (optional); used by `api/oc_ri.c` (default format without an Accept option, response buffer guess) and the link-format `ct=` of the resource.
- **Severity:** Low - an application that does not call `oc_resource_bind_content_type()` gets text/plain as the resource default; since F-062 the block-wise follow-up blocks no longer depend on it.
- **Description:** `oc_new_resource()` leaves `content_type[0]` / `[1]` at 0, and `oc_add_resource()` does not check them. Content-Format 0 is text/plain, so 'not bound' and 'text/plain' cannot be told apart, and every place that takes the resource default (request without Accept, spec 2.2.4) uses text/plain. Spec 2.2.4: Points SHALL use application/cbor, and without Accept / Content-Format the device SHALL assume the default format of the resource; text/plain is not one of the formats a KNX IoT resource supports (link-format, octet-stream, CBOR, optional JSON).
- **Evidence:** test below; spec 2.2.4 and 2.5.11.1 (Table 32: `p` cbor).
- **Expected:** `oc_new_resource()` binds application/cbor as the default (`content_type[0] = APPLICATION_CBOR`, `[1] = CONTENT_NONE`); an application overrides it with `oc_resource_bind_content_type()`.
- **Fix:** `oc_new_resource()` sets `content_type[0] = APPLICATION_CBOR`, `content_type[1] = CONTENT_NONE` (spec 2.2.4); `oc_resource_bind_content_type()` still overrides it. Test: `test_oc_server_api`, `ServerApiResource.NewResourceDefaultsToCborContentFormat` (default CBOR, override with octet-stream).

## ~~F-064: an OSCORE message that cannot be queued for sending is never released~~ - FIXED

- **Files:** `security/oc_oscore_engine.c`, `oc_oscore_send_multicast_message()` and `oc_oscore_send_unicast_message()`: `oc_process_post(&message_buffer_handler, OUTBOUND_NETWORK_EVENT_ENCRYPTED, outgoing_msg) == OC_PROCESS_ERR_FULL`.
- **Severity:** Low - each rejected post loses one message buffer for good; under an event flood the message pool runs dry.
- **Description:** on `OC_PROCESS_ERR_FULL` both send paths only log "could not send message" and keep `outgoing_msg` (the message encrypted in place, or its clone for a shared CON message). The other callers (`oc_receive_message()`, `oc_send_message()`, the OSCORE inbound hand-over to the CoAP engine) release their message on `ERR_FULL`. Hidden until F-050: `oc_process_post()` never returned `ERR_FULL` before.
- **Evidence:** `test_oc_oscore_echo_tx`, `OscoreEngine.OutboundMulticastWithFullQueueReleasesTheMessage` and `OscoreEngine.OutboundUnicastWithFullQueueReleasesTheMessage` (queue filled to `OC_PROCESS_MAX_EVENTS`, engine run with `oc_process_post_synch()`): no `oc_message_unref()` of the message.
- **Expected:** release `outgoing_msg` with `oc_message_unref()` in both `ERR_FULL` branches.
- **Fix:** both send paths hand `outgoing_msg` to `oc_message_post()` (F-065), which releases it on `ERR_FULL` (in place: the only reference; shared: the clone, the original stays with its transaction); the message is not used after the post. Tests: `test_oc_oscore_echo_tx`, `OscoreEngine.OutboundMulticastWithFullQueueReleasesTheMessage`, `OscoreEngine.OutboundUnicastWithFullQueueReleasesTheMessage` (the message is handed to `oc_message_post` and rejected there), `OscoreEngine.InboundRequestWithFullQueueReleasesTheMessage` (the inbound hand-over to the CoAP engine).

## ~~F-065: the message routing process leaks a message, and an interface event is lost, when the event queue is full~~ - FIXED

- **Files:** `api/oc_buffer.c`, `message_buffer_handler` (forwards to the OSCORE layer and the CoAP engine: inbound OSCORE / plain, outbound OSCORE multicast / unicast); `api/oc_network_events.c`, `oc_process_network_event()` (`INTERFACE_UP` / `INTERFACE_DOWN`).
- **Severity:** Low - each rejected forward loses one message buffer for good (every plain and OSCORE message passes this handler); a rejected interface event is never handled (e.g. endpoints not refreshed after an address change).
- **Description:**
  - `message_buffer_handler` takes over the reference of the message from the queue and posts it on; the return value of these 4 `oc_process_post()` calls is ignored, so on `OC_PROCESS_ERR_FULL` nobody holds the message any more. Same pattern as F-064 in the OSCORE engine; hidden until F-050.
  - `oc_process_network_event()` resets `interface_up` / `interface_down` even when the post was rejected, so the event is dropped.
  - the release of a message on `ERR_FULL` was written by hand at every caller (5 places) and missing at 6 (4 here, 2 in F-064).
- **Evidence:** tests below.
- **Expected:** one place releases a message whose post is rejected (all message posts go through it); an interface event stays pending and is posted again (as the etimer does for its timer event).
- **Fix:** new `oc_message_post()` (`oc_buffer.c/.h`): posts a message and releases it on `OC_PROCESS_ERR_FULL`; all 9 message posts use it (`oc_receive_message`, `oc_send_message`, the 4 routes of `message_buffer_handler`, the 3 OSCORE posts), the hand-written `ERR_FULL` branches are gone; the caller does not use the message after the call. `oc_process_network_event()` resets `interface_up` / `interface_down` only when the post was accepted and polls its own process while one is pending, so the next run retries. Tests: `test_oc_buffer`, `BufferProcess.RouteInboundPlainWithFullQueueReleasesTheMessage`, `...RouteInboundOscoreWithFullQueueReleasesTheMessage`, `...RouteOutboundOscoreMulticastWithFullQueueReleasesTheMessage`, `...RouteOutboundOscoreUnicastWithFullQueueReleasesTheMessage`; `test_oc_network_events`, `NetworkEvents.InterfaceEventWithFullQueueStaysPendingAndIsDispatchedLater`.
