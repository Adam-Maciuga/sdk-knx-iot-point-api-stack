# Release Notes — knx-iot-point-api-stack

> **Important:** Stack and demos version numbers are kept in sync. For every
> major stack version `1.x`, use the corresponding `knx-iot-point-api-demos`
> version `1.x`. 
> Minor Versions `1.x.Y` are independent and not synced.

## 1.1.0 (2026-07-17)

Changes since the last release (tag `v1.0.0`, 2026-03-02) up to 2026-07-16.

> **Note:** The certified API version remains **1.0.0**. CoAP Observe
> support is new in this release but not yet covered by EITT conformance
> tests v40.
> The API version will move to 1.1.0 once Observe conformance tests
> exist and certification is repeated.

### Repository Restructuring

- The project is now split into two independent repos: **knx-iot-point-api-stack** (core library) and **knx-iot-point-api-demos** (demo/Zephyr apps)
- The stack is now designed to be consumed as a standalone library (port sources, cleaned-up public headers) rather than bundled with demo apps
- Switched third-party dependencies (mbedTLS, mDNS, etc.) from git submodules to CMake FetchContent

### Protocol / Spec Compliance

- CoAP Observe (RFC 7641) support for `/k` and `/p` resources
- CoAP duplicate-response handling per RFC 7252 §4.5 (response cache with capped retransmissions — also closes a DDoS vector)
- OSCORE echo/replay defense reworked for S-mode (unified ring buffer, spec-correct unicast/multicast delay handling)
- SPAKE2+ (PASE) handshake hardened with an explicit state machine rejecting malformed/out-of-order/concurrent requests
- SPAKE2+ switched to offline precalculated registration records (RFC 9383 §3.2) — no more runtime password handling
- IPv6 network-scope awareness (detects real ULA/global connectivity; RFC 6724-compliant source-address selection)
- Multicast scope/address corrections per KNX spec (default scope now "5"; proper egress-interface pinning)
- Custom, platform-independent DNS-SD/mDNS implementation, with correct port/goodbye handling
- Group Value Response conformance fix (Write+Update flags both required per spec table)

### Security Fixes

- Fixed stack buffer overflow in device hostname handling
- Fixed buffer overflow risk on max CoAP payload length
- Fixed multiple heap memory leaks
- Fixed overlapping-memcpy risk in CoAP option serialization (now uses memmove)
- DDoS mitigation via capped response-cache retransmissions

### Build System / Crypto

- Migrated crypto backend from Mbed TLS 3.6 to **Mbed TLS 4.1 / TF-PSA-Crypto 1.0** (PSA Crypto API throughout)
- Switched to dynamic-memory-allocation-only mode; removed the static memory pool allocator entirely
- Zephyr port upgraded from 4.3 → 4.4
- Comprehensive automated test suite added: Google Test unit tests (1000+ tests) and Python/pytest runtime conformance tests over real CoAP/OSCORE
- Removed legacy OCF-derived code (OC_CLOUD, multi-device discovery, etc.)

### Platform

- New Zephyr port with correct IPv6/multicast handling
- New KNX-IoT Zephyr shell (incl. heap diagnostics command)
- NVS storage optimized for faster boot/factory-reset
- Fixed a ~6s SPAKE2+ handshake stall on Zephyr; fixed RX-thread crashes from socket/poll races
- Fixed DNS-SD advertising the wrong (hardcoded) unicast port on Zephyr Wi-Fi, causing EITT 5.1.2.x CoAP timeouts
- Fixed event loop not servicing new timers promptly on ports that sleep until signaled (Zephyr)
- Fixed a clock tick-rate mismatch that stretched a 2s SWU timer to 20s on frdm_rw612

### New Features (Demo-Facing)

- New Zephyr demo apps built on this stack: LSSB, LSAB, and EITT (certification test app) — see the [knx-iot-point-api-demos](https://gitlab.knx.org/public-projects/knx-iot-point-api/knx-iot-point-api-demos) release notes for details
- EITT REST test-control endpoints (`/test/restart`, `/test/factory-reset`, `/test/trigger`) for automated conformance testing
- MCUboot-based firmware update support + SWU application

### Breaking Changes

- `OC_DYNAMIC_ALLOCATION`, `OC_REPLAY_PROTECTION`, `OC_DNS_SD`, `OC_SPAKE` compile options all removed — these features are now unconditionally enabled
- `OC_SENDER_MULTICAST_SCOPE` renamed to `KNX_MULTICAST_SCOPE` (default changed to "5")
- `util/oc_memb` module removed — callers must use calloc/free directly
- Legacy platform/device APIs removed from `oc_api.h`

## 1.0.0 (2026-03-02)

This is Version 1.0.0 of the IoT Point API Stack.
It is fully certified against EITT 4.4.0088 conformance Tests V39, KNX System Conformance Testing v1.1.1
