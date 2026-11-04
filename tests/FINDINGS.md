# Findings

Defects in the stack found by its tests that are still open.

A finding is removed once it is fixed (its test then asserts the corrected
behaviour) or withdrawn. Numbers are never reused, and tests and comments
keep citing them: `git log -p -S "F-NNN" -- tests/FINDINGS.md` shows a
removed entry. The next number is F-054.

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
- **Fix:** `coap_notify_observers()` copies the GET handler's `content_format` into the response buffer's `return_content_type` right after the payload. Test: `test_coap_observe`, `CoapObserve.NotifyObserversAboveTheBlockSizeSendsTheFirstBlockAndKeepsTheRest` now asserts application/cbor on block 1.

## F-032: a second /p notification while a block-wise one is still open loops forever - OPEN

- **Files:** `messaging/coap/observe.c`, `coap_notify_observers()`, block-wise branch: the `continue` taken when a not yet completed response buffer exists for the observer.
- **Severity:** High - the event loop hangs (busy loop, a MID consumed per turn); a notification whose follow-up blocks are not (yet) fetched by the client is enough, e.g. two value changes in a row or a client that never fetches the rest.
- **Description:** the `continue` jumps back to `while (obs)` without `obs = obs->next`, so the same observer is examined again, the same unfinished buffer is found again, and the loop never ends. The buffer's timeout (`OC_EXCHANGE_LIFETIME`) cannot fire because the loop does not return.
- **Evidence:** code reading. No test: it would hang; a test for the fix calls `coap_notify_observers()` twice for an observer with Block2 64 and a 100 byte value (same set-up as `NotifyObserversAboveTheBlockSizeSendsTheFirstBlockAndKeepsTheRest`).
- **Expected:** advance to the next observer (or replace the unfinished buffer with the newer state) instead of re-examining the same one.

## F-033: a rejected POST /auth/at update still changes the token's scope in RAM - OPEN

- **Files:** `api/oc_knx_sec.c`, POST /auth/at handler, scope (key 9) of an existing entry (same id, update in place).
- **Severity:** Medium - a request answered with 4.00 narrows or clears the scope of a live token; its /k subscribers and its access checks follow the changed entry (the scope is read at use time).
- **Description:** the entry is changed before the scope is validated.
  - string scope: `free(at_entry->ga)`, `ga = NULL`, `ga_len = 0`, then the names are checked; no valid name -> 4.00, but the GA list is already gone.
  - int array scope: `at_entry->scope = OC_ACL_NONE`, then the empty array check -> 4.00, the token keeps no scope.
  - the change is not stored (`oc_store_at_table_entry` is not reached), so RAM and storage differ until the next reboot or a successful update.
- **Evidence:** code reading. A test: token with `<ga>` [1], POST the same id with scope `["if.unknown"]` (or `[]`) -> 4.00, then the entry still has `<ga>` [1] and a /k event for GA 1 still reaches its subscriber.
- **Expected:** validate the new scope first (into locals), change the entry only when the request is accepted.

## F-034: a scope update that only changes group addresses re-initializes all OSCORE contexts - OPEN

- **Files:** `api/oc_knx_sec.c`, POST /auth/at handler: `scope_updated` is set only in the string-scope branch, not in the int-array (`<ga>`) branch.
- **Severity:** Low - unnecessary work and a reset of the replay/sequence state of every context on a plain GA update (token rotation of a `<ga>` token).
- **Description:** after the loop the handler skips `oc_init_oscore_from_storage(false)` only for `OC_STATUS_CHANGED && other_updated == false && scope_updated == true`. An update of `<ga>` alone leaves `scope_updated` false and takes the re-initialization path.
- **Evidence:** code reading. A test: token with `<ga>` [1], POST the same id with `<ga>` [2] only -> 2.04, and `oc_init_oscore_from_storage` (wrapped) is not called.
- **Expected:** set `scope_updated = true` in the `<ga>` branch as in the string branch.

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

