# Runtime Test Coverage Tracking

> **Source of truth** for mapping EITT spec test IDs to our runtime tests.
> Authoritative EITT ID list extracted from `tests/EITT_REFERENCE_PROJECT/EittProject.xml`.
> Last updated: 2026-05-14
> Total EITT test IDs: 179
> Covered (DONE): 176
> Blocked: 3
> TODO: 0

## Rules

1. **This file is the single source of truth.** When checking gaps, read THIS file.
2. **Every EITT test ID must have a row.** The 179 IDs below come from EittProject.xml.
3. **Status values:** `DONE`, `TODO`, `BLOCKED:<reason>`
4. **After writing a test**, update this file in the same commit.

## Legend

- **DONE** — Fully covered by one or more test functions
- **TODO** — Not yet implemented, but feasible
- **BLOCKED:timer** — Requires SPAKE2+ handover timing not implemented in stack

---

## 5.1 Discovery (42 IDs — 42 DONE)

| Spec ID | Description | Status | Test File | Notes |
|---------|-------------|--------|-----------|-------|
| 5.1.1.1 | Multicast Discovery via /.well-known/core | DONE | test_5_1_discovery_multicast.py | Requires DEVICE_IFACE (veth) |
| 5.1.1.2 | Unicast Discovery via /.well-known/core | DONE | test_5_1_discovery.py | |
| 5.1.1.3 | Multicast Discovery with filters rt, if & ep | DONE | test_5_1_discovery_multicast.py | 5 sub-steps; requires DEVICE_IFACE |
| 5.1.1.3b | Invalid Multicast Discovery Query | DONE | test_5_1_discovery_multicast.py | Requires DEVICE_IFACE |
| 5.1.1.3c | Invalid Multicast Discovery Query for Interface | DONE | test_5_1_discovery_multicast.py | Requires DEVICE_IFACE |
| 5.1.1.3d | Invalid Multicast Discovery Query for resource type | DONE | test_5_1_discovery_multicast.py | Requires DEVICE_IFACE |
| 5.1.1.3e | Multicast Discovery Query with If Wildcard | DONE | test_5_1_discovery_multicast.py | Requires DEVICE_IFACE |
| 5.1.1.4 | Unicast Discovery with filters rt, if & ep | DONE | test_5_1_discovery.py | 5 sub-steps |
| 5.1.1.4b | Invalid Unicast Discovery Query | DONE | test_5_1_discovery.py | |
| 5.1.1.4c | Invalid Unicast Discovery Query for Interface | DONE | test_5_1_discovery.py | |
| 5.1.1.4d | Invalid Unicast Discovery Query for resource type | DONE | test_5_1_discovery.py | |
| 5.1.1.4e | Unicast Discovery Query for If Wildcard | DONE | test_5_1_discovery.py | |
| 5.1.1.5 | ?if=urn:knx:if.pm query, Multicast, device in PM | DONE | test_5_1_discovery_multicast.py | Requires DEVICE_IFACE |
| 5.1.1.5b | ?if=urn:knx:if.pm query, Multicast, not in PM ignores | DONE | test_5_1_discovery_multicast.py | Requires DEVICE_IFACE |
| 5.1.1.5c | ?if=urn:knx:if.pm query, Unicast, device in PM responds | DONE | test_5_1_discovery_extended.py | |
| 5.1.1.5d | ?if=urn:knx:if.pm query, Unicast, not in PM → error | DONE | test_5_1_discovery_extended.py | |
| 5.1.1.6 | ?if=urn:knx:if.pm&ep=knx://sn.{sn}, Multicast, PM found | DONE | test_5_1_discovery_multicast.py | Requires DEVICE_IFACE |
| 5.1.1.6b | ?if=urn:knx:if.pm&ep=knx://sn.{sn}, Multicast, not in PM ignores | DONE | test_5_1_discovery_multicast.py | Requires DEVICE_IFACE |
| 5.1.1.6c | ?if=urn:knx:if.pm&ep=knx://sn.{sn}, Multicast, different SN ignores | DONE | test_5_1_discovery_multicast.py | Requires DEVICE_IFACE |
| 5.1.1.6d | ?if=urn:knx:if.pm&ep=knx://sn.{sn}, Unicast, device in PM | DONE | test_5_1_discovery_extended.py | |
| 5.1.1.6e | ?if=urn:knx:if.pm&ep=knx://sn.{sn}, Unicast, not in PM → error | DONE | test_5_1_discovery_extended.py | |
| 5.1.1.6f | ?if=urn:knx:if.pm&ep=knx://sn.{sn}, Unicast, different SN | DONE | test_5_1_discovery_extended.py | |
| 5.1.1.7 | ?d=urn:knx:g.s. query, Multicast, GA → single GO | DONE | test_5_1_discovery_extended.py | |
| 5.1.1.7b | ?d=urn:knx:g.s. query, Multicast, GA → multiple GO | DONE | test_5_1_discovery_extended.py | |
| 5.1.1.7c | ?d=urn:knx:g.s. query, Multicast, GA → no GO | DONE | test_5_1_discovery_extended.py | |
| 5.1.1.7d | ?d=urn:knx:g.s. query, Multicast, multiple GAs → one GO | DONE | test_5_1_discovery_extended.py | |
| 5.1.1.7e | ?d=urn:knx:g.s. query, Unicast, GA → single GO | DONE | test_5_1_discovery_extended.py | |
| 5.1.1.7f | ?d=urn:knx:g.s. query, Unicast, GA → multiple GO | DONE | test_5_1_discovery_extended.py | |
| 5.1.1.7g | ?d=urn:knx:g.s. query, Unicast, GA → no GO | DONE | test_5_1_discovery_extended.py | |
| 5.1.1.7h | ?d=urn:knx:g.s. query, Unicast, multiple GAs → one GO | DONE | test_5_1_discovery_extended.py | |
| 5.1.1.8 | Invalid ?d=urn:knx:g.s.* wildcard rejected | DONE | test_5_1_discovery_extended.py | |
| 5.1.1.9a | ?ep=knx://ia.{iid}.{ia}, Multicast, correct IID+IA | DONE | test_5_1_discovery_multicast.py | Requires DEVICE_IFACE |
| 5.1.1.9b | ?ep=knx://ia.{iid}.{ia}, Multicast, wrong IID | DONE | test_5_1_discovery_multicast.py | Requires DEVICE_IFACE |
| 5.1.1.9c | ?ep=knx://ia.{iid}.{ia}, Multicast, wrong IA | DONE | test_5_1_discovery_multicast.py | Requires DEVICE_IFACE |
| 5.1.2.1 | Discovery using SN for unconfigured device | DONE | test_5_1_discovery_mdns.py | mDNS browse + SRV + AAAA + CoAP |
| 5.1.2.2 | Discovery using SN for configured device | DONE | test_5_1_discovery_mdns.py | |
| 5.1.2.3 | Discovery using IA for configured device | DONE | test_5_1_discovery_mdns.py | IA subtype discovery |
| 5.1.2.3a | Discovery for IA=0 for configured device | DONE | test_5_1_discovery_mdns.py | |
| 5.1.2.3b | Discovery using IA for unconfigured device | DONE | test_5_1_discovery_mdns.py | |
| 5.1.2.4 | Discovery using programming mode | DONE | test_5_1_discovery_mdns.py | PM subtype enabled/disabled |
| 5.1.2.5a | Unsolicited mDNS responses on PM change | DONE | test_5_1_discovery_mdns.py | |
| 5.1.2.5b | Unsolicited mDNS responses on IA change | DONE | test_5_1_discovery_mdns.py | |

