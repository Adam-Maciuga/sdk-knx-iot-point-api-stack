# mDNS DNS-SD subtype support — change summary

Adds RFC 6763 §7.1 selective instance enumeration (subtype PTR records) to
Zephyr's DNS-SD stack and wires it into the KNX-IoT Wi-Fi mDNS driver.

KNX-IoT requires three subtype PTR records per device:

| Sub-type label            | Example                  | Condition          |
|---------------------------|--------------------------|--------------------|
| `_{serial}`               | `_00fa10020700`          | Always             |
| `_ia{iid_hex}-{ia_hex}`   | `_ia33a3-20a`            | After commissioning|
| `_pm`                     | `_pm`                    | Programming mode   |

---

## Zephyr repo

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

## KNX-IoT stack repo

### 5. `port/zephyr/dns-sd-wifi.c`
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