## F-038: a rejected POST /auth/at update still changes the token's OSCORE key material in RAM - OPEN

- **Files:** `api/oc_knx_sec.c`, POST /auth/at handler, `cnf:osc` members `ms`, `contextId`, `id`, `salt` of an existing entry.
- **Severity:** Medium - rejected key material can become live; complements F-033 (same in-place pattern for the scope).
- **Description:** each member frees and replaces the field of the live entry as it is parsed. A later member that fails validation (e.g. a valid `ms`, then an `osc_id` of a wrong size) returns 4.00, but the earlier fields are already replaced. The change is not stored, but the next successful AT POST or PASE calls `oc_init_oscore_from_storage()`, which builds the contexts from RAM, so the rejected key becomes active.
- **Evidence:** code reading. A test: token with `ms` A, POST the same id with `ms` B and an invalid `osc_id` -> 4.00, then the entry still holds `ms` A.
- **Expected:** parse and validate into locals, write the entry only when the whole request is accepted.

## F-039: the endpoint list is freed by the network thread while the main thread walks it - OPEN

- **Files:** `port/linux/ipadapter.c`, `refresh_endpoints_list()` / `free_endpoints_list()` and `oc_connectivity_get_endpoints()`; same in `port/windows/ipadapter.c`. Readers: `messaging/coap/engine.c`, `api/oc_endpoint.c`, `api/oc_knx_dev.c`.
- **Severity:** Medium - use-after-free across threads; small window, triggered by an address change while a message is processed.
- **Description:** an interface change makes the network thread free every node of `dev->eps` and rebuild the list under `oc_network_event_handler_mutex`. `oc_connectivity_get_endpoints()` returns the list head and its callers iterate it without the mutex, so they can read freed nodes.
- **Evidence:** code reading.
- **Expected:** readers hold the mutex while iterating, or receive a copy; or the refresh swaps in a new list and frees the old one only when no reader is active.

## F-040: allocation failures in the string/array layer write through NULL - OPEN

- **Files:** `util/oc_mmem.c` (`oc_mmem_alloc()` sets `size` even when `malloc` fails); `api/oc_helpers.c`, `_oc_new_string()`, `oc_concat_strings()`, `_oc_alloc_string_array()`; `include/oc_helpers.h`.
- **Severity:** Medium - crash on heap exhaustion instead of an error; reachable from remote requests (observe registration, blockwise href, uri_query copies in `engine.c`, `separate.c`).
- **Description:** the `malloc` result is not checked; `memcpy`/`strcpy` then write to NULL, and `oc_string_len()` reports a length for a NULL buffer. The API returns `void`, so callers cannot detect the failure.
- **Evidence:** code reading. A test with a failing `malloc` wrap: `oc_new_string()` must leave an empty string (size 0, ptr NULL) instead of faulting.
- **Expected:** on failure leave `ptr = NULL`, `size = 0` and skip the copy; callers that need the string check `oc_string(x) != NULL`.

## F-041: the CBOR parser writes through NULL when an array or string allocation fails - OPEN

- **Files:** `api/oc_rep.c`, array parsing (`oc_new_int_array`/`oc_new_bool_array`/`oc_new_double_array`/string array, then writes at index `k`), scalar strings (`oc_alloc_string` then `cbor_value_copy_*`), object/mixed arrays (`(*prev)->next = 0` after a failed nested parse).
- **Severity:** Medium - peer-triggered crash under heap pressure; string arrays reserve 32 bytes per item, so a 1 KB payload of 1-byte strings needs ~32 KB.
- **Description:**
  - a failed array allocation is not checked; the next `cbor_value_get_*`/copy writes to `NULL + k`.
  - a failed scalar string allocation leaves `ptr = NULL` with a non-zero size; TinyCBOR treats a NULL buffer as "measure only", so the handler gets NULL with a length.
  - in object/mixed arrays a failed nested parse leaves `*prev` NULL and `(*prev)->next = 0` dereferences it.
  - `*err = cbor_value_advance(&array)` overwrites the error of the nested parse, so an OOM or illegal type inside an element is dropped.