## 5.2 Device Resources (19 IDs — 19 DONE)

| Spec ID | Description | Status | Test File | Notes |
|---------|-------------|--------|-----------|-------|
| 5.2.1.1 | Reading API version and base path | DONE | test_5_2_wellknown_knx.py | |
| 5.2.2.1 | Reset device to factory default (erase-code 2) | DONE | test_5_2_reset.py | |
| 5.2.2.2 | Reset device without network info (erase-code 7) | DONE | test_5_2_reset.py | |
| 5.2.2.3 | Restarting device | DONE | test_5_2_wellknown_knx.py | |
| 5.2.3.1 | Setting Installation ID and Individual Address | DONE | test_5_2_wellknown_knx.py | |
| 5.2.3.1b | Invalid GET /.well-known/knx/ia → fails | DONE | test_5_2_wellknown_knx.py | |
| 5.2.3.2 | Setting Fabric ID alongside IID and IA | DONE | test_5_2_wellknown_knx.py | |
| 5.2.4.1 | Read fingerprint in loaded state | DONE | test_5_2_fingerprint.py | |
| 5.2.4.1b | Invalid POST to fingerprint | DONE | test_5_2_fingerprint.py | |
| 5.2.4.1c | Invalid PUT to fingerprint | DONE | test_5_2_fingerprint.py | |
| 5.2.4.2 | Read fingerprint in loading state → 5.03 | DONE | test_5_2_fingerprint.py | |
| 5.2.4.3 | Read fingerprint in unloaded state → 5.03 | DONE | test_5_2_fingerprint.py | |
| 5.2.5.1 | Read list of device data property-ids | DONE | test_5_2_device_resources.py | |
| 5.2.6.1 | Read specific device data for various property-ids | DONE | test_5_2_device_resources.py | |
| 5.2.7.1 | Write specific device data for various property-ids | DONE | test_5_2_device_resources.py | |
| 5.2.7.1b | Invalid PUT on read-only dev resource | DONE | test_5_2_device_resources.py | |
| 5.2.8.1 | Read list of software update property-ids | DONE | test_5_2_swu.py | |
| 5.2.9.1 | Read specific SWU data during PUSH update | DONE | test_5_2_swu.py | |
| 5.2.10.1b | Invalid PUT on read-only SWU resource | DONE | test_5_2_swu.py | |

