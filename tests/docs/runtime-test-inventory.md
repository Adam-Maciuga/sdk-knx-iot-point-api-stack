> **Provenance.** Generated 2026-09-10 by comparing upstream `work_in_progress`
> (`b6271257`) against this branch at its PRE-rebase tip `8434a58b`. Section 5.2
> therefore overstates the transport gap: the rebase already merged upstream's
> Block2 plumbing (`block2=`, `_reassemble_block2`, `BLOCK2_OPTION`,
> `_decode_block_option`, `oscore_get`, `oscore_put`) into
> `tests/runtime/transport/`. After the rebase the only symbols still missing
> are `oscore_delete`, `oscore_multicast_post`, `count_incoming_messages`,
> `multicast_post` and `source_port`. Everything else in this report stands.
> It is the driver for splitting the restored hand-written suite into the
> runtime tier (see `memory/project-runtime-test-tiers.md`).

# Upstream runtime-test inventory vs. the EITT XML interpreter

Branch under review: `feature/runtime_tests_v2` @ **8434a58b** (EITT interpreter, hand-written suite deleted)
Upstream: `knx-org/work_in_progress` @ **b6271257** (hand-written suite kept and grown)
Merge base: **16ec40f8**

Worktree used: `D:\Zephyr\modules\lib\knx-iot-point-api-stack\.claude\worktrees\agent-ac4253b1e708a4bf1`
(upstream `tests/runtime/` materialised over the branch tree via `git checkout knx-org/work_in_progress -- tests/runtime/`; branch versions read with `git show 8434a58b:<path>`)

---

## 1. Headline counts

| Quantity | Value |
|---|---|
| Upstream hand-written test **files** | 20 (`tests/runtime/test_5_*.py`, `test_coap_*.py`) + 1 non-test tool (`test_runner_gui.py`) |
| Upstream test **functions** | **349** |
| Upstream collected pytest **items** | **385** (349 − 6 parametrised funcs + 42 generated cases) |
| EITT template `TestCase` elements | **193** (`tests/runtime/data/KnxConformanceTestTemplate-PointAPI.xml`, ns `http://knx.org/xml/eitt/4.3`) |
| …of which numbered conformance cases | **187** (6 are `Test Sequence Preparation`) |

The 385 figure is independently confirmed by upstream's own `tests/TEST_CATALOG.md`
("380 passed, 5 skipped, 385 collected, 848.00s").

### Bucket totals (by test function; 349 total)

| Bucket | Functions | Items |
|---|---:|---:|
| **COVERED** | 135 | 143 |
| **COVERED-WEAK** | 95 | 99 |
| **KEEP** | 119 | 143 |
| **INFRA** | 0 test funcs (1 file) | 0 |
| **Total** | **349** | **385** |

COVERED + COVERED-WEAK = 230 functions map onto a template `TestCase`.
**119 functions (143 items, ~37 % of the collected suite) have no template case at all**
and are the hard requirement for the third tier.

The KEEP item count exceeds the function count because four KEEP functions are
parametrised: `test_invalid_block_query_returns_400` [×18],
`test_5_3_8_7i_invalid_scope_names_rejected` [×3],
`test_5_3_8_7j_mixed_scope_keeps_only_the_valid_names` [×4],
`test_5_3_18_2_empty_message_with_oscore_option_rejected` [×3].

### Per-file breakdown

| File | Funcs | Items | COVERED | COVERED-WEAK | KEEP |
|---|---:|---:|---:|---:|---:|
| `test_5_1_discovery.py` | 19 | 19 | 10 | 0 | 9 |
| `test_5_1_discovery_extended.py` | 14 | 14 | 11 | 3 | 0 |
| `test_5_1_discovery_mdns.py` | 32 | 32 | 8 | 8 | 16 |
| `test_5_1_discovery_multicast.py` | 19 | 19 | 17 | 2 | 0 |
| `test_5_2_device_resources.py` | 16 | 24 | 5 | 11 | 0 |
| `test_5_2_fingerprint.py` | 5 | 5 | 2 | 3 | 0 |
| `test_5_2_reset.py` | 2 | 2 | 0 | 2 | 0 |
| `test_5_2_swu.py` | 8 | 29 | 1 | 2 | 5 |
| `test_5_2_wellknown_knx.py` | 5 | 5 | 2 | 3 | 0 |
| `test_5_3_security.py` | 84 | 91 | 13 | 41 | 30 |
| `test_5_4_group_comm.py` | 31 | 31 | 11 | 8 | 12 |
| `test_5_4_observe.py` | 19 | 19 | 0 | 0 | 19 |
| `test_5_5_fp_tables.py` | 38 | 38 | 35 | 0 | 3 |
| `test_5_6_app_program.py` | 9 | 9 | 5 | 4 | 0 |
| `test_5_7_functional_blocks.py` | 2 | 2 | 0 | 2 | 0 |
| `test_5_8_parameters.py` | 7 | 7 | 4 | 3 | 0 |
| `test_5_10_generic.py` | 26 | 26 | 11 | 3 | 12 |
| `test_coap_header_budget.py` | 9 | 9 | 0 | 0 | 9 |
| `test_coap_payload_limit.py` | 4 | 4 | 0 | 0 | 4 |
| `test_runner_gui.py` | 0 | 0 | — | — | INFRA |
| **Total** | **349** | **385** | **135** | **95** | **119** |

---

### The KEEP set at a glance (what the third tier must contain)

| Group | Funcs | Items | File |
|---|---:|---:|---|
| CoAP Observe (`/p` + `/k`, OSCORE notifications) | 19 | 19 | `test_5_4_observe.py` |
| mDNS sleep-period / transition-audit / state-sync (`5.1.900/901/902`) | 16 | 16 | `test_5_1_discovery_mdns.py` |
| SPAKE2+ state machine (`5.3.1.5a–k`) | 11 | 11 | `test_5_3_security.py` |
| AT mandatory-property decision table (`5.3.8.7a–j`) | 10 | 15 | `test_5_3_security.py` |
| Pagination hardening on generic lists | 12 | 12 | `test_5_10_generic.py` |
| Pagination hardening on `.well-known/core` (`5.1.1.900`) | 9 | 9 | `test_5_1_discovery.py` |
| CoAP header budget (OUTER/INNER + blockwise) | 9 | 9 | `test_coap_header_budget.py` |
| SPAKE2+ brute-force lockout (`5.3.1.6a–f`) | 6 | 6 | `test_5_3_security.py` |
| Discovery `ep` malformed-length parsing (`5.4.1.15 bonus`) | 6 | 6 | `test_5_4_group_comm.py` |
| SWU block-query validation + leading zero | 2 | 19 | `test_5_2_swu.py` |
| Read-on-init network gate (`5.4.900.x`, privileged) | 4 | 4 | `test_5_4_group_comm.py` |
| Oversized CoAP payload → 4.13 | 4 | 4 | `test_coap_payload_limit.py` |
| Empty-message-is-not-OSCORE (`5.3.18.2`) | 3 | 5 | `test_5_3_security.py` |
| `/swu/lastupdate` lifecycle + reboot persistence | 3 | 3 | `test_5_2_swu.py` |
| Table create-on-unknown-id 2.01 semantics | 3 | 3 | `test_5_5_fp_tables.py` |
| Unicast NON re-resolution (`5.4.901.1`) | 1 | 1 | `test_5_4_group_comm.py` |
| Update-only GO ignores `st:"a"` (`5.4.1.10b`, F-005) | 1 | 1 | `test_5_4_group_comm.py` |
| **Total** | **119** | **143** | |

Four recurring themes: **query-parameter hardening** (`pn`/`ps`/`po`/`pkgs` across
`.well-known/core`, `/fp/g`, `/a/swu` — 40 items), **CoAP framing limits** (header
budget + payload limit — 13 items), **security state machines** the template never
walks (SPAKE2+ ordering and lockout, AT mandatory properties — 34 items), and
**observe**, which the template nominally covers but the interpreter skips entirely.

---

## 2. The template's real case list (the yardstick)

Extracted directly from the XML (not from any doc). 187 numbered cases:

- **5.1.1** (34): `.1 .2 .3 .3b .3c .3d .3e .4 .4b .4c .4d .4e .5 .5b .5c .5d .6 .6b .6c .6d .6e .6f .7 .7b .7c .7d .7e .7f .7g .7h .8 .9a .9b .9c`
- **5.1.2** (8): `.1 .2 .3 .3a .3b .4 .5a .5b`
- **5.2** (19): `5.2.1.1 5.2.2.1 5.2.2.2 5.2.2.3 5.2.3.1 5.2.3.1b 5.2.3.2 5.2.4.1 5.2.4.1b 5.2.4.1c 5.2.4.2 5.2.4.3 5.2.5.1 5.2.6.1 5.2.7.1 5.2.7.1b 5.2.8.1 5.2.9.1 5.2.10.1b`
- **5.3** (45): `5.3.1.1 .1.2 .1.2b .1.3 .1.4a .1.4b .1.4c .1.4d 5.3.4.1 5.3.8.1 .1b .1c .1d .1e .1f .2 .3 .4 .5 .6 5.3.9.1 .3a .3b 5.3.10.1 .2a .2b 5.3.11.1 5.3.12.1 5.3.13.1 **5.3.14.1** 5.3.15.1 5.3.16.1 5.3.17.1 .3 .4 .5 .6 .7 .8 5.3.18.1 5.3.19.1 .2 .5 .6 5.3.20.1`
- **5.4.1** (16): `.1 .1.b .2 .3 .4 .5 .6 .7 .8 .9 .10 .11 .12 .13 .14 .15`
- **5.4.2** (7): `.1 .2 .2b .2c .2d .2e .3` — suite titled *"GET & OBSERVE /.knx (Preliminary - Not for Certification)"*
- **5.5** (34), **5.6** (7), **5.7** (2), **5.8** (5), **5.10** (9)

**Important corrections to upstream's own `tests/runtime/EITT_TEST_COVERAGE.md`:**
that file claims 179 IDs taken from `tests/EITT_REFERENCE_PROJECT/EittProject.xml` and
omits **5.3.14.1** and the whole **5.4.2** suite. The template is the authority here
(per the "count it in the template" rule), and it has both. Treat
`EITT_TEST_COVERAGE.md` as a useful starting map but not as the ID list.

### What the interpreter actually runs

`tests/runtime/eitt/overrides.yaml` on the branch **skips** these template cases,
so they are *not* covered by the interpreter even though the template has them:

| Template case | Override | Consequence for this inventory |
|---|---|---|
| `5.3.14.1` Set replay window size | `skip` — optional, stack has no PUT handler | Upstream has no test either. Gap on both sides. |
| `5.4.2.1 / .2 / .2b / .2c / .2d / .2e / .3` | `skip` ×7 — "out of scope, /.knx Observe not implemented" | **All 19 observe tests are KEEP.** See §4.3. |
| `5.10.1.2`, `5.10.1.4` | `xfail` (multi-IPv6 deviation; `/test/*` endpoints inflate `total`) | Upstream's 5.10 tests still add value; several are KEEP anyway. |

---

## 3. Per-file classification tables

Legend: **C** = COVERED, **CW** = COVERED-WEAK, **K** = KEEP.

### 3.1 `tests/runtime/test_5_1_discovery.py` — 19 funcs

| Test | Bucket | EITT id / reason |
|---|---|---|
| `TestUnicastDiscovery::test_5_1_1_2_unicast_discovery` | C | 5.1.1.2 — replicates the EITT `all:` regex set verbatim (`/dev`,`/auth`,`/swu` `ct=40`; `/k` `ct=60`\|`60 50`\|`50 60`; `fb.417`/`fb.421` `ct=40`) |
| `TestUnicastDiscoveryFilters::test_5_1_1_4_step1_rt_known_value` | C | 5.1.1.4 (step 1) |
| `…::test_5_1_1_4_step2_rt_wildcard` | C | 5.1.1.4 (step 2) |
| `…::test_5_1_1_4_step3_if_known_value` | C | 5.1.1.4 (step 3) |
| `…::test_5_1_1_4_step4_ep_serial_number` | C | 5.1.1.4 (step 4) |
| `…::test_5_1_1_4_step5_ep_sn_wildcard` | C | 5.1.1.4 (step 5) |
| `TestInvalidUnicastQueries::test_5_1_1_4b_invalid_query_key` | C | 5.1.1.4b |
| `…::test_5_1_1_4c_invalid_interface` | C | 5.1.1.4c |
| `…::test_5_1_1_4d_invalid_resource_type` | C | 5.1.1.4d |
| `…::test_5_1_1_4e_if_wildcard` | C | 5.1.1.4e |
| `TestDiscoveryPaginationManyFunctionalBlocks::test_5_1_1_900_pagination_follows_next_links_over_all_fbs` | K | no `5.1.1.900`; template 5.10.1.6 paginates `/fp/g` only and **never sends `pn`/`ps` to `.well-known/core`**. Walks `?rt=*&ps=1` across core + application FBs: 1 entry/page, href uniqueness, 200-page infinite-loop guard, and a single full-size page returning the identical href set with no `p.next`. Guards `074c7f68` / `ad576cea` / `d3d6d236`. |
| `…::test_5_1_1_900_negative_pn_returns_400` | K | pagination input validation on `/.well-known/core`; template never sends a negative `pn` |
| `…::test_5_1_1_900_negative_ps_returns_400` | K | negative `ps` rejected |
| `…::test_5_1_1_900_ps_zero_returns_400` | K | `ps=0` rejected |
| `…::test_5_1_1_900_negative_pn_and_ps_returns_400` | K | both negative rejected |
| `…::test_5_1_1_900_non_numeric_pn_returns_400` | K | non-numeric `pn` rejected |
| `…::test_5_1_1_900_trailing_garbage_ps_returns_400` | K | trailing garbage after digits rejected |
| `…::test_5_1_1_900_out_of_int_range_pn_returns_400` | K | out-of-int-range `pn` rejected |
| `…::test_5_1_1_900_overflow_pn_ps_returns_400` | K | integer-overflow `pn`/`ps` rejected |

All 9 KEEPs here are the runtime guard for commit **`c89c4a96` "Stricter validation for
pagination query parameters"** and **`b5ae9605` "Centralize and harden decimal query
param parsing"**. Nothing in the template exercises malformed pagination, and nothing
in the template paginates `.well-known/core` at all. Specific regressions guarded:
`atoi()`→`strtol()`+endptr (non-numeric, trailing garbage), the `query_ps == 0` →
`query_ps < 1` fix, the explicit `temp > INT_MAX` check (no `ERANGE` on 64-bit `long`),
and signed-multiplication overflow in `first_entry = pn * ps`.

### 3.2 `tests/runtime/test_5_1_discovery_extended.py` — 14 funcs, all covered

| Test | Bucket | EITT id |
|---|---|---|
| `TestProgrammingModeDiscovery::test_5_1_1_5c_pm_unicast_device_in_pm` | C | 5.1.1.5c |
| `…::test_5_1_1_5d_pm_unicast_device_not_in_pm` | CW | 5.1.1.5d — asserts exact **4.04** |
| `…::test_5_1_1_6d_pm_ep_correct_sn` | C | 5.1.1.6d |
| `…::test_5_1_1_6e_pm_ep_not_in_pm` | CW | 5.1.1.6e — asserts exact **4.04** |
| `…::test_5_1_1_6f_pm_ep_wrong_sn` | CW | 5.1.1.6f — asserts exact **4.04** |
| `TestGroupAddressDiscovery::test_5_1_1_7e_unicast_matching_ga` | C | 5.1.1.7e |
| `…::test_5_1_1_7f_unicast_multiple_go` | C | 5.1.1.7f |
| `…::test_5_1_1_7g_unicast_no_matching_ga` | C | 5.1.1.7g |
| `…::test_5_1_1_7h_unicast_multi_ga_per_go` | C | 5.1.1.7h |
| `…::test_5_1_1_7_multicast_ga_single_go` | C | 5.1.1.7 |
| `…::test_5_1_1_7b_multicast_ga_multiple_go` | C | 5.1.1.7b |
| `…::test_5_1_1_7c_multicast_ga_no_match` | C | 5.1.1.7c |
| `…::test_5_1_1_7d_multicast_multi_ga_per_go` | C | 5.1.1.7d |
| `TestGAWildcard::test_5_1_1_8_ga_wildcard_rejected` | C | 5.1.1.8 |

### 3.3 `tests/runtime/test_5_1_discovery_mdns.py` — 32 funcs (16 KEEP)

Requires `zeroconf` for the `mdns`-fixture tests; the `5.1.900/901/902` families use
raw sockets on `ff02::fb:5353` and skip without `DEVICE_IFACE`.

| Test | Bucket | EITT id / reason |
|---|---|---|
| `TestMdnsDiscoverySerialUnconfigured::test_5_1_2_1_browse_knx_service` | CW | 5.1.2.1 — plain PTR browse only, weaker than template |
| `…::test_5_1_2_1_browse_serial_subtype` | C | 5.1.2.1 — asserts `_00fa10020800._sub._knx._udp.local.` |
| `…::test_5_1_2_1_srv_resolution` | CW | 5.1.2.1 — asserts SRV hostname is exactly **`knx-{serial}.local.`** (commit `94803394`) |
| `…::test_5_1_2_1_aaaa_resolution` | CW | 5.1.2.1 — only "≥1 AAAA" |
| `…::test_5_1_2_1_coap_reachability` | CW | 5.1.2.1 — CoAP GET is 2.xx after 3 retries; no mDNS assertion |
| `TestMdnsDiscoverySerialConfigured::test_5_1_2_2_serial_subtype_configured` | C | 5.1.2.2 |
| `…::test_5_1_2_2_full_chain_configured` | CW | 5.1.2.2 — full PTR→SRV→AAAA→CoAP incl. exact hostname |
| `TestMdnsDiscoveryIA::test_5_1_2_3_ia_subtype_discovery` | C | 5.1.2.3 — exact `_ia1199887766-1101._sub.` |
| `…::test_5_1_2_3_ia_full_chain` | CW | 5.1.2.3 — full chain |
| `TestMdnsDiscoveryIAZero::test_5_1_2_3a_ia_zero_subtype` | C | 5.1.2.3a — `_ia0-0._sub.`, restores IA/IID |
| `TestMdnsDiscoveryIAUnconfigured::test_5_1_2_3b_ia_unconfigured_subtype` | CW | 5.1.2.3b — **passively captures the factory-reset announcement**, asserts `_ia0-ffff._sub.` PTR RDATA, then re-provisions SPAKE2+/AT/IA and mutates `oscore_ctx` in place. Needs `DEVICE_IFACE`. |
| `TestMdnsDiscoveryPM::test_5_1_2_4_pm_subtype_enabled` | C | 5.1.2.4 |
| `…::test_5_1_2_4_pm_full_chain` | CW | 5.1.2.4 — full chain |
| `…::test_5_1_2_4_pm_subtype_disabled` | CW | 5.1.2.4 — negative: instance absent after `{1:false}` |
| `TestMdnsUnsolicitedPM::test_5_1_2_5a_pm_toggle_announcements` | CW | 5.1.2.5a — verifies via **browse** (solicited), not by capturing unsolicited packets |
| `TestMdnsUnsolicitedIA::test_5_1_2_5b_ia_change_announcement` | CW | 5.1.2.5b — same weakness |
| `TestMdnsSleepPeriod::test_5_1_900_1_sp_not_present_by_default` | K | `SP` TXT key absent at sleep-period 0 |
| `…::test_5_1_900_2_sp_200` | K | `SP=200` appears in re-announcement |
| `…::test_5_1_900_3_sp_20000` | K | 5-digit upper range (`char sp[6]` buffer bound) |
| `…::test_5_1_900_4_sp_cleared` | K | `SP` TXT removed when sleep period returns to 0 |
| `TestMdnsTransitionAudit::test_5_1_901_1_transition_ia_change` | K | goodbye(TTL=0)→announce audit on IA change; old `_ia` in goodbye, new in announce, SN unchanged, gap ≤ 1.5 s |
| `…::test_5_1_901_2_transition_pm_enable` | K | `_pm` absent from goodbye, present in re-announce |
| `…::test_5_1_901_3_transition_pm_disable` | K | `_pm` in goodbye, absent from re-announce |
| `…::test_5_1_901_4_transition_sp_set` | K | SP 0→500: no goodbye, `SP=500` in announce |
| `…::test_5_1_901_5_transition_sp_clear` | K | SP 500→0: standalone TXT goodbye then announce without SP |
| `TestMdnsStateSync::test_5_1_902_1_state_sync_pm_sequence` | K | query→act→re-query PM consistency + `_pm` in goodbye |
| `…::test_5_1_902_2_state_sync_sp_sequence` | K | SP set/clear state-sync loop |
| `…::test_5_1_902_3_state_sync_restart_preserves_state` | K | restart: IA subtype persists, PM cleared; OSCORE SSN resync probe |
| `…::test_5_1_902_4_state_sync_factory_reset_clears_state` | K | factory reset clears IA/PM/SP, SN survives; re-provisions AT |
| `…::test_5_1_902_5_state_sync_restart_address_stability` | K | mDNS AAAA+SRV port still reach the device after restart |
| `…::test_5_1_902_6_state_sync_factory_reset_address_stability` | K | same invariant across factory reset |
| `…::test_5_1_902_7_state_sync_mdns_alive_in_unloaded` | K | **LSM unload must NOT emit a goodbye**; SRV port and address identical across unload — direct guard for commit `b4cab4bc` "Do not stop mDNS on UNLOADED" |

