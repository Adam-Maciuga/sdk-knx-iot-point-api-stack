# mDNS DNS-SD subtype, SRV and TXT support — change summary

Adds RFC 6763 §7.1 selective instance enumeration (subtype PTR records) to
Zephyr's DNS-SD stack and wires it into the KNX-IoT Wi-Fi mDNS driver.
The patches also add SRV and TXT query handling (RFC 6763 §12.2), which is
not present natively in Zephyr 4.3 or 4.4.

KNX-IoT requires three subtype PTR records per device:

| Sub-type label            | Example                  | Condition          |
|---------------------------|--------------------------|--------------------|
| `_{serial}`               | `_00fa10020700`          | Always             |
| `_ia{iid_hex}-{ia_hex}`   | `_ia33a3-20a`            | After commissioning|
| `_pm`                     | `_pm`                    | Programming mode   |

---

Patch files:

- `port/zephyr/patches/0001-zephyr-4.3-net-dns-sd-add-subtype-support.patch`
- `port/zephyr/patches/0001-zephyr-4.4-net-dns-sd-add-subtype-srv-txt-support.patch`

---

## Changes common to Zephyr 4.3 and 4.4

### 1. `include/zephyr/net/dns_sd.h`

- `DNS_SD_MAX_LABELS` 4 → 5, updated comments.
- Added `const char *subtype` field to `struct dns_sd_rec`.
- Added registration macros:
  - `DNS_SD_REGISTER_SERVICE_SUBTYPE`
  - `DNS_SD_REGISTER_TCP_SERVICE_SUBTYPE`
  - `DNS_SD_REGISTER_UDP_SERVICE_SUBTYPE`

### 2. `subsys/net/lib/dns/dns_sd.h`

- Updated `dns_sd_query_extract` doc to describe 5-label sub-type query support.
- Added `dns_sd_handle_subtype_ptr_query` declaration.

### 3. `subsys/net/lib/dns/dns_sd.c`

- `subtype_is_valid()` — validates leading `_`, alphanumeric+hyphen, up to 63 chars.
- `add_subtype_ptr_record()` — builds `<sub>._sub.<svc>.<proto>.<domain>.` owner
  name in DNS wire format with a compressed RDATA pointer back to
  `<svc>.<proto>.<domain>.` within the same record.
- `dns_sd_handle_subtype_ptr_query()` — full PTR + TXT + SRV + A/AAAA response
  per RFC 6763 §12.1.
- `dns_sd_query_extract`: 5-label `<sub>._sub.<svc>.<proto>.<domain>` now parsed;
  populates `record->subtype`, leaves `record->instance` NULL (wildcard).
- `dns_sd_rec_match`: plain queries skip sub-type records; sub-type queries match
  only records whose `subtype` equals the filter's `subtype`.

### 4. `subsys/net/lib/dns/mdns_responder.c`

- Expanded label buffer array from 4 to 5 entries (`subtype_buf` added).
- Routes sub-type queries (`filter.subtype != NULL`) to
  `dns_sd_handle_subtype_ptr_query` instead of `dns_sd_handle_ptr_query`.

---

## Zephyr 4.4 additions

### API type renames (Zephyr 4.4 networking API changes)

The following type and constant renames apply throughout `dns_sd.c` and
`mdns_responder.c` in the 4.4 patch (no semantic change):

| 4.3                  | 4.4                      |
|----------------------|--------------------------|
| `struct in_addr`     | `struct net_in_addr`     |
| `struct in6_addr`    | `struct net_in6_addr`    |
| `htons()`            | `net_htons()`            |
| `IPPROTO_TCP`        | `NET_IPPROTO_TCP`        |
| `IPPROTO_UDP`        | `NET_IPPROTO_UDP`        |

`dns_sd_handle_ptr_query` and `dns_sd_handle_subtype_ptr_query` both gain a
`struct net_if *iface` as their first parameter.

### SRV and TXT query handling (RFC 6763 §12.2)

#### `subsys/net/lib/dns/dns_sd.h`

- Added declarations:
  - `dns_sd_handle_srv_query(iface, inst, addr4, addr6, buf, buf_size)`
  - `dns_sd_handle_txt_query(iface, inst, buf, buf_size)`

#### `subsys/net/lib/dns/dns_sd.c`

- `dns_sd_handle_srv_query()` — response to a SRV query for a specific instance
  (RFC 6763 §12.2): answer section contains the SRV record; additional section
  contains TXT and A/AAAA records.
- `dns_sd_handle_txt_query()` — response to a TXT query for a specific instance
  (RFC 6763 §12.2): answer section contains the TXT record only.

#### `subsys/net/lib/dns/mdns_responder.c`

- `send_sd_response` gains an `enum dns_rr_type qtype` parameter.
- The `IS_ENABLED(CONFIG_MDNS_RESPONDER_DNS_SD)` dispatch condition is expanded
  from PTR-only to also handle SRV and TXT:
  ```c
  } else if (IS_ENABLED(CONFIG_MDNS_RESPONDER_DNS_SD) &&
             (qtype == DNS_RR_TYPE_PTR ||
              qtype == DNS_RR_TYPE_SRV ||
              qtype == DNS_RR_TYPE_TXT)) {
      send_sd_response(sock, family, src_addr, addrlen,
                       &dns_msg, result, qtype);
  ```
- Inside `send_sd_response`, query dispatch order:
  1. Sub-type PTR (`filter.subtype != NULL`) → `dns_sd_handle_subtype_ptr_query`
  2. SRV (`qtype == DNS_RR_TYPE_SRV`) → `dns_sd_handle_srv_query`
  3. TXT (`qtype == DNS_RR_TYPE_TXT`) → `dns_sd_handle_txt_query`
  4. Otherwise (plain PTR) → `dns_sd_handle_ptr_query`

---

## KNX-IoT stack repo

### `port/zephyr/dns-sd-wifi.c`

- Three sub-type label buffers: `knx_subtype_sn`, `knx_subtype_ia`,
  `knx_subtype_pm`.
- Three `DNS_SD_REGISTER_SERVICE_SUBTYPE` linker-section records sharing the
  same `knx_instance` / `knx_txt` / `knx_port` buffers as the primary record.
- `knx_dns_sd_update_service`: populates sub-type buffers from `iid`, `ia`, and `pm`
  arguments; zeroes `knx_subtype_ia` when uncommissioned (`iid == 0 && ia == 0`);
  zeroes `knx_subtype_pm` when `pm == false`.
- `knx_dns_sd_stop`: zeroes all three sub-type buffers alongside `knx_instance`,
  suppressing all mDNS advertisements atomically.

---

## How suppression works

All four records (primary + three sub-types) share the `knx_instance` pointer.
Zephyr's `rec_is_valid()` rejects records with an empty instance string, so
zeroing `knx_instance` in `knx_dns_sd_stop` silently suppresses all four records
in the next mDNS response cycle without any special-casing per record.

Individual sub-type records can be suppressed independently by zeroing their
own buffer (e.g. `knx_subtype_pm` when leaving programming mode), because
`subtype_is_valid()` rejects empty or NULL sub-type strings.

---

## Known limitations / TODO

- **No proactive announcements**: Zephyr's mDNS responder has no public API for
  proactively sending unsolicited announcements or goodbye packets.  Updated
  sub-type buffers are only reflected in the next incoming query response.
  See the TODO FIXME comment in `port/zephyr/dns-sd-wifi.c` and the equivalent
  Linux/Windows implementation in `port/dns-sd.c` for reference.