## 5.3 Security (44 IDs — 41 DONE, 3 BLOCKED)

| Spec ID | Description | Status | Test File | Notes |
|---------|-------------|--------|-----------|-------|
| 5.3.1.1 | SPAKE2+ authenticated key exchange sequence | DONE | test_5_3_security.py | Full 3-step handshake |
| 5.3.1.2 | Invalid SPAKE2+ with wrong password | DONE | test_5_3_security.py | |
| 5.3.1.2b | Invalid SPAKE2+ with incorrect confirmP | DONE | test_5_3_security.py | |
| 5.3.1.3 | SPAKE2+ after various reset types | DONE | test_5_3_security.py | |
| 5.3.1.4a | Secondary SPAKE2+ within default time limit | DONE | test_5_3_security.py | Tests current stack behavior |
| 5.3.1.4b | Secondary SPAKE2+ after default time limit expires | BLOCKED:timer | | Requires SPAKE2+ handover timing |
| 5.3.1.4c | Secondary SPAKE2+ within specified time limit | BLOCKED:timer | | Requires SPAKE2+ handover timing |
| 5.3.1.4d | Secondary SPAKE2+ after specified time limit expires | BLOCKED:timer | | Requires SPAKE2+ handover timing |
| 5.3.4.1 | Read list of auth resources | DONE | test_5_3_security.py | |
| 5.3.8.1 | Write entry to access token list | DONE | test_5_3_security.py | |
| 5.3.8.1b | Invalid POST AT without security fails | DONE | test_5_3_security.py | |
| 5.3.8.1c | Invalid PUT AT without security fails | DONE | test_5_3_security.py | |
| 5.3.8.1d | Invalid POST AT without if.sec scope fails | DONE | test_5_3_security.py | |
| 5.3.8.1e | Invalid PUT AT without if.sec scope fails | DONE | test_5_3_security.py | |
| 5.3.8.1f | Invalid PUT AT with if.sec scope still fails | DONE | test_5_3_security.py | |
| 5.3.8.2 | Invalid mixed group+config scopes | DONE | test_5_3_security.py | |
| 5.3.8.3 | Write multiple group address entries | DONE | test_5_3_security.py | |
| 5.3.8.4 | Overwrite existing access token | DONE | test_5_3_security.py | |
| 5.3.8.5 | Create multiple AT entries in single write | DONE | test_5_3_security.py | |
| 5.3.8.6 | Update multiple AT entries | DONE | test_5_3_security.py | |
| 5.3.9.1 | Read list of access tokens | DONE | test_5_3_security.py | |
| 5.3.9.3a | Read AT list without security (unconfigured) | DONE | test_5_3_security.py | |
| 5.3.9.3b | Read AT list without security (configured) | DONE | test_5_3_security.py | |
| 5.3.10.1 | Read specific AT by token id | DONE | test_5_3_security.py | |
| 5.3.10.2a | Read AT without security → fails | DONE | test_5_3_security.py | |
| 5.3.10.2b | Read AT without if.sec scope → fails | DONE | test_5_3_security.py | |
| 5.3.11.1 | Delete specific AT by token id | DONE | test_5_3_security.py | |
| 5.3.12.1 | Read list of OSCORE related resources | DONE | test_5_3_security.py | |
| 5.3.13.1 | Read replay window size | DONE | test_5_3_security.py | |
| 5.3.15.1 | Read OSCORE delay parameter | DONE | test_5_3_security.py | |
| 5.3.16.1 | Write OSCORE delay parameter | DONE | test_5_3_security.py | |
| 5.3.17.1 | Default Anti-Replay Window | DONE | test_5_3_security.py | |
| 5.3.17.3 | Group Message Anti-Replay Window | DONE | test_5_3_security.py | |
| 5.3.17.4 | Synchronization Delay Jitter and Echo Option | DONE | test_5_3_security.py | |
| 5.3.17.5 | Invalid Echo Option Reply for Synchronization | DONE | test_5_3_security.py | |
| 5.3.17.6 | Sending Device Responds to Echo Option Challenge | DONE | test_5_3_security.py | |
| 5.3.17.7 | Echo Option Challenge on First Unicast Request | DONE | test_5_3_security.py | |
| 5.3.17.8 | Echo Option Challenge on PASE Token Request | DONE | test_5_3_security.py | |
| 5.3.18.1 | Incorrect authentication tag refused | DONE | test_5_3_security.py | |
| 5.3.19.1 | OSCORE Encryption with default values | DONE | test_5_3_security.py | 16+32 byte master secret |
| 5.3.19.2 | Master Salt applied correctly | DONE | test_5_3_security.py | |
| 5.3.19.5 | Master Salt + Context ID for Group Messages | DONE | test_5_3_security.py | |
| 5.3.19.6 | Master Salt + Context ID for Config Messages | DONE | test_5_3_security.py | |
| 5.3.20.1 | Access control via interface scopes on AT | DONE | test_5_3_security.py | 5 sub-tests |