- **Evidence:** code reading. Fuzz harness or unit test with a failing `malloc` wrap at the n-th call.
- **Expected:** check each allocation, set `CborErrorOutOfMemory` and return; combine errors with `|=`.

## F-042: a timed callback that removes itself and returns OC_EVENT_DONE is freed twice - OPEN

- **Files:** `api/oc_ri.c`, `poll_event_callback_timers()` and `oc_ri_remove_timed_event_callback()`; `api/oc_server_api.c`, `oc_remove_delayed_callback()`.
- **Severity:** Low - double free; no stack caller does it today, but it is a natural application pattern and is not documented as forbidden.
- **Description:** inside the callback `oc_remove_delayed_callback(data, itself)` removes and frees the running entry; on `OC_EVENT_DONE` the poll loop removes and frees the same pointer again.
- **Evidence:** code reading. A test: a delayed callback that calls `oc_remove_delayed_callback()` on itself and returns `OC_EVENT_DONE`; ASan reports a double free.
- **Expected:** mark the running entry and defer the free, or document that a callback must not remove itself.

## F-043: OSCORE engine frees a persistent context or leaks a temporary one on error paths - OPEN

- **Files:** `security/oc_oscore_engine.c`, outbound response encryption (`unicast_echo_response_by_mc` branch).
- **Severity:** Low - latent; the free needs a multicast-caused echo response for a token without group addresses, which is not reachable today.
- **Description:**
  - the temporary response sender context is only created when `auth_at->ga_len > 0`, but `oc_oscore_free_context(oscore_ctx)` runs whenever `unicast_echo_response_by_mc` is set; with `ga_len == 0` it frees the persistent context found earlier.
  - when `oc_oscore_encrypt()` fails, the function returns before that free, so the temporary context stays in the context list (LRU eviction only removes recipient contexts).
- **Evidence:** code reading.
- **Expected:** keep a flag "temporary context created" and free exactly that context on every exit path.

## F-044: netlink replies shorter than a header are read past the buffer - OPEN

- **Files:** `port/linux/ipadapter.c`, the two netlink receive loops (interface list and address change).
- **Severity:** Low - heap over-read of the `malloc(response_len)` buffer; data comes only from the kernel.
- **Description:** `recv` can return fewer than `sizeof(struct nlmsghdr)` bytes, and `response->nlmsg_type` is read without checking the length (`NLMSG_OK` is not used).
- **Evidence:** code reading.
- **Expected:** check `NLMSG_OK(response, len)` before each header access.

## F-045: string setters free their source when called with their own value - OPEN

- **Files:** `api/oc_helpers.c`, `oc_string_copy()`, `oc_byte_string_copy()`, `oc_string_copy_from_char*()`; `api/oc_core_res.c`, `oc_core_set_device_hostname()`; `api/oc_knx_swu.c`, `oc_swu_set_package_name()`, `oc_swu_set_last_update()`.
- **Severity:** Low - read after free; no in-tree caller passes its own value today, but these are public APIs (e.g. re-announcing the current host name).
- **Description:** each setter frees the destination first and then copies from the source; when the source points into the destination (`oc_string_copy(&s, s)`, `oc_core_set_device_hostname(oc_string(info->iot_hostname))`), the copy reads freed memory.
- **Evidence:** code reading. A test: `oc_string_copy(&s, s)` under ASan.
- **Expected:** allocate and copy into a new string first, then free the old one (or return early when source and destination are the same).

## F-046: blockwise buffer lookup matches on an href prefix - OPEN

