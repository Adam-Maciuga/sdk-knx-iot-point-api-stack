---
description: "Hard rules for writing EITT-compliant runtime conformance tests. Always apply when working on tests/runtime/ files."
applyTo: "tests/runtime/**"
---

# Hard Rules for Runtime Tests

- Runtime tests MUST 100% replicate the EITT tests. No workarounds, no simplifications.
- The EITT virtual GUI (`eitt_virtual_gui`) works against the same project, so our tests must work the same way.
- Do NOT make assumptions about stack behavior. Verify against the EITT project or source code.
- EITT project location: `tests/EITT_REFERENCE_PROJECT/EittProject.xml` (in the stack repo).
- Use the MCP tool `mcp_knx-iot-rag_search_knx_stack` (test-spec / KNX IoT RAG) as an additional reference.
- EITT is generally correct. If a discrepancy is found between the test spec and the EITT project, stop and ask the user — do NOT resolve it autonomously.
- Our runtime tests need to mimic the EITT as closely as possible (same parameters, same sequence, same payloads).
- Before declaring any test category complete: reiterate the EITT-vs-ours comparison multiple times until 100% certain nothing is missing. Then reiterate once more.
- REITERATION RULE: When reiterating, NEVER use own memory/notes/summaries as source of truth. Always re-read the EITT XML and/or query the test-spec MCP tool fresh. Own memory can be wrong — the EITT project and test-spec MCP are the only sources of truth.
- COVERAGE CHECK DIRECTION: Always check coverage EITT→ours (iterate EITT test IDs, verify each exists in our code), never ours→EITT. Scanning our files misses what we forgot to write.
- PROTOCOL CHECK: For each EITT test, check what element types it uses (CoAPMessage, DnsMessage, etc.). If our test uses a different protocol than the EITT, it is NOT a valid replication — mark it as an approximation and flag it.
- NO FABRICATED COUNTS: Never claim "X/Y" unless both X and Y are independently derived from the EITT XML in the current session. Do not infer the denominator from the numerator.
- LABEL APPROXIMATIONS: If a test cannot exactly replicate the EITT (e.g., mDNS not available), mark it clearly with a comment like "# APPROXIMATION: EITT uses mDNS, we use CoAP" and do not count it as EITT-matched.
- NO STACK CODE CHANGES: Never modify stack source code (C files under api/, security/, messaging/, port/, etc.) without explicit human approval. Only modify test files autonomously.
- UNCHECKED TELEGRAMS: EITT telegrams with Active="N" (unchecked checkbox) can be ignored — they are skipped during EITT execution and do not need runtime test coverage.
- TRACE BUFFER IS GROUND TRUTH: Each EITT section has its own trace buffer file (e.g., `5_4_1_trace_buffer.xml`). Always analyze the trace buffer for the target section BEFORE writing tests — it shows exact CBOR payloads, OSCORE credentials, GO/RCP/PUB table configs, and expected response codes.
- DISABLED TEST FILES ARE NOT REFERENCE: Files named `disabled_test_*.py` are NOT EITT-compliant. Never use them as templates. Only the EITT trace buffers and EittProject.xml are valid references.
- MULTICAST SSN SYNC: Multicast OSCORE requests require SSN synchronization (echo challenge). Send SSN=1 first, extract echo from 4.01 response, resend SSN=2 with echo. Only then are subsequent SSNs accepted.
- GROUP COMM PROVISIONING: S-mode group communication tests require provisioning GO table (/fp/g), publisher table (/fp/p) for receiving, recipient table (/fp/r) for sending, plus a multicast AT with context_id. All must be installed in LOADING state, then set to LOADED.
- TRIGGER ENDPOINT: POST /test/trigger with {11: "/p/N"} triggers the DUT to send s-mode. Optional {1: bool} sets explicit value. Without it, the value toggles. Use with listen_multicast() via threading to capture DUT output.
- CFLAG VALUES: GO table cflags: 0x10=write(w), 0x18=write+read(wr), 0x20=init(i), 0x40=transmit(t), 0x80=update(u), 0x90=write+update(w+u). These are bitmasks.
- TOKEN FILTERING: CoAP receive loops MUST filter by expected token. The DUT sends unsolicited CON POST /k answers on the same socket — without token filtering, they get misinterpreted as responses to unrelated requests, causing InvalidTag or state corruption.
- SOCKET DRAINING: After factory reset or test failure, always call `drain_socket()` before starting SPAKE2+ — stale DUT retransmissions sitting in the socket will corrupt the handshake (e.g. CBOR decode of empty payload).
- MULTICAST SELF-FILTERING: `listen_multicast()` captures ALL messages on the multicast group, including our own sends. Always filter results by `addr[0] == coap.host` (DUT address) before processing. Without this, decryption fails because our messages use different OSCORE keys.
- REPROVISION EVERY TEST CLASS: Each test class setup MUST call `_reprovision()` (factory reset + SPAKE2+ + AT provisioning). Without it, anti-replay windows from previous tests persist and cause 4.01 rejections for reused sender_ids starting at SSN=0.
- NO S-MODE WRITES AS SETUP: Do NOT use `/k` s-mode writes to set datapoint values as test preconditions — they trigger DUT answer/forward messages that pollute the socket. Use direct `_set_datapoint()` on `/p/N` instead.
- TEST LOCALLY BEFORE CI: Always run `pytest` locally before pushing to CI. Multicast tests will be skipped on Windows (no veth pair) but unicast tests catch most regressions. Use `-x` for fast failure feedback.
- MEMORY SYNC: When rules in this file change, always update the corresponding repo memory (`/memories/repo/`) to stay in sync. Both must reflect the same knowledge — update both or neither.