## 5.4 Group Communication — S-Mode (16 IDs — 16 DONE)

| Spec ID | Description | Status | Test File | Notes |
|---------|-------------|--------|-----------|-------|
| 5.4.1.1 | Unicast write group message to device (POST /k) | DONE | test_5_4_group_comm.py | |
| 5.4.1.1b | Unicast write with unauthorized GA → 4.03 | DONE | test_5_4_group_comm.py | |
| 5.4.1.2 | Multicast write group message | DONE | test_5_4_group_comm.py | Requires DEVICE_IFACE |
| 5.4.1.3 | Multicast read group message → response | DONE | test_5_4_group_comm.py | Requires DEVICE_IFACE |
| 5.4.1.4 | Unicast read group message → response | DONE | test_5_4_group_comm.py | |
| 5.4.1.5 | Unicast write → update multiple GOs | DONE | test_5_4_group_comm.py | |
| 5.4.1.6 | Trigger device multicast write | DONE | test_5_4_group_comm.py | Deferred trigger via /test/trigger |
| 5.4.1.7 | Trigger device multicast write (first GA) | DONE | test_5_4_group_comm.py | Deferred trigger |
| 5.4.1.8 | Trigger in loading state → no send | DONE | test_5_4_group_comm.py | |
| 5.4.1.9 | Init flag → s-mode on startup | DONE | test_5_4_group_comm.py | Uses /test/restart |
| 5.4.1.10 | Multicast response → device update | DONE | test_5_4_group_comm.py | Requires DEVICE_IFACE |
| 5.4.1.11 | Multicast write/response ignored in loading | DONE | test_5_4_group_comm.py | Requires DEVICE_IFACE |
| 5.4.1.12 | Updating own GOs for outgoing messages | DONE | test_5_4_group_comm.py | |
| 5.4.1.13 | Receiving support for long Group Addresses | DONE | test_5_4_group_comm.py | 4 sub-tests: ga=0,256,65535,max |
| 5.4.1.14 | Sending support for long Group Addresses | DONE | test_5_4_group_comm.py | Deferred trigger |
| 5.4.1.15 | Trigger sending Confirmable messages (unicast) | DONE | test_5_4_group_comm.py | Discovery responder resolves IA→IPv6 |

## 5.5 Function Point Tables (35 IDs — 35 DONE)