- **Files:** `api/oc_blockwise.c`, `oc_blockwise_find_buffer()`.
- **Severity:** Low - a request for `k` can be attached to the buffer of `k/1` (same endpoint, method, role, query).
- **Description:** `strncmp(href, buffer->href, href_len)` compares only the length of the incoming href, so a shorter href matches a longer stored one.
- **Evidence:** code reading. A test: buffer for `p/1`, find with `p` -> must return NULL.
- **Expected:** compare the lengths too (`href_len == oc_string_len(buffer->href)`).

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

## F-050: the process event queue grows without limit and is used after shutdown - OPEN

- **Files:** `util/oc_process.c`, `oc_process_post()`, `oc_process_shutdown()`; `api/oc_buffer.c` (ERR_FULL unref paths).
- **Severity:** Low - a flood of events grows the queue until `oc_abort`; a post after shutdown uses freed memory.
- **Description:** `oc_process_post()` doubles the queue with `realloc` and never returns `OC_PROCESS_ERR_FULL`, so the callers' ERR_FULL paths that unref the message are dead code. `oc_process_shutdown()` frees `events` without setting it to NULL or resetting the counters.
- **Evidence:** code reading.
- **Expected:** cap the queue (return ERR_FULL at the cap) and reset `events`/counters in `oc_process_shutdown()`.

## F-051: a failed block-wise buffer allocation stops notifying all remaining observers - OPEN

- **Files:** `messaging/coap/observe.c`, `coap_notify_observers()`, block-wise branch: the `break` taken when `oc_blockwise_alloc_response_buffer()` returns NULL.
- **Severity:** Low - under heap pressure the observers after the current one silently miss this notification (no error, no retry).
- **Description:** when a notification is larger than the observer's Block2 size and no response buffer can be allocated, the `break` leaves the whole `while (obs)` loop instead of skipping only this observer. Related: F-032 (the `continue` in the same branch).
- **Evidence:** code reading. A test: two observers of one resource, Block2 64, a 100 byte value, a failing `malloc` wrap for the first buffer only -> the second observer must still get its first block.
- **Expected:** skip only the current observer (`obs = obs->next; continue;`).

## F-052: one observer's deregister stops the periodic notifications for all observers of the resource - OPEN

- **Files:** `api/oc_ri.c`, GET with Observe = 1 (deregister) on an `OC_PERIODIC` resource, `remove_periodic_observe_callback()`.
- **Severity:** Low - the remaining observers of a periodic resource get no more periodic notifications.
- **Description:** the periodic callback exists once per resource, not per observer. When `coap_observe_handler()` removes one observer, the handler calls `remove_periodic_observe_callback(resource)` without checking whether other observers are left. Related: F-036.
- **Evidence:** code reading. A test: two clients observe a periodic resource, one deregisters, advance the clock past the period -> the other must still be notified.
- **Expected:** remove the periodic callback on deregister only when `num_observers` of the resource has dropped to 0.

## F-053: a rejected CON separate response is answered with ACK 4.13 instead of a RST - OPEN

- **Files:** `messaging/coap/engine.c`, `coap_receive()`, parser-error tail (`coap_send_response_with_empty_application_payload()` with `echo_ctx` type `is_con ? ACK : NON`).
- **Severity:** Low - protocol deviation; the peer's separate response is acknowledged with an error code it does not expect for a response.
- **Description:** a CON separate response the device receives as client (e.g. payload `>= OC_MAX_APP_DATA_SIZE`, rejected by the parser with 4.13) gets the generic parser-error reply: an ACK carrying 4.13. RFC 7252 4.2: rejecting a Confirmable message is done by sending a matching Reset (empty, same MID) and otherwise ignoring it. The parser-error reply only builds ACK or NON messages, not a RST. Remainder of F-030 (rejected ACK, RST and NON responses are now ignored).
- **Evidence:** code reading. A test: like `CoapReceivePayload.ClientResponseOfMaxAppDataSizeIsRejectedBeforeTheCallback`, but the response is a CON separate response -> expect one RST, code 0.00, the response's MID, no token.
- **Expected:** for a rejected CON separate response send an empty RST with its MID instead of the ACK 4.13.