### 3.4 `tests/runtime/test_5_1_discovery_multicast.py` — 19 funcs, all covered

| Test | Bucket | EITT id |
|---|---|---|
| `TestMulticastDiscovery::test_5_1_1_1_site_local_discovery` | C | 5.1.1.1 (step 3) |
| `…::test_5_1_1_1_second_request` | C | 5.1.1.1 (step 4) |
| `TestMulticastDiscoveryFilters::test_5_1_1_3_step1_rt_known_value` … `_step5_ep_sn_wildcard` (5 tests) | C | 5.1.1.3 steps 1–5 |
| `TestInvalidMulticastQueries::test_5_1_1_3b_invalid_query_key` | C | 5.1.1.3b |
| `…::test_5_1_1_3c_invalid_interface` | C | 5.1.1.3c |
| `…::test_5_1_1_3d_invalid_resource_type` | C | 5.1.1.3d |
| `TestMulticastInterfaceWildcard::test_5_1_1_3e_if_wildcard` | C | 5.1.1.3e |
| `TestMulticastProgrammingModeDiscovery::test_5_1_1_5_pm_on_returns_response` | C | 5.1.1.5 |
| `…::test_5_1_1_5b_pm_off_no_response` | CW | 5.1.1.5b — negative silence over a 5 s window |
| `TestMulticastPMEndpointDiscovery::test_5_1_1_6_pm_ep_correct_sn` | C | 5.1.1.6 |
| `…::test_5_1_1_6b_pm_off_no_response` | C | 5.1.1.6b |
| `…::test_5_1_1_6c_pm_on_wrong_sn` | C | 5.1.1.6c |
| `TestMulticastIADiscovery::test_5_1_1_9a_correct_iid_ia` | C | 5.1.1.9a |
| `…::test_5_1_1_9b_wrong_iid` | C | 5.1.1.9b |
| `…::test_5_1_1_9c_wrong_ia` | CW | 5.1.1.9c — negative silence over 5 s |

Note: this file imports `BLOCK2_OPTION` and `_decode_block_option` from `coap_client` —
**both missing from the branch's transport module** (see §5).

### 3.5 `tests/runtime/test_5_2_device_resources.py` — 16 funcs / 24 items

| Test | Bucket | EITT id / reason |
|---|---|---|
| `TestDevResourceList::test_5_2_5_1_dev_link_format` | CW | 5.2.5.1 — enforces the EITT `all:` `ct=60` rule on every line **plus** per-resource `rt="…:dpa[.:]0.NN"`, deliberately accepting both the dot and the stack's colon form (a spec discrepancy the real EITT regex tolerates only because `.` matches any char) |
| `TestDevResourceRead::test_5_2_6_1_serial_number` | CW | 5.2.6.1 — exact `"00fa10020800"` |
| `…_firmware_version` | CW | 5.2.6.1 — exact `[0, 0, 1]` and 3-element shape |
| `…_hardware_type` | C | 5.2.6.1 — type-only (`str`) |
| `…_model` | CW | 5.2.6.1 — exact `"KNX Certification"` |
| `…_sna` | CW | 5.2.6.1 — exact `17`, asserting the derivation `0x1101 >> 8` |
| `…_da` | CW | 5.2.6.1 — exact `1`, asserting `0x1101 & 0xFF` |
| `…_iid` | C | 5.2.6.1 — type-only (`int`) |
| `…_pm` | CW | 5.2.6.1 — asserts CBOR **bool**, not 0/1 int |
| `…_mid` | CW | 5.2.6.1 — exact `667` |
| `…_port` | CW | 5.2.6.1 — `int` **and** `1024 <= port <= 65535` |
| `…_ipv6` | CW | 5.2.6.1 — CBOR byte string of exactly 16 bytes |
| `…_hname` | CW | 5.2.6.1 — exact `"knx-00fa10020800.local"`, pinning the `94803394` hostname format |
| `…_fid` | C | 5.2.6.1 — type-only (`int`) |
| `TestDevResourceWrite::test_5_2_7_1_write_pm` | C | 5.2.7.1 — PUT then read-back, restores `pm=false` in `finally` |
| `TestDevReadOnlyPutRejected::test_5_2_7_1b_put_readonly_rejected` **[×9]** | C | 5.2.7.1b — `dev/sn, dev/hwt, dev/hwv, dev/fwv, dev/model, dev/hostname, dev/sna, dev/da, dev/mport`; asserts 4.xx class (matching the template) plus a value-unchanged re-read |

Splitting 5.2.6.1 into 14 property reads maps to one template case, but 10 of the 14
pin exact configured values, exact CBOR types, or derived-value relationships that the
template's single case does not check. Not a coverage gap — keep for diagnosis value.

### 3.6 `tests/runtime/test_5_2_fingerprint.py` — 5 funcs

| Test | Bucket | EITT id / reason |
|---|---|---|
| `TestFingerprintChanges::test_5_2_4_1_fingerprint_changes` | CW | 5.2.4.1 — asserts the fingerprint **strictly changes** after each of several table/param modifications |
| `TestFingerprintInvalidWrite::test_5_2_4_1b_post_fingerprint_rejected` | CW | 5.2.4.1b — exact **4.05** |
| `…::test_5_2_4_1c_put_fingerprint_rejected` | CW | 5.2.4.1c — exact **4.05** |
| `TestFingerprintLoadingState::test_5_2_4_2_fingerprint_loading` | C | 5.2.4.2 (5.03) |
| `TestFingerprintUnloadedState::test_5_2_4_3_fingerprint_unloaded` | C | 5.2.4.3 (5.03) |

### 3.7 `tests/runtime/test_5_2_reset.py` — 2 funcs

| Test | Bucket | EITT id / reason |
|---|---|---|
| `TestFactoryReset::test_5_2_2_1_factory_reset` | CW | 5.2.2.1 — full erase-code-2 cycle incl. re-running SPAKE2+ and re-provisioning; asserts prior ATs are gone |
| `TestResetCode7::test_5_2_2_2_reset_code_7` | CW | 5.2.2.2 — asserts erase-code 7 is **selective**: `if.sec` ATs survive while network info is cleared |

### 3.8 `tests/runtime/test_5_2_swu.py` — 8 funcs / 29 items (4 funcs KEEP)

| Test | Bucket | EITT id / reason |
|---|---|---|
| `TestSwuResourceList::test_5_2_8_1_swu_link_format` | C | 5.2.8.1 — adds an `/a/swu` `ct=42` + `if=` check, still inside the EITT case's own validation set. Its expected listing must reflect the **removal of `/swu/maxdefer` (`bd42603d`) and `/swu/pkgqurl` (`a017c762`)** |
| `TestSwuFirmwareUpdate::test_5_2_9_1_swu_firmware_update` | CW | 5.2.9.1 — beyond the 4-step PUSH flow: `/swu/lastupdate` key 1 is a non-empty RFC3339 string, the map has **no `osv` key**, `lastupdate` **changes** after the upgrade, exact **4.00** for a mid-download `PUT /swu/update`, exact **4.04** for `pkgname`/`pkgv` with no package |
| `TestSwuLastUpdateLifecycle::test_5_2_9_0_lastupdate_is_mfg_date_before_any_update` | K | no template case: `/swu/lastupdate` equals the manufacturing date before any update (`47436b41`, `a7f68a63`) |
| `…::test_lastupdate_changes_after_swu_update` | K | `lastupdate` advances after a completed update |
| `…::test_lastupdate_survives_reboot` | K | `lastupdate` persists across restart (RTC/storage behaviour) |
| `TestSwuReadOnlyPutRejected::test_5_2_10_1b_put_readonly_swu_rejected` **[×5]** | CW | 5.2.10.1b — `swu/pkgname, swu/pkgbytes, swu/pkgv, swu/state, swu/result`; asserts **exact 4.05** (the template accepts the generic read-only failure) plus a 4.04-before/4.04-after invariant for `pkgname`/`pkgv` |
| `TestSwuBlockQueryValidation::test_invalid_block_query_returns_400` **[×18]** | K | `PUT /a/swu` `po`/`ps`/`pkgs` validation: non-numeric, trailing garbage (`2abc`, `64k`), negative, zero `ps`/`pkgs`, `2147483648` (> `INT_MAX`), 14-digit values, empty values — all exact **4.00**. Exercises the non-NUL-terminated raw-buffer bound-copy path. Guards `c89c4a96` + `e92bf3c0`. |
| `…::test_leading_zero_is_accepted` | K | `po=00&ps=008` must parse as decimal, not octal, and must not be rejected |

### 3.9 `tests/runtime/test_5_3_security.py` — 84 funcs / 91 items (**30 KEEP**)

COVERED (13): `test_5_3_4_1_get_auth_link_format`, `test_5_3_8_1b_post_auth_at_without_oscore_fails`,
`test_5_3_8_1c_put_auth_at_without_oscore_fails`, `test_5_3_8_1e_put_at_without_if_sec_scope_fails`,
`test_5_3_8_1f_put_at_with_if_sec_scope_still_fails`, `test_5_3_8_2_mixed_scope_rejected`,
`test_5_3_8_3_multi_group_scope`, `test_5_3_9_3b_get_auth_at_without_oscore_configured_device`,
`test_5_3_10_2b_get_at_with_wrong_scope_returns_403`, `test_5_3_12_1_get_auth_o_link_format`,
`test_5_3_19_1_oscore_with_16_byte_master_secret`, `test_5_3_20_1_limited_scope_can_access_matching_resource`,
`test_5_3_19_2_oscore_with_master_salt`.

COVERED-WEAK (41) — the notable ones:

| Test | EITT id | What is materially stricter |
|---|---|---|
| `test_5_3_4_1_get_auth_without_oscore_returns_403` | 5.3.4.1 | exact **4.03** on the unsecured GET (negative case absent from template) |
| `test_5_3_8_1_write_at_and_verify` | 5.3.8.1 | read-back asserts CBOR `data[0]==token_id`, `data[38]==2` |
| `test_5_3_8_1_write_at_with_group_scope` | 5.3.8.1 | stored scope must equal `[1,2,3]` |
| `test_5_3_8_1d_post_at_without_if_sec_scope_fails` | 5.3.8.1d | additionally proves the rejected AT was **not created** |
| `test_5_3_8_4_overwrite_existing_at` | 5.3.8.4 | overwrite **replaces**, not merges, the scope |
| `test_5_3_8_6_update_multiple_ats` | 5.3.8.6 | exact per-token updated scopes |
| `test_5_3_9_1_get_auth_at_link_format` | 5.3.9.1 | provisioned token id present in listing |
| `test_5_3_10_1_get_at_by_id` | 5.3.10.1 | CBOR `data[0]`, `data[38]`, master secret present under `cnf(8).osc(4)` |
| `test_5_3_10_1_get_nonexistent_at_returns_error` | 5.3.10.1 | negative not in template; notes stack returns **4.00** not 4.04 |
| `test_5_3_10_2a_get_at_without_oscore_returns_403` | 5.3.10.2a | documents the **4.01-planned / 4.03-actual** accepted deviation |
| `test_5_3_11_1_delete_at` | 5.3.11.1 | full create→verify→delete→verify-gone |
| `test_5_3_11_1_delete_nonexistent_at` | 5.3.11.1 | negative not in template |
| `test_5_3_13_1_get_replwdo` | 5.3.13.1 | decoded value is `int` and `> 0` |
| `test_5_3_16_1_put_osndelay` | 5.3.16.1 | write 500, read back `== 500`, restore |
| `test_5_3_19_1_oscore_with_32_byte_master_secret` | 5.3.19.1 | stored master secret read back is exactly 32 bytes |
| `test_5_3_17_1_old_ssn_within_window_accepted` | 5.3.17.1 | jumps SSN +31 then loops offsets `[0,1,2,3,4,30]` — probes the whole 32-entry window |
| `test_5_3_18_1_corrupted_auth_tag_rejected` / `…_different_auth_tag_rejected` | 5.3.18.1 | **raw-socket forged datagrams** (single-bit flip / random 8-byte tag) |
| `test_5_3_17_7_echo_on_new_context` | 5.3.17.7 | inspects the client SSN counter `in (1,2)` to detect whether the echo retry happened |
| `test_5_3_19_6_context_ids_with_master_salt` | 5.3.19.6 | loops **13 context IDs**, repeating the first to verify key re-derivation |
| `test_5_3_17_6_receive_device_multicast` | 5.3.17.6 | asserts outer NON, `kid`, `kid_ctx`, inner POST, and CBOR `sia`/`ga` |
| `test_5_3_19_5_group_oscore_with_salt_and_ctx_id` | 5.3.19.5 | same, proving the salt participates in group HKDF |
| `test_5_3_17_3_group_anti_replay_window` | 5.3.17.3 | 31-send SSN choreography; verdict read from the **datapoint value**, not the response code |
| `test_5_3_17_4_sync_delay_echo` | 5.3.17.4 | asserts a challenge really arrived (10-byte random KidContext, inner option 252) |
| `test_5_3_17_5_invalid_echo_reply` | 5.3.17.5 | rejection proven by datapoint state |
| `test_5_3_1_3_spake_across_reset_types` | 5.3.1.3 | walks **all 5 reset types**, automating the two formerly manual steps |
| `test_5_3_17_8_echo_on_pase_at_post` | 5.3.17.8 | asserts `pase_ctx.ssn == 0` before, `in (1,2)` after |
| `test_5_3_1_2_wrong_password_spake` | 5.3.1.2 | asserts client-side `ValueError match="confirmV"` |
| `test_5_3_1_1_full_handshake` | 5.3.1.1 | uses the derived key for a real OSCORE GET and reads back the provisioned PASE token |
| `test_5_3_20_1_*` (4 of 5) | 5.3.20.1 | extra positive/negative scope controls (`if.p`→`/dev/pm`, `if.d`→`/dev/da`, `if.c` rejected on POST `/auth/at` and PUT `/dev/pm`) |

**KEEP (30 funcs, 37 items)** — four families with **no template case at all**:

| Family | Funcs | Reason |
|---|---:|---|
| `TestWriteAccessTokenMandatoryProperties::test_5_3_8_7a…7j` | 10 (15 items) | AT mandatory-property decision table: create with id only / without scope / without sender-id / without master-secret → exact **4.00** and no entry; empty scope array is NIL not a delete; id-only deletes; partial update accepted; `7i` **[×3]** invalid scope names; `7j` **[×4]** mixed scope keeps only valid names. This is the runtime guard for **`bf53ef81` "Strengthen AT validation"** and **`a9b75f4e` "Fix ACL scope aggregation"**. |
| `TestEmptyMessageIsNotOscore::test_5_3_18_2_*` | 3 (5 items) | RFC 7252 §4.1: a code-0.00 EMPTY message carrying an OSCORE option must not be processed **[×3 CON/NON/ACK]**; an EMPTY RST must not break OSCORE; a forged RST with TKL=8 must be refused. Guards **`9f57f9e3` "Stricter OSCORE option parsing"** and **`5e1e8527`**. |
| `TestSpake2PlusStateMachine::test_5_3_1_5a…5k` | 11 | SPAKE2+ handshake state machine: out-of-order steps → 4.00; step-1 restart from PARAMS_SENT/CREDENTIALS_SENT is legal and returns fresh parameters; step-1 cardinality (missing `id`, missing `rnd`); recovery paths complete with a working key. |
| `TestSpake2PlusBruteForce::test_5_3_1_6a…6f` | 6 | Brute-force lockout: 10 failures then **5.03**; factory reset clears the block; Max-Age (option 14) strictly shrinks proving the 6 s tick; self-unblock after ~60 s (`skipif RUN_SLOW_TESTS`); a partial step-1 does not reset the counter; a completed handshake does. |

### 3.10 `tests/runtime/test_5_4_group_comm.py` — 31 funcs (11 KEEP)

| Test | Bucket | EITT id / reason |
|---|---|---|
| `TestUnicastWrite::test_5_4_1_1_unicast_write_updates_value` | C | 5.4.1.1 |
| `TestUnicastWriteUnauthorized::test_5_4_1_1b_unauthorized_ga_returns_forbidden` | CW | 5.4.1.1.b — accepts **4.03 or 4.00** (loosened per the accepted deviation) but adds a value-unchanged check |
| `TestMulticastWrite::test_5_4_1_2_multicast_write_updates_value` | C | 5.4.1.2 |
| `TestMulticastRead::test_5_4_1_3_multicast_read_gets_answer` | CW | 5.4.1.3 — decrypts the answer and asserts `sia==0x1101`, `ga==65535`, `st=="a"`, `value is False` |
| `TestUnicastRead::test_5_4_1_4_unicast_read_gets_answer` | CW | 5.4.1.4 — **weaker**: the DUT's answer POST is drained, never decrypted |
| `TestUnicastWriteMultipleGO::test_5_4_1_5_write_updates_both_datapoints` | C | 5.4.1.5 |
| `TestTriggerMulticastWrite::test_5_4_1_6_trigger_multicast_write` | CW | 5.4.1.6 — asserts CoAP `type==NON` and decrypted `sia`/`st=="w"`/`ga` |
| `TestTriggerFirstGA::test_5_4_1_7_sends_first_ga` | C | 5.4.1.7 |
| `TestNotSendingInLoadingState::test_5_4_1_8_no_send_in_loading` | C | 5.4.1.8 |
| `TestInitFlagStartup::test_5_4_1_9_init_flag_sends_read_on_restart` | CW | 5.4.1.9 — **skips rather than fails** when no message arrives |
| `TestReadOnInitNetworkGate::test_5_4_900_1_read_on_init_up_from_start_sends_read` | K | read-on-init fires when a ULA (scope 5) exists before restart. **Privileged**: `ip -6 addr` on `DUT_IFACE`, Linux only |
| `…::test_5_4_900_2_read_on_init_withheld_while_down` | K | withheld while only link-local for ≥600 ms / 6 reschedule cycles. Privileged |
| `…::test_5_4_900_3_read_on_init_released_after_network_up` | K | released on live `RTM_NEWADDR` transition. Privileged |
| `…::test_5_4_900_4_read_on_init_abandoned_after_max_retries` | K | 20-cycle (~2 s) retry cap; counter resets on a fresh restart. Privileged |
| `TestMulticastResponseUpdate::test_5_4_1_10_multicast_answer_updates_value` | C | 5.4.1.10 |
| `TestMulticastResponseUpdateOnly::test_5_4_1_10b_answer_updateonly_go` | K | update-only GO (cflag 0x80, w=0): `st:"a"` must be ignored. Probes spec 2.5.7.3.3 Table 19 "if w=true" vs code requiring UPDATE **and** WRITE (FINDINGS F-005) |
| `TestMulticastWriteIgnoredLoading::test_5_4_1_11_write_ignored_in_loading` | C | 5.4.1.11 |
| `TestOwnGroupObjectUpdate::test_5_4_1_12_self_update_on_transmit` | CW | 5.4.1.12 — asserts **both** the transmitting `/p/3` and the shared-GA receiver `/p/1` updated |
| `TestLongGAReceive::test_5_4_1_13_long_ga_write_ga0` | C | 5.4.1.13 |
| `…_ga256` / `…_ga65535` / `…_ga_max32` | C ×3 | 5.4.1.13 — GA boundary values with manually raised SSNs |
| `TestLongGASend::test_5_4_1_14_sends_long_ga` | CW | 5.4.1.14 — decrypts and asserts `ga == 4294967295` |
| `TestUnicastConfirmable::test_5_4_1_15_unicast_sends_con` | CW | 5.4.1.15 — runs a **discovery responder** to resolve `PEER_IA`, then asserts CoAP `type==CON` |
| `TestUnicastNonReResolution::test_5_4_901_1_unicast_non_re_resolution` | K | 4 unanswered NON POSTs mark the recipient UNRESOLVED and force a second discovery GET (~60 s) |
| `TestUnicastDiscoveryMalformedLengths::test_5_4_1_15_bonus_iid_too_long` | K | discovery `ep` with 12-hex IID (max 10) rejected |
| `…_bonus_iid_too_short` | K | empty IID rejected |
| `…_bonus_ia_too_long` | K | 5-hex IA rejected; truncation must not match `PEER_IA` |
| `…_bonus_ia_too_short` | K | empty IA rejected |
| `…_bonus_sn_too_long` | K | 17-char serial pushes the `knx://ia.` anchor out of the parser window |
| `…_bonus_sn_too_short` | K | documents the **lenient** parser: a 4-char serial still resolves |