| Spec ID | Description | Status | Test File | Notes |
|---------|-------------|--------|-----------|-------|
| 5.5.1.1 | Write single GO with Read flag | DONE | test_5_5_fp_tables.py | |
| 5.5.1.2 | Write single GO with Multiple Flags (Actuator) | DONE | test_5_5_fp_tables.py | |
| 5.5.1.3 | Write single GO with Multiple Flags (Sensor) | DONE | test_5_5_fp_tables.py | |
| 5.5.1.4 | Write multiple GOs (Actuator) | DONE | test_5_5_fp_tables.py | |
| 5.5.1.5 | Write multiple GOs (Sensor) | DONE | test_5_5_fp_tables.py | |
| 5.5.1.6 | Update single GO (Actuator) | DONE | test_5_5_fp_tables.py | |
| 5.5.1.7 | Update single GO (Sensor) | DONE | test_5_5_fp_tables.py | |
| 5.5.1.8 | Delete GO via POST empty element | DONE | test_5_5_fp_tables.py | |
| 5.5.1.9 | Invalid write GO with no GAs | DONE | test_5_5_fp_tables.py | |
| 5.5.1.9b | Invalid update GO with no GAs | DONE | test_5_5_fp_tables.py | |
| 5.5.2.1 | Read list of Group Object Identifiers | DONE | test_5_5_fp_tables.py | |
| 5.5.3.1 | Read GO via its identifier | DONE | test_5_5_fp_tables.py | |
| 5.5.4.1 | Delete GO via its identifier | DONE | test_5_5_fp_tables.py | |
| 5.5.5.1 | Write single recipient (multicast, grpid) | DONE | test_5_5_fp_tables.py | |
| 5.5.5.3 | Write single recipient (unicast, IA) | DONE | test_5_5_fp_tables.py | |
| 5.5.5.4 | Write multiple recipients (MC + UC) | DONE | test_5_5_fp_tables.py | |
| 5.5.5.5 | Update multiple recipients | DONE | test_5_5_fp_tables.py | |
| 5.5.5.5a | Update (partial) multiple recipients | DONE | test_5_5_fp_tables.py | |
| 5.5.5.5b | Invalid update non-existing recipient | DONE | test_5_5_fp_tables.py | |
| 5.5.5.6 | Delete recipient via POST empty element | DONE | test_5_5_fp_tables.py | |
| 5.5.5.7a | Write entries with empty GA list | DONE | test_5_5_fp_tables.py | |
| 5.5.5.8a | Write split entry with up to 20 GAs | DONE | test_5_5_fp_tables.py | |
| 5.5.6.1 | Read list of recipient table entry IDs | DONE | test_5_5_fp_tables.py | |
| 5.5.7.1 | Read single recipient entry | DONE | test_5_5_fp_tables.py | |
| 5.5.8.1 | Delete recipient entry via identifier | DONE | test_5_5_fp_tables.py | |
| 5.5.9.1 | Write single publisher (multicast, grpid) | DONE | test_5_5_fp_tables.py | |
| 5.5.9.3 | Write single publisher (unicast, IA) | DONE | test_5_5_fp_tables.py | |
| 5.5.9.4 | Write multiple publishers | DONE | test_5_5_fp_tables.py | |
| 5.5.9.6 | Delete publisher via POST empty element | DONE | test_5_5_fp_tables.py | |
| 5.5.9.7a | Write publisher with empty GA list | DONE | test_5_5_fp_tables.py | |
| 5.5.9.8a | Write split publisher entry (20 GAs) | DONE | test_5_5_fp_tables.py | |
| 5.5.10.1 | Read list of publisher table entry IDs | DONE | test_5_5_fp_tables.py | |
| 5.5.11.1 | Read single publisher entry | DONE | test_5_5_fp_tables.py | |
| 5.5.12.1 | Delete publisher entry via identifier | DONE | test_5_5_fp_tables.py | |
| 5.5.12.2 | Invalid delete non-existing publisher | DONE | test_5_5_fp_tables.py | |

## 5.6 Application Program (7 IDs — 7 DONE)

| Spec ID | Description | Status | Test File | Notes |
|---------|-------------|--------|-----------|-------|
| 5.6.1.1 | LSM startLoading transition | DONE | test_5_6_app_program.py | |
| 5.6.1.2 | LSM loadComplete transition | DONE | test_5_6_app_program.py | |
| 5.6.1.3 | LSM unload transition | DONE | test_5_6_app_program.py | |
| 5.6.1.4 | LSM state persists over reboot | DONE | test_5_6_app_program.py | 3 sub-tests |
| 5.6.2.1 | GET /a/lsm returns status | DONE | test_5_6_app_program.py | |
| 5.6.3.1 | Read list of application program resources | DONE | test_5_6_app_program.py | |
| 5.6.4.1 | Write and Read /ap/pv (program version) | DONE | test_5_6_app_program.py | |

## 5.7 Functional Blocks (2 IDs — 2 DONE)

| Spec ID | Description | Status | Test File | Notes |
|---------|-------------|--------|-----------|-------|
| 5.7.1.1 | Read list of functional block instances | DONE | test_5_7_functional_blocks.py | |
| 5.7.2.1 | Read list of datapoints of a FB | DONE | test_5_7_functional_blocks.py | |

## 5.8 Parameters & Diagnostics (5 IDs — 5 DONE)

| Spec ID | Description | Status | Test File | Notes |
|---------|-------------|--------|-----------|-------|
| 5.8.1.1 | Write single parameter or diagnostic property | DONE | test_5_8_parameters.py | POST /p with [{1: val, 11: href}] |
| 5.8.2.1 | Read list of parameter/diagnostic paths | DONE | test_5_8_parameters.py | |
| 5.8.3.1 | Read single parameter/diagnostic property | DONE | test_5_8_parameters.py | |
| 5.8.3.2 | Read meta data for parameter or diagnostic property | DONE | test_5_8_parameters.py | GET /p/p1?m=* and ?m=id and ?m=value |
| 5.8.4.1 | Write single parameter via PUT | DONE | test_5_8_parameters.py | |

## 5.10 Generic Tests (9 IDs — 9 DONE)

| Spec ID | Description | Status | Test File | Notes |
|---------|-------------|--------|-----------|-------|
| 5.10.1.1 | Pagination via GET specifying Page Number pn | DONE | test_5_10_generic.py | |
| 5.10.1.2 | Pagination via GET specifying pn and Page Size ps | DONE | test_5_10_generic.py | |
| 5.10.1.4 | List Metadata Query Parameter l | DONE | test_5_10_generic.py | GET ?l=total on /fp/g, /p, /auth/at |
| 5.10.1.5 | Invalid List Metadata Query Parameter l | DONE | test_5_10_generic.py | |
| 5.10.1.6 | Pagination with Link to next Page | DONE | test_5_10_generic.py | Follow p.next links with ps=2 |
| 5.10.4.1 | Read DPT associated with Group Object | DONE | test_5_10_generic.py | |
| 5.10.5.1 | Accept Option omitted → default | DONE | test_5_10_generic.py | |
| 5.10.5.2 | Content-Format Option omitted → default | DONE | test_5_10_generic.py | |
| 5.10.5.3 | Unknown Critical Option → Bad Option | DONE | test_5_10_generic.py | |

---

## Bonus Tests (not in EITT)

These test functions exist in our suite but do not correspond to any EITT test ID.
They provide additional coverage beyond the certification scope.

| Test Function | File | Notes |
|---------------|------|-------|
| test_5_5_1_6b_update_nonexisting_go | test_5_5_fp_tables.py | Stack creates new entry (2.01) |
| test_5_5_9_5_update_multiple_publishers | test_5_5_fp_tables.py | Additional publisher update test |
| test_5_5_9_5b_update_nonexisting_publisher | test_5_5_fp_tables.py | Stack creates new entry (2.01) |

---

## Summary

| Category | Total | DONE | TODO | BLOCKED |
|----------|-------|------|------|---------|
| 5.1 Discovery | 42 | 42 | 0 | 0 |
| 5.2 Device Resources | 19 | 19 | 0 | 0 |
| 5.3 Security | 44 | 41 | 0 | 3 |
| 5.4 Group Communication | 16 | 16 | 0 | 0 |
| 5.5 Function Point Tables | 35 | 35 | 0 | 0 |
| 5.6 Application Program | 7 | 7 | 0 | 0 |
| 5.7 Functional Blocks | 2 | 2 | 0 | 0 |
| 5.8 Parameters | 5 | 5 | 0 | 0 |
| 5.10 Generic | 9 | 9 | 0 | 0 |
| **TOTAL** | **179** | **176** | **0** | **3** |

**Coverage: 169/179 = 94.4% (DONE)**
**Achievable: 176/179 = 98.3% (excluding BLOCKED)**
**Remaining TODO: 7 (5.5.9.3, 5.8.1.1, 5.8.3.2, 5.10.1.1, 5.10.1.2, 5.10.1.4, 5.10.1.6)**
**Remaining BLOCKED: 3 (5.3.1.4b/c/d — SPAKE2+ handover timing)**