The six `_bonus_*` names embed `5_4_1_15` but are explicitly additional — they guard
`5371cc3c` / `a0853da3` / `586e2dfb` (IA/IID/serial bounded parsing) and must be KEEP.
All except the first four classes need `device_iface`.

### 3.11 `tests/runtime/test_5_4_observe.py` — 19 funcs, **all KEEP**

The template *does* have a 5.4.2 suite, but it is titled
*"(Preliminary - Not for Certification)"*, covers **`/.knx` S-Mode only**, has
5.4.2.2d as an empty `TODO`, and **all seven cases are `skip`-ped by
`tests/runtime/eitt/overrides.yaml`** ("out of scope, /.knx Observe not implemented in
the stack yet"). Upstream's tests target `/p/1` datapoints and `/k`, and are the only
observe coverage that actually executes.

| Test | Bucket | Reason |
|---|---|---|
| `test_5_4_2_1_obs_a1_register_returns_current_value` | K | observe register on `/p/1` returns 2.05 + Observe option + current value (template 5.4.2 is `/.knx` only, and skipped) |
| `test_5_4_2_2_obs_a2_value_change_notifies` | K | notification on value change with incremented Observe sequence |
| `test_5_4_2_3_obs_a3_deregister_stops_notifications` | K | `Observe=1` deregistration ends the relationship |
| `test_5_4_2_4_obs_a3b_delete_sub_removes_subscription` | K | `DELETE /sub` is a third deregistration path → 2.02 |
| `test_5_4_2_5_obs_a4_resubscribe_replaces_by_ip_port` | K | spec 2.6.10.1: re-subscribe replaces by source IP+port, **not** token; exactly one notification |
| `test_5_4_3_1_obs_b1_missing_lt_rejected` | K | missing `lt` → 4.00 (nearest template case 5.4.2.3 is skipped) |
| `test_5_4_3_2_obs_b2_lt_zero_rejected` | K | `lt=0` → 4.00 |
| `test_5_4_3_3_obs_b3_expired_lt_stops_notifications` | K | lifetime expiry stops notifications |
| `test_5_4_4_1_obs_c1_default_confirmable` | K | default notifications are CON (template 5.4.2.2d is an empty TODO) |
| `test_5_4_4_2_obs_c2_non_true_non_confirmable` | K | `non=true` yields NON |
| `test_5_4_5_1_obs_d1_first_k_notification_sia_only` | K | first `/k` notification is exactly `{sia}` with no `s` object (spec 2.5.9.1) |
| `test_5_4_5_2_obs_d2_subsequent_k_notification_has_s_object` | K | later `/k` notification includes `s` with `st="w"` |
| `test_5_4_5_3_obs_d3_k_deregister_stops_notifications` | K | `/k` deregister stops notifications |
| `test_5_4_5_4_obs_d4_inbound_post_k_not_echoed` | K | inbound s-mode POST `/k` is not echoed to observers |
| `test_5_4_5_5_obs_d5_plain_get_k_no_subsequent` | K | plain GET `/k` returns `sia` and never pushes |
| `test_5_4_6_1_obs_e1_encrypted_notifications_decrypt` | K | OSCORE-protected notifications decrypt correctly |
| `test_5_4_6_2_obs_e2_notification_aad_binds_request_piv` | K | notification AAD binds the request PIV (the OSCORE-AAD-for-observe fix) |
| `test_5_4_7_1_obs_f1_independent_seq_per_observer` | K | independent Observe sequence per observer |
| `test_5_4_7_2_obs_f2_stale_observer_pruned` | K | stale observer pruned |

Design rationale: `tests/runtime/OBSERVE_TEST_PROPOSAL.md`.

### 3.12 `tests/runtime/test_5_5_fp_tables.py` — 38 funcs (3 KEEP)

**35 of 38 map 1:1 onto their name-embedded template id** — verified: no duplicates and
no template id claimed by two tests. 5.5.1.1/.2/.3/.4/.5/.6/.7/.8/.9/.9b (10),
5.5.2.1, 5.5.3.1, 5.5.4.1, 5.5.5.1/.3/.4/.5/.5a/.5b/.6/.7a/.8a (9), 5.5.6.1, 5.5.7.1,
5.5.8.1, 5.5.9.1/.3/.4/.6/.7a/.8a (6), 5.5.10.1, 5.5.11.1, 5.5.12.1, 5.5.12.2.

The 3 exceptions:

| Test | Bucket | EITT id / reason |
|---|---|---|
| `TestGroupObjectTableWrite::test_5_5_1_6b_update_nonexisting_go` | K | no `5.5.1.6b` — POST `/fp/g` with unused id 999 must **create** and return exactly **2.01 Created**, not 2.04 or an error: the table's create-vs-update discrimination on an unknown id |
| `TestPublisherTableWrite::test_5_5_9_5_update_multiple_publishers` | K | no `5.5.9.5` — two publishers created (2.01), re-POSTed with the same ids and changed GA lists → exactly **2.04 Changed**. The publisher-table analogue of 5.5.5.5, which the template has only for recipients |
| `TestPublisherTableWrite::test_5_5_9_5b_update_nonexisting_publisher` | K | no `5.5.9.5b` — POST `/fp/p` with unknown id 999 → exactly **2.01 Created** (analogue of 5.5.5.5b) |

### 3.13 `tests/runtime/test_5_6_app_program.py` — 9 funcs

| Test | Bucket | EITT id / reason |
|---|---|---|
| `TestLsmTransitions::test_5_6_1_1_start_loading` | C | 5.6.1.1 |
| `…::test_5_6_1_2_load_complete` | C | 5.6.1.2 |
| `…::test_5_6_1_3_unload` | C | 5.6.1.3 |
| `…::test_5_6_1_4_lsm_persist_loading` | CW | 5.6.1.4 — LSM state 2 persists over a real **reboot** |
| `…::test_5_6_1_4_lsm_persist_loaded` | CW | 5.6.1.4 — state 1 persists over reboot |
| `…::test_5_6_1_4_lsm_persist_unloaded` | CW | 5.6.1.4 — state 0 persists over reboot |
| `TestLsmRead::test_5_6_2_1_read_lsm_all_states` | CW | 5.6.2.1 — reads `{3:<state>}` in **each** LSM state |
| `TestAppProgramList::test_5_6_3_1_read_ap_list` | C | 5.6.3.1 |
| `TestProgramVersion::test_5_6_4_1_write_read_pv` | C | 5.6.4.1 |

### 3.14 `tests/runtime/test_5_7_functional_blocks.py` — 2 funcs

| Test | Bucket | EITT id / reason |
|---|---|---|
| `TestFunctionalBlockList::test_5_7_1_1_read_fb_list` | CW | 5.7.1.1 — asserts at least two FB instances and their link-format attributes; sensitive to `80d3cd77` `include_if` |
| `TestFunctionalBlockDatapoints::test_5_7_2_1_read_fb_datapoints` | CW | 5.7.2.1 — asserts `/p/1` and `/p/2` specifically |

### 3.15 `tests/runtime/test_5_8_parameters.py` — 7 funcs

| Test | Bucket | EITT id / reason |
|---|---|---|
| `TestParameterPostWrite::test_5_8_1_1_write_parameter_via_post` | C | 5.8.1.1 |
| `TestParameterList::test_5_8_2_1_read_parameter_list` | CW | 5.8.2.1 — asserts `/p/p1` present; affected by the `/test/*` endpoint inflation noted in the 5.10.1.4 xfail |
| `TestParameterRead::test_5_8_3_1_read_parameter` | C | 5.8.3.1 |
| `TestParameterMetadata::test_5_8_3_2_read_all_metadata` | C | 5.8.3.2 (`?m=*`) |
| `…::test_5_8_3_2_read_metadata_id` | CW | 5.8.3.2 — `?m=id` sub-case |
| `…::test_5_8_3_2_read_metadata_value` | CW | 5.8.3.2 — `?m=value` sub-case |
| `TestParameterWrite::test_5_8_4_1_write_parameter` | C | 5.8.4.1 |

### 3.16 `tests/runtime/test_5_10_generic.py` — 26 funcs (**12 KEEP**)

| Test | Bucket | EITT id / reason |
|---|---|---|
| `TestPaginationPageNumber::test_5_10_1_1_page_zero_returns_entries` | C | 5.10.1.1 |
| `…::test_5_10_1_1_page_beyond_range_returns_400` | C | 5.10.1.1 — exact 4.00 beyond range is the template's own expectation |
| `TestPaginationPageSize::test_5_10_1_2_first_page_with_ps1` | C | 5.10.1.2 |
| `…::test_5_10_1_2_second_page_with_ps2` | C | 5.10.1.2 |
| `…::test_5_10_1_2_beyond_range` | C | 5.10.1.2 |
| `TestPaginationEdgeCases::test_5_10_1_2_ps_zero_returns_400` | K | `ps=0` hits the `query_ps < 1` branch independently of `first_entry >= total` |
| `…::test_5_10_1_2_last_valid_page_exact_boundary` | K | in-range boundary complement: `pn=3&ps=1` of 4 → exactly 1 entry and **no** `p.next` |
| `…::test_5_10_1_2_negative_pn_returns_400` | K | negative `pn` — indexing below the resource range |
| `…::test_5_10_1_2_negative_ps_returns_400` | K | negative `ps` |
| `…::test_5_10_1_2_non_numeric_pn_returns_400` | K | `pn=abc` — `atoi()` regression guard |
| `…::test_5_10_1_2_trailing_garbage_ps_returns_400` | K | `ps=2abc` — endptr check |
| `…::test_5_10_1_2_out_of_int_range_pn_returns_400` | K | `pn=2147483648` — `temp > INT_MAX` |
| `…::test_5_10_1_2_overflow_pn_ps_returns_400` | K | `pn=100000&ps=100000` — signed `pn*ps` overflow to a negative `first_entry` |
| `…::test_5_10_1_2_leading_zero_ps_is_accepted` | K | `ps=00001` parses as decimal 1, not octal |
| `…::test_5_10_1_2_negative_five_digit_pn_returns_400` | K | `pn=-12345` sits exactly at the accepted length limit, so `temp < 0` must reject it |
| `…::test_5_10_1_2_above_uint16_ps_returns_400` | K | `ps=65536` — `temp > USHRT_MAX` boundary complement of the accepted 65535 |
| `…::test_5_10_1_2_huge_pn_returns_400` | K | `pn=999999&ps=2` large-but-int-safe → still 4.00, no crash |
| `TestListMetadata::test_5_10_1_4_total_fpg` | C | 5.10.1.4 — `?l=total` on `/fp/g` |
| `…::test_5_10_1_4_total_p` | C | 5.10.1.4 — on `/p`; **this is the case the branch `xfail`s** (device answers `total=9`, template expects 5, because `/test/*` endpoints sit on the application-resource chain) |
| `…::test_5_10_1_4_total_auth_at` | C | 5.10.1.4 — on `/auth/at` |
| `TestPaginationNextPage::test_5_10_1_6_follow_next_links` | C | 5.10.1.6 — 5 GOs, `ps=2`, three pages, exactly 5 entries; guards `074c7f68` |
| `TestInvalidListMetadata::test_5_10_1_5_invalid_list_metadata` | CW | 5.10.1.5 — **looser** than the template (accepts 4.04, 2.05 *or* 4.00) but sweeps `/fp/g`, `/fp/r`, `/fp/p`, `/auth/at` instead of only `/fp/g` |
| `TestDptMetadata::test_5_10_4_1_read_dpt` | CW | 5.10.4.1 — asserts the **text** CBOR key `"dpt"` (not an integer key) and that the value contains `dpt.switch` |
| `TestDefaultOptions::test_5_10_5_1_accept_omitted` | C | 5.10.5.1 |
| `…::test_5_10_5_2_content_format_omitted` | C | 5.10.5.2 (loose: accepts 2.xx or 4.xx) |
| `…::test_5_10_5_3_unknown_critical_option` | CW | 5.10.5.3 — hand-rolls a raw non-OSCORE datagram with critical option **65001** (experimental range) and asserts exact 4.02 |

**Verified against the template:** 5.10.1.2 sends only well-formed `pn`/`ps` pairs (one
in-range, one beyond-range). It never sends negative, non-numeric, trailing-garbage,
`INT_MAX+1`, `USHRT_MAX+1`, leading-zero or overflow-product values. Only the 3 tests in
`TestPaginationPageSize` map to 5.10.1.2; **all 12 in `TestPaginationEdgeCases` are
uncovered** (11 hardening + 1 in-range boundary complement).

### 3.17 `tests/runtime/test_coap_header_budget.py` — 9 funcs, **all KEEP**

No EITT id in any name; no template case exercises header budgets.

| Test | Bucket | Reason |
|---|---|---|
| `TestInnerHeaderBudgetWorstCase::test_oscore_get_with_maximum_uri_query_is_answered` | K | 4 `Uri-Query` params summing to exactly `KNX_MAX_URI_QUERY_LEN`=64 inside the OSCORE plaintext must still serialize and be answered (`COAP_MAX_INNER_HEADER_SIZE`); a silent drop is a failure |
| `…::test_oscore_get_with_maximum_uri_path_is_answered` | K | 8 inner path segments totalling 48 chars; asserts a **4.xx**, i.e. the DUT refuses the unknown path rather than dropping the frame |
| `…::test_oscore_get_with_maximum_path_and_query_is_answered` | K | both Class E option sets at maximum simultaneously |
| `…::test_dut_still_responsive_after_worst_case_inner_frame` | K | liveness after the worst-case inner frame (heap/buffer overflow guard) |
| `TestPlainHeaderBudgetWorstCase::test_plain_discovery_with_maximum_uri_query_is_answered` | K | same budget check through unprotected `coap_serialize_message()` (`COAP_MAX_HEADER_SIZE`) |
| `…::test_plain_request_with_maximum_uri_path_is_answered` | K | maximal outer `Uri-Path`; asserts 4.xx |
| `…::test_plain_discovery_with_maximum_path_and_query_is_answered` | K | both at maximum |
| `…::test_dut_still_responsive_after_worst_case_plain_frame` | K | liveness after worst-case plain frame |
| `TestOuterHeaderBudgetBlockwise::test_oscore_discovery_pages_are_all_delivered` | K | multi-block OSCORE discovery: Block2 is a Class **U** outer option, must fit `COAP_MAX_OUTER_HEADER_SIZE` and deliver a complete non-empty payload — the **only** test exercising the Block2 path |

Direct guard for **`ff269ddc` "Split CoAP header budget into OUTER and INNER"**,
`e56c6f0b`, `dd2ab14f`, `ca905fb5`, `458a5b26`, `df59c184`.

### 3.18 `tests/runtime/test_coap_payload_limit.py` — 4 funcs, **all KEEP**

| Test | Bucket | Reason |
|---|---|---|
| `TestOversizedPayloadRejected::test_payload_exactly_max_returns_413` | K | exactly `KNX_PAYLOAD_SIZE`=768 → exact **4.13** (no room for the NUL terminator); rejected in `coap_oscore_parse_options` before routing |
| `…::test_payload_above_max_returns_413` | K | 868 bytes → exact 4.13, not silent truncation |
| `…::test_payload_below_max_is_not_rejected_with_413` | K | 766 bytes must **not** be answered 4.13 (negative control; other rejection reasons allowed) |
| `…::test_dut_still_responsive_after_oversized_request` | K | liveness after an oversized datagram |

Guard for **`b1bbc37e` "Reject oversized CoAP payloads"**, `fe431296` "Fix heap
overflow in CoAP buffer", `c4b345eb` "Use wire_len for datagram length".

### 3.19 `tests/runtime/test_runner_gui.py` — INFRA

Not a pytest module at all: a **standalone tkinter application** (`class TestRunnerApp`,
`def main()`, 871 lines) that shells out to `pytest --collect-only`, renders a checkbox
tree, and scrapes `PASSED|FAILED|SKIPPED|ERROR|XFAIL|XPASS \[ nn%\]` from the output.
It contains **zero test functions** — pytest collects nothing from it (the `Test*` class
has an `__init__`, so pytest would warn and skip even if it tried).

**Overlap with the branch's own GUI tests: none.** The branch (`8434a58b`) ships a
**PySide6** GUI under `tests/runtime/gui/` with 149 real unit tests across
`test_gui_interfaces.py` (12), `test_gui_models.py` (34), `test_gui_properties.py` (12),
`test_gui_runner.py` (7), `test_gui_theme.py` (10), `test_gui_trace.py` (21),
`test_gui_trace_export.py` (13), `test_gui_window.py` (11), plus `test_events.py` (10),
`test_project_copy.py` (11) and `test_comment_scheme.py` (8). Different toolkit,
different architecture (the branch GUI drives the interpreter directly; upstream's
scrapes pytest stdout). **Recommendation: drop `test_runner_gui.py`; it is superseded.**

---

## 4. Cross-cutting findings

### 4.1 `EITT_TEST_COVERAGE.md` is not a reliable ID list
It claims 179 IDs from `tests/EITT_REFERENCE_PROJECT/EittProject.xml` and **omits
5.3.14.1 and the entire 5.4.2 suite**, both of which the template contains. It also
lists only 2 "bonus" tests plus 4 in a side table, while the real non-template count is
110 functions. Use the template XML (or this report) as the yardstick.

### 4.2 The template has 5.4.2 but the interpreter skips all of it
Consequence: **observe is 100 % uncovered on the branch today.** All 19 upstream observe
tests are KEEP, not because the template lacks a section but because the section is
skipped as out-of-scope and targets `/.knx` rather than `/p`.

### 4.3 Where "COVERED-WEAK" is doing real work
Three patterns recur and are worth preserving even where a template case exists:
1. **Unsecured-access negatives** (`*_without_oscore_returns_403`, 8 tests in 5.3). The
   template's cases are secured reads; these assert the 4.03.
2. **CBOR field-level read-back** (`data[0]`, `data[38]`, exact scopes). The template
   matches with JSON pointers, but on far fewer fields.
3. **Persistence across reboot/reset** (LSM ×3, SWU lastupdate, mDNS state sync,
   5.2.2.2 selective reset). The template rarely restarts the device mid-case.

---

## 5. Porting surface

### 5.1 conftest: essentially compatible

Upstream `tests/runtime/conftest.py` (818 lines) vs branch `tests/runtime/conftest.py`
(871 lines) differ in **5 hunks only** (diff with `--strip-trailing-cr`; the files have
different line endings, which makes a naive `diff` useless).

**Fixture sets are identical.** Both expose exactly:
`server_process`, `coap_port`, `device_host`, `device_iface`, `mc_scope`, `coap`,
`oscore_ctx` (all `scope="session"`).

**Module-level symbols the tests import — all present on BOTH sides:**
`DUT_IA`, `DUT_IID`, `DUT_IA_HEX`, `DUT_IID_HEX`, `DUT_SERIAL_NUMBER`,
`DEVICE_PASSWORD`, `ALL_SCOPES`, `MC_SCOPE`, `TEST_SENDER_ID`, `TEST_TOKEN_ID`,
`auth_prepare`, `ia_prepare`, `set_lsm`, `parse_link_format`, `_eitt_sort_key`.

**Branch has, upstream lacks:**
- `pytest_addoption` → `--section` (repeatable, exact section-boundary match; the
  documented fix for `-k 5.6` also matching 5.5.5.6).
- `_try_discover`: substitutes `[::1]` when a discovery reply arrives from the
  unspecified address `::` (wildcard-bound simulated device).
- `discover_dut`: with an explicit interface, sweeps **both** scopes (5 then 2) instead
  of only the configured one — fixes Windows `WinError 10049` on scope 5.
- `server_process`: honours `RUNTIME_TEST_SERVER_PREFIX` (e.g. `ip netns exec dut`) so
  the DUT can run in its own netns — required for correct source addresses in
  5.4.1.4 / 5.4.1.15.

**Upstream has, branch lacks:** nothing.

**Import-path change required in every ported file:**
`from coap_client import …` → `from transport.coap_client import …`,
`from knx_oscore import …` → `from transport.knx_oscore import …`,
`from knx_spake2plus import …` → `from transport.knx_spake2plus import …`.
Mechanical; `from conftest import …` is unchanged.

### 5.2 `coap_client.py`: the real cost

Upstream `tests/runtime/coap_client.py` (1593 lines) vs branch
`tests/runtime/transport/coap_client.py` (2085 lines). The two are **complementary
supersets**, not competing rewrites.

**Missing from the branch, needed by the ported tests:**

| Symbol | Upstream use count in tests | Notes |
|---|---:|---|
| `CoapClient.oscore_get` | **181** | wraps `oscore_request(…,1,…)` **plus Block2 reassembly** |
| `CoapClient.oscore_put` | **61** | thin wrapper over `oscore_request(…,3,…)` |
| `CoapClient.oscore_delete` | 10 | thin wrapper over `oscore_request(…,4,…)` |
| `CoapClient.oscore_multicast_post` | 17 | group-OSCORE multicast send |
| `CoapClient.count_incoming_messages` | 7 | used by the observe/negative-silence tests |
| `CoapClient.multicast_post` | 0 | unused by tests; port for completeness only |
| `CoapClient.source_port` (property) | 0 | unused by tests |
| `BLOCK2_OPTION`, `SIZE2_OPTION`, `BLOCK_SZX_MAX` | imported by `test_5_1_discovery_multicast.py` | module constants |
| `_decode_block_option` | imported by `test_5_1_discovery_multicast.py` | module helper |
| `CoapClient._reassemble_block2` | internal | the actual Block2 loop |
| `block2=` parameter on `request`, `_build_request`, `oscore_request` | internal | plumbing for the above |

`oscore_post` **already exists** on the branch with an identical signature, so the
`oscore_get`/`put`/`delete` wrappers are a handful of lines each. The substantive work
is **Block2**: the branch has *zero* `block2` handling (`grep -c 'block2\|BLOCK2'` = 0),
while upstream threads it through `_build_request` → `request` → `oscore_request` and
adds `_reassemble_block2`. Only `test_coap_header_budget.py::TestOuterHeaderBudgetBlockwise`
and `test_5_1_discovery_multicast.py` depend on it directly, but `oscore_get` — used
181 times — reassembles transparently, so a non-reassembling `oscore_get` would silently
truncate long link-format bodies.

**Present on the branch, absent upstream** (keep, the interpreter needs them):
`arm_group_listener`, `arm_plain_listener`, `recv_unicast_telegram`,
`send_echo_challenge`, `send_plain_response`, `send_empty_ack`, plus the
`GroupListener` / `PlainListener` / `UnicastTelegramListener` classes and
`CoapFormatError`.

**`oscore_request` signature divergence — additive merge:**

| Parameter | Upstream | Branch |
|---|:--:|:--:|
| `payload`, `uri_queries`, `accept`, `content_format`, `timeout` | yes | yes |
| `block2` | **yes** | no |
| `seq_no_override`, `seq_no_offset`, `invalid_auth_tag`, `kid_override` | no | **yes** |
| `echo`, `expect_echo_challenge`, `msg_type`, `crit_option` | no | **yes** |

The branch also disables the automatic Echo-retry loop when any adversarial parameter is
set. Adding `block2=` alongside is orthogonal — no conflict.

### 5.3 `knx_oscore.py`: near-identical

Upstream 1 class (`OscoreContext`), branch the same. Method sets are identical except
the branch adds `protect_echo_challenge`. `protect_request` diverges the same additive
way: upstream has `block2=`, the branch has
`seq_no_override` / `seq_no_offset` / `invalid_auth_tag` / `kid_override`.
**Merge = add `block2=` to the branch's `protect_request`.** No test imports anything
else from this module.

`knx_spake2plus.py` is imported by 3 test files (`Spake2PlusClient`) and is unchanged
between the two trees apart from its location (`transport/`).

### 5.4 Per-KEEP-file dependency table

| File | Non-stdlib imports | conftest symbols | Fixtures | Extra requirements |
|---|---|---|---|---|
| `test_5_1_discovery.py` | `pytest`, `coap_client.LINK_FORMAT` | `parse_link_format` | `coap` | — |
| `test_5_1_discovery_mdns.py` | `pytest`, `cbor2`, `zeroconf` (`IPVersion`, `ServiceBrowser`, `ServiceInfo`, `ServiceListener`, `Zeroconf`, `InterfaceChoice`), `coap_client.{APPLICATION_CBOR, LINK_FORMAT, CoapClient}`, `knx_oscore.OscoreContext`, `knx_spake2plus.Spake2PlusClient` | `DEVICE_PASSWORD`, `ALL_SCOPES`, `DUT_IA`, `DUT_IID`, `auth_prepare`, `ia_prepare`, `set_lsm` | `coap`, `oscore_ctx`, local `mdns`, local `_require_iface` | **zeroconf**; `DEVICE_IFACE` for 900/901/902; raw multicast socket on `ff02::fb:5353` |
| `test_5_1_discovery_multicast.py` | `pytest`, `cbor2`, `coap_client.{LINK_FORMAT, BLOCK2_OPTION, _decode_block_option}` | `DUT_IA_HEX`, `DUT_IID_HEX`, `DUT_SERIAL_NUMBER`, `MC_SCOPE` | `coap`, `oscore_ctx` | **`BLOCK2_OPTION` + `_decode_block_option` must be added to the branch transport** |
| `test_5_2_swu.py` | `pytest`, `cbor2`, `coap_client.{APPLICATION_CBOR, LINK_FORMAT}` | — | `coap`, `oscore_ctx` | 3 parametrised (`path,payload_data` ×5, `queries` ×18) |
| `test_5_3_security.py` | `pytest`, `cbor2`, `coap_client.{APPLICATION_CBOR, LINK_FORMAT, knx_group_multicast_address, _encode_option}`, `knx_oscore.OscoreContext`, `knx_spake2plus.Spake2PlusClient` | `DEVICE_PASSWORD`, `ALL_SCOPES`, `MC_SCOPE`, `auth_prepare`, `ia_prepare` | `coap`, `oscore_ctx`, `device_iface`; locals `_cleanup_at_entries` (autouse, class, **all 22 classes**), `_reprovision_after` (5 classes), `factory_reset_device` (3 classes / 20 tests), `group_setup`, `group_setup_with_salt`, `group_rx_setup` (×3 definitions) | `DEVICE_IFACE` for the 5 group tests; raw socket for the forged-datagram tests; `RUN_SLOW_TESTS` for `5_3_1_6d`; **`oscore_multicast_post`** |
| `test_5_4_group_comm.py` | `pytest`, `cbor2`, `select`, `coap_client.{APPLICATION_CBOR, CoapClient, knx_group_multicast_address}`, `knx_oscore.OscoreContext` | `DEVICE_PASSWORD`, `ALL_SCOPES`, `MC_SCOPE`, `auth_prepare`, `ia_prepare`, `set_lsm` | `coap`, `oscore_ctx`, `device_iface`; locals `setup` (class), `_per_test_network_reset` | `DEVICE_IFACE` for most classes; **privileged `ip -6 addr` on `DUT_IFACE`, Linux only**, for the 4 `5_4_900_*` tests; binds port 5683 and joins `ff02::fd` / `ff05::fd`; **`oscore_multicast_post`**, **`count_incoming_messages`** |
| `test_5_4_observe.py` | `pytest`, `cbor2`, `coap_client.{CoapClient, APPLICATION_CBOR, _parse_options, CON, ACK}` | `auth_prepare`, `ia_prepare`, `set_lsm`, `DUT_IA`, `_eitt_sort_key` | `coap`, `oscore_ctx`, locals `make_observer`, `observe_runtime_setup` | one `CoapClient` per observer (subscription keyed by IP+port); **`count_incoming_messages`**; `observe_register_oscore` / `collect_notifications` / `observe_deregister` all already exist on the branch |
| `test_5_5_fp_tables.py` | `pytest`, `cbor2`, `coap_client.{APPLICATION_CBOR, LINK_FORMAT}` | `DUT_IA`, `DUT_IID`, `DEVICE_PASSWORD`, `ALL_SCOPES`, `set_lsm`, `parse_link_format` | local `_setup` (class-scope; no direct test args) | — |
| `test_5_10_generic.py` | `pytest`, `cbor2`, `coap_client.{APPLICATION_CBOR, LINK_FORMAT, _encode_option, CON, GET}` | `DUT_IA`, `DUT_IID`, `DEVICE_PASSWORD`, `ALL_SCOPES`, `auth_prepare`, `ia_prepare`, `set_lsm`, `parse_link_format` | `coap`, `oscore_ctx`, local `setup` | — |
| `test_coap_header_budget.py` | `pytest`, `coap_client.LINK_FORMAT` | — | `coap`, `oscore_ctx` | **Block2 reassembly required** for `TestOuterHeaderBudgetBlockwise` |
| `test_coap_payload_limit.py` | `pytest`, `coap_client.APPLICATION_CBOR` | — | `coap` | — |

### 5.5 Estimated porting work

| Item | Size |
|---|---|
| Add `oscore_get` / `oscore_put` / `oscore_delete` wrappers | ~20 lines |
| Add `block2=` plumbing (`_build_request`, `request`, `oscore_request`, `OscoreContext.protect_request`) + `_reassemble_block2` + `BLOCK2_OPTION` / `SIZE2_OPTION` / `BLOCK_SZX_MAX` / `_decode_block_option` | ~120 lines, ported verbatim from upstream |
| Add `oscore_multicast_post`, `multicast_post`, `count_incoming_messages`, `source_port` | ~150 lines, ported verbatim |
| Rewrite imports in the ported test files (`coap_client` → `transport.coap_client`) | mechanical |
| conftest | **no change needed** — every symbol the tests import already exists |

No fixture has to be invented, and no upstream conftest behaviour is missing from the
branch. The port is dominated by transport-module merges, not test rewrites.

---

## 6. Upstream device-behaviour changes to re-check

107 commits between `16ec40f8` and `b6271257`; **93 touch `api/`, `messaging/`,
`security/`, `port/` or `include/`**. The EITT interpreter's expectations were written
against the branch's device, so each of these is a candidate for an expectation drift.
**Flagged only — no outcome asserted.**

| Commit | Change | EITT sections possibly affected |
|---|---|---|
| `94803394` | Device hostname format → `knx-{serial}.local` (`api/oc_core_res.c`, `oc_knx_dev.c`, `include/oc_endpoint.h`) | **5.1** (mDNS SRV target, `5.1.2.1/.2/.3`), **5.2** (`dev/hname` in 5.2.6.1) |
| `c687ca1e` | DNS-SD hostname + subtype handling refactor | **5.1.2.x** |
| `02a35d1f` | **Always emit the TXT record** per RFC 6763 (`port/dns-sd_mdns.c`, +305/−108) | **5.1.2.x** (every mDNS case; TXT now present where it may previously have been absent) |
| `c67de330` | RFC 6762-compliant announcement repeats (F-015) | **5.1.2.5a/.5b** (unsolicited announcements), any capture-based timing |
| `e4cb4280` | mDNS TXT answer bug fix (F-016, `port/mdns.patch`) | **5.1.2.x** |
| `1a16de37` | Clarifies mDNS data on announcement **and** query response | **5.1.2.x** |
| `b4cab4bc` | **Do not stop mDNS on UNLOADED** (`api/oc_knx.c`, `port/dns-sd*`) | **5.1.2.x**, and any case that unloads the LSM (**5.6**) then expects mDNS state |
| `51aa032a`, `ea69414b` | Thread DNS-SD parity; IA subtype uses the full 64-bit IID | **5.1.2.3** (subtype label shape) — Thread builds |
| `bd42603d` | **Remove `/swu/maxdefer`** | **5.2.8.1** (link-format listing), **5.2.9.1** |
| `a017c762` | **Remove `/swu/pkgqurl`** | **5.2.8.1**, **5.2.9.1** |
| `cc6eb99f` | Enforce fixed SWU property values | **5.2.9.1**, **5.2.10.1b** |
| `e92bf3c0`, `96d992bb`, `8d59c420`, `36096df5` | SWU state machine / block handling refactors; `uint32_t` package byte counts | **5.2.9.1**, **5.2.10.1b** |
| `47436b41`, `a7f68a63`, `44c02d52` | Manufacturing date set; `/swu/lastupdate` simplification + RTC lifecycle | **5.2.8.1/.9.1** (and the KEEP lastupdate tests) |
| `074c7f68` | **Omit `p.next` on the last page** | **5.1.1** (discovery pagination), **5.10.1.6**, and every paginated list in **5.5**, **5.8**, **5.3.9.1** |
| `d3d6d236` | Only advertise a next page when one exists | same as above |
| `ad576cea` | Account for functional-block overflow in the next-page hint | **5.1.1**, **5.7** |
| `c89c4a96` | **Stricter pagination query-parameter validation** (`oc_discovery.c`, `oc_knx_dev.c`, `oc_knx_helpers.c`, `oc_knx_sec.c`, `oc_knx_swu.c`) | **5.10.1.1/.1.2/.1.5**, **5.1.1**, any list with `pn`/`ps` |
| `b5ae9605`, `cddde6b9`, `586e2dfb`, `a0853da3`, `82032382` | Centralised / hardened decimal + bounded numeric string parsing | **5.10.1.x**, **5.2.3.1** (IA/IID), **5.4.1.15** |
| `5371cc3c` | IA/IID parsing and bounds checking | **5.1.1.9a/b/c**, **5.2.3.1/.3.2**, **5.4.1.15** |
| `80d3cd77` | **`include_if` flag on discovery link-format** (`oc_discovery.c`, `oc_knx_fb.c/h`, `oc_knx_fp.c`, `oc_knx_p.c`, `oc_knx_sec.c`, `oc_knx_swu.c`) | **5.1.1.3/.4** (`if=` filters), **5.5**, **5.6.3.1**, **5.7.1.1/.2.1**, **5.8.2.1**, **5.2.5.1/.8.1**, **5.3.4.1/.12.1** — every link-format body |
| `764a9ec3` then `c76a0582` | `/auth` sub-resource listing corrected, then the ordering reverted | **5.3.4.1**, **5.3.12.1** (link-format order/content) |
| `d72fcaa9` | Reverted `auth/at` content format | **5.3.9.1**, **5.3.10.1** (`ct=` in link-format; the branch overrides already juggle `ct=40` vs `ct=60 40`) |
| `ff269ddc` | **CoAP header budget split into OUTER and INNER** (`oc_main.c`, `coap.c`, `conf.h`, `constants.h`, `oscore_constants.h`, `oc_oscore_engine.c`) | **all sections** — changes the maximum request/response that fits; most visible in **5.1** (long discovery queries), **5.10**, and anything blockwise |
| `e56c6f0b` | Tighter CoAP string-option group header sizing | same |
| `dd2ab14f`, `ca905fb5`, `458a5b26`, `df59c184`, `78fc1f4f` | MTU→PDU rename, block-size flooring, RFC 7959 Block parsing, MTU must fit both headers | **all** blockwise responses (long link-format bodies in 5.1/5.3/5.5) |
| `89a95216` | `OC_BLOCK_WISE` switch removed — blockwise is **always** active | **all** — responses that previously never blocked may now block |
| `5cbc920f` | **Harden CoAP option parsing** (`coap.c` +140/−… , `oscore.c`) | **5.10.5.3** (unknown critical option), **all** malformed-input paths |
| `5e1e8527` | Strict length checks across CoAP/OSCORE (`coap.c`, `oscore.c`, `oc_oscore_engine.c`) | **5.3** (all OSCORE), **5.10.5.x** |
| `9f57f9e3` | **Stricter OSCORE option parsing and error handling** | **5.3.18.1**, **5.3.19.x**, **5.3.17.x** |
| `fe431296` | Heap overflow fix in the CoAP buffer | **all** |
| `b1bbc37e`, `c4b345eb` | Reject oversized CoAP payloads; use `wire_len` for datagram length | **all** — new **4.13** path |
| `bf53ef81` | **Strengthen AT validation and cleanup for `POST /auth/at`** | **5.3.8.1…8.6**, **5.3.9.x**, **5.3.10.x**, **5.3.11.1** |
| `a9b75f4e` | **Fix ACL scope aggregation** (`oc_knx_sec.c`, `oc_ri.c`) | **5.3.20.1**, **5.3.8.1d/1e/1f**, **5.3.10.2b** |
| `94284034`, `b9fb9c59` | AT RAM reset refactor; `osc_version`/`osc_hkdf`/`osc_alg` marked unused | **5.3.8.x**, **5.3.10.1** (fields present in the AT read-back) |
| `38cb6af7` | `(uint32_t)` casts in `oc_replay_check_client` | **5.3.17.1**, **5.3.17.3** (anti-replay window arithmetic) |
| `c6f18619` | Stronger `hardware_type` validation | **5.2.6.1** (`dev/hwt`), **5.2.7.1b** |
| `09e03d4e` | `hwv` becomes a static pointer (no longer writable) | **5.2.6.1**, **5.2.7.1b** |
| `75d9d179`, `b6744688`, `494349f1`, `f2c0038e`, `af556849`, `615a4fef` | Strict hwref/serial validation, serial-number helper, upper-casing | **5.2.6.1** (`dev/sn`), **5.1.1.4 step 4** and **5.1.2.1** (`ep=knx://sn.…`), **5.4.1.15** |
| `7c3603c0` | Clarifies current OBSERVE behaviour (`oc_ri.c`, `observe.h`) | **5.4.2** (currently skipped) and the KEEP observe tests |
| `fcfe66ca` | Improved error handling in separate-response logic | **5.4.1.3/.4** (separate responses), **5.10** |
| `1f9a0c82`, `0ce57dba`/`4ba03f68`, `f0b69cbf`, `2e888568` | PKI removed; TCP/TLS/DTLS dead code removed; UDP+OSCORE only | build/config surface; the branch already dropped the matching flags (`41c2f64`) |
| `72ac75e8`, `35104ce6` | Multicast subscription handling in connectivity init; subscribe to site-local multicast | **5.1.1.1** (site-local discovery), **5.4.1.2/.3/.10/.11** — Zephyr/Thread |
| `34c5fc5f` | **prox-uri tests removed** — "option is not set actively by device" | *No device change*: touches only `tests/COVERAGE_LEDGER.md` and `tests/unit/test_coap.cpp`. Listed as a known candidate but it is test-only. |
| `380c0ca1` | SWU state machine edge-case tests | *No device change*: `tests/unit/test_oc_knx_swu.cpp` only. |

**Highest-risk for interpreter expectations**, in order: `80d3cd77` (`include_if`
changes *every* link-format body the template pattern-matches), `ff269ddc` +
`89a95216` (header budget / always-on blockwise change what fits in one response),
`074c7f68` + `d3d6d236` + `ad576cea` (`p.next` presence), `02a35d1f` (TXT always
present), and `bd42603d` + `a017c762` (two resources gone from the SWU listing that
template 5.2.8.1 may still expect).

---

## 7. Open questions

1. **`5.3.14.1` (PUT `/auth/o/replwdo`)** is in the template, `skip`-ped by the branch
   (no PUT handler, `oc_knx_sec.c:258`, `g_oscore_replay_window_size` hard-coded 32),
   and has **no upstream test either**. Genuine gap on both sides — is the resource
   meant to stay read-only, or is the PUT handler planned?
2. **Observe tier placement.** The 19 observe tests carry `5.4.2.x` numbers that
   *collide* with real (but skipped) template case ids. If they move to the runtime
   tier they should probably be renumbered (the proposal's own `OBS-x` labels) to avoid
   two different things called `5.4.2.2`. Needs a decision.
3. *(Resolved during this review — recorded for traceability.)* `TestPaginationEdgeCases`
   (12) and `test_5_1_1_900_*` (9) were initially split between CW and KEEP. Re-reading
   the template settled it: 5.10.1.2 sends only well-formed `pn`/`ps`, and no 5.1.1.x
   case paginates `.well-known/core` at all. **Both families are KEEP** and the counts
   above reflect that.
4. **The 14 `test_5_2_6_1_*` property reads** are one template case split 14 ways. Keep
   all 14 for diagnosis, or collapse to a parametrised test? Not a coverage question.
5. **`test_5_4_1_9_init_flag_sends_read_on_restart` skips instead of failing** when no
   message arrives, and three `test_5_4_1_15_bonus_*` cases "stand down under cumulative
   suite load" per `tests/runtime/README.md`. These are the 5 skips in upstream's green
   run. Are they acceptable as-is in the new runtime tier, or should they be made
   deterministic (e.g. by the netns isolation the branch's
   `RUNTIME_TEST_SERVER_PREFIX` enables)?
6. **`EITT_TEST_COVERAGE.md` vs `EittProject.xml`.** I used the template
   (`KnxConformanceTestTemplate-PointAPI.xml`, 187 numbered cases). Upstream's doc cites
   `tests/EITT_REFERENCE_PROJECT/EittProject.xml` with 179 — a path that does not exist
   in either tree I examined. If that reference project is the certification-relevant
   artefact rather than the template, the 5.3.14.1 / 5.4.2 conclusions need re-checking.
7. **`test_runner_gui.py`** — I recommend deleting it as superseded by the branch's
   PySide6 GUI, but it is the only *cross-platform, dependency-light* runner (tkinter
   ships with Python; PySide6 does not). If anyone runs the suite on a machine without
   PySide6, that is a reason to keep it.
