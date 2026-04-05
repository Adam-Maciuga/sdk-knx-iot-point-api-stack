/*
 * Copyright (c) 2026 KNX Association
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * KNX-IoT DNS-SD / mDNS service registration for Wi-Fi backends.
 *
 * Implements knx_dns_sd_update_service() and related helpers declared in port/dns-sd.h
 * using Zephyr's built-in DNS-SD API (CONFIG_DNS_SD=y).  The Zephyr mDNS
 * responder (CONFIG_MDNS_RESPONDER=y) handles the on-wire advertisement
 * automatically once a service is registered.
 *
 * KNX-IoT service model:
 *   Instance : <serial_number_lowercase>          e.g. "00fa10020700"
 *   Type     : _knx._udp.local.
 *   Host     : <SERIAL_NUMBER_UPPERCASE>.knx      e.g. "00FA10020700.knx"
 *   Port     : 5683  (CoAP)
 *   TXT      : (optional) SP=<sleep_period>
 *
 * Implementation note — Zephyr DNS-SD registration:
 *   Zephyr only supports compile-time DNS-SD registration via the
 *   DNS_SD_REGISTER_SERVICE() macro (linker-section approach).  There are no
 *   dns_sd_register_service() / dns_sd_unregister_service() runtime functions.
 *   The macro creates a const struct dns_sd_rec in the .dns_sd linker section
 *   whose pointer fields (.instance, .text, .port) point to our mutable runtime
 *   buffers.  Updating the buffer contents is reflected in the next mDNS
 *   advertisement because the responder dereferences the pointers at query time.
 *
 *   To suppress advertising (knx_dns_sd_stop), the instance buffer is zeroed so
 *   the Zephyr responder's validity check (non-empty instance) skips the record.
 *
 * DNS-SD sub-type PTR records (_ia{iid}-{ia}, _{SERIAL}, _pm) are now
 * supported via DNS_SD_REGISTER_SERVICE_SUBTYPE added to Zephyr's dns_sd API.
 * Three additional linker-section records share the same instance/text/port
 * buffers as the primary record; updating the sub-type buffers is reflected
 * in the next mDNS advertisement.
 */

#include <ctype.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include "oc_core_res.h"
#include "port/dns-sd.h"
#include "port/oc_log.h"

#include <zephyr/net/dns_sd.h>
#include <zephyr/net/hostname.h>

/* ── Constants ────────────────────────────────────────────────────────────── */

#define KNX_SERVICE_TYPE   "_knx"
#define KNX_SERVICE_PROTO  "_udp"
#define KNX_COAP_PORT       5683

#define INSTANCE_SIZE       20   /* serial number: max 19 chars + NUL */
#define HOSTNAME_SIZE       32   /* "SERIALNO.knx" + NUL */	// TODO FIXME 24!?

/* TXT record buffer in DNS label wire format.
 * The Zephyr DNS_SD_REGISTER_SERVICE macro computes text_size = sizeof(buf)-1,
 * so the effective size is TXT_BUF_SIZE-1 bytes.  Unused trailing bytes are
 * zeroed; zero-length labels are empty strings per RFC 6763 §6.1 and are
 * silently ignored by DNS-SD clients.
 * "SP=65535" = 8 chars + 1 length byte = 9 bytes; 15 effective bytes is enough. */
#define TXT_BUF_SIZE        16   /* effective text_size = TXT_BUF_SIZE - 1 = 15 */ // TODO FIXME verify size

/*
 * Sub-type label sizes (RFC 6763 §7.1):
 *   _{serial}           : "_" + 12 hex = 13 chars	// TODO FIXME 13 vs "_" + 19 = 20
 *   _ia{iid_hex}-{ia_hex}: up to "_ia" + 8 + "-" + 4 = 16 chars (safe upper bound: 24) // TODO FIXME 16 vs 24
 *   _pm                 : 3 chars
 */
#define SUBTYPE_SERIAL_SIZE  21   /* "_" + serial (≤19 chars) + NUL */
#define SUBTYPE_IA_SIZE      28   /* "_ia" + iid_hex + "-" + ia_hex + NUL */ // TODO FIXME 3+8+1+4+1 = 17
#define SUBTYPE_PM_SIZE       4   /* "_pm" + NUL */

/* ── State ────────────────────────────────────────────────────────────────── */

static char     knx_instance[INSTANCE_SIZE];   /* serial lowercase; empty → suppress */
static char     knx_hostname[HOSTNAME_SIZE];   /* UPPERCASE.knx */
static uint16_t knx_port    = KNX_COAP_PORT;

/* TXT record buffer in DNS label wire format.
 * Starts as all-zero (empty TXT record, per RFC 6763 §6.1).
 * The macro stores sizeof(knx_txt)-1 = TXT_BUF_SIZE-1 as the text_size. */
static char     knx_txt[TXT_BUF_SIZE];

/*
 * Sub-type label buffers.  Zeroing a buffer suppresses the corresponding
 * sub-type record because subtype_is_valid() rejects empty strings.
 */
static char knx_subtype_sn[SUBTYPE_SERIAL_SIZE]; /* "_{serial}"           */
static char knx_subtype_ia[SUBTYPE_IA_SIZE];     /* "_ia{iid}-{ia}"       */
static char knx_subtype_pm[SUBTYPE_PM_SIZE];     /* "_pm" or "" (=off)    */

/*
 * Primary DNS-SD registration.
 * Signature: DNS_SD_REGISTER_SERVICE(id, instance, service, proto, domain,
 *                                    text, port)
 * The record's pointer fields (.instance, .text, .port) point to the mutable
 * runtime buffers above, so updating those buffers updates what is advertised.
 */
DNS_SD_REGISTER_SERVICE(knx_dns_sd,
                        knx_instance,
                        KNX_SERVICE_TYPE,
                        KNX_SERVICE_PROTO,
                        "local",
                        knx_txt,
                        &knx_port);

/*
 * Sub-type registrations (RFC 6763 §7.1).
 * All three share the same instance/text/port pointers as the primary record.
 * Zeroing knx_subtype_* suppresses the corresponding advertisement.
 * Zeroing knx_instance (knx_dns_sd_stop) suppresses all records simultaneously.
 */
DNS_SD_REGISTER_SERVICE_SUBTYPE(knx_dns_sd_sn,
                                knx_instance,
                                KNX_SERVICE_TYPE,
                                KNX_SERVICE_PROTO,
                                "local",
                                knx_txt,
                                &knx_port,
                                knx_subtype_sn);

DNS_SD_REGISTER_SERVICE_SUBTYPE(knx_dns_sd_ia,
                                knx_instance,
                                KNX_SERVICE_TYPE,
                                KNX_SERVICE_PROTO,
                                "local",
                                knx_txt,
                                &knx_port,
                                knx_subtype_ia);

DNS_SD_REGISTER_SERVICE_SUBTYPE(knx_dns_sd_pm,
                                knx_instance,
                                KNX_SERVICE_TYPE,
                                KNX_SERVICE_PROTO,
                                "local",
                                knx_txt,
                                &knx_port,
                                knx_subtype_pm);

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static void str_to_lower(char *dst, const char *src, size_t max)
{
    size_t i;

    for (i = 0; i < max - 1 && src[i] != '\0'; i++) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

static void str_to_upper(char *dst, const char *src, size_t max)
{
    size_t i;

    for (i = 0; i < max - 1 && src[i] != '\0'; i++) {
        dst[i] = (char)toupper((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

/* Encode a single key=value string into DNS label wire format in dst[].
 * Returns the number of bytes written (including the length byte). */
static size_t encode_txt_label(char *dst, size_t dst_max, const char *kv)
{
    size_t kv_len = strlen(kv);

    if (kv_len == 0 || kv_len + 1 > dst_max || kv_len > 255) {
        dst[0] = 0;
        return 1;
    }
    dst[0] = (char)kv_len;
    memcpy(dst + 1, kv, kv_len);
    return kv_len + 1;
}

/* ── Public interface ─────────────────────────────────────────────────────── */

int knx_dns_sd_update_service(char *serial_no, uint64_t iid, uint16_t ia, bool pm)
{
    (void)serial_no;

    OC_DBG("DNS-SD: Publish KNX service: serial_no=%s iid=%" PRIx64 " ia=%04x pm=%d.",
           serial_no ? serial_no : "(null)", iid, (unsigned int)ia, (int)pm);

#ifdef OC_DNS_SD
    oc_device_info_t *device = oc_core_get_device_info();

    if (!device) {
        OC_ERR("DNS-SD: Unable to publish KNX DNS-SD service, device info not available!");
        return -1;
    }

    const char *sn = oc_string(device->serialnumber);
    OC_DBG("DNS-SD: Serial number from device info: %s.", sn ? sn : "(null)");

    /* Instance name: lowercase serial number.
     * Writing to knx_instance updates the live DNS-SD advertisement because
     * the registered dns_sd_rec.instance pointer points here. */
    str_to_lower(knx_instance, sn, sizeof(knx_instance));
    OC_DBG("DNS-SD: Instance (lowercase): \"%s\".", knx_instance);

    /* mDNS host name: UPPERCASE.knx  (matches OpenThread SRP convention) */
    char sn_upper[INSTANCE_SIZE];
    str_to_upper(sn_upper, sn, sizeof(sn_upper));
    snprintf(knx_hostname, sizeof(knx_hostname), "%s.knx", sn_upper);
    OC_DBG("DNS-SD: Setting hostname to \"%s\".", knx_hostname);
    net_hostname_set(knx_hostname, strlen(knx_hostname));

    /* Serial-number sub-type: _{serial}._sub._knx._udp.local */
    snprintf(knx_subtype_sn, sizeof(knx_subtype_sn), "_%s", knx_instance);
    OC_INF("DNS-SD: Serial number sub-type registered: %s._sub._knx._udp.local -> %s._knx._udp.local.", knx_subtype_sn, knx_instance);

    /* Installation-address sub-type: _ia{iid_hex}-{ia_hex}._sub._knx._udp.local
     * Use 0 / 0 when the device has not been commissioned yet (iid == 0 && ia == 0). */
    if (iid != 0U || ia != 0U) {
        snprintf(knx_subtype_ia, sizeof(knx_subtype_ia),
                 "_ia%" PRIx64 "-%x", iid, (unsigned int)ia);
        OC_INF("DNS-SD: IA sub-type registered: %s._sub._knx._udp.local -> %s._knx._udp.local.", knx_subtype_ia, knx_instance);
    } else {
        OC_INF("DNS-SD: IA sub-type not registered (device not commissioned).");
        memset(knx_subtype_ia, 0, sizeof(knx_subtype_ia));
    }

    /* Programming-mode sub-type: _pm._sub._knx._udp.local (only when active) */
    if (pm) {
        memcpy(knx_subtype_pm, "_pm", sizeof("_pm"));
        OC_INF("DNS-SD: PM sub-type registered: _pm._sub._knx._udp.local -> %s._knx._udp.local.", knx_instance);
    } else {
        OC_INF("DNS-SD: PM sub-type not registered (programming mode inactive).");
        memset(knx_subtype_pm, 0, sizeof(knx_subtype_pm));
    }

    OC_INF("DNS-SD: KNX service registered: %s._knx._udp.local port %d.",
           knx_instance, knx_port);
#else
    OC_WRN("DNS-SD: OC_DNS_SD not defined, DNS-SD service registration is disabled!");
#endif /* OC_DNS_SD */
    return 0;
}

uint16_t knx_dns_sd_get_used_port(void)
{
    return KNX_COAP_PORT;
}

void knx_dns_sd_set_sleep_period(int sp)
{
#ifdef OC_DNS_SD
    OC_DBG("DNS-SD: Updating TXT record sleep period: SP=%d.", sp);
    if (sp > 0) {
        char kv[16];
        snprintf(kv, sizeof(kv), "SP=%d", sp);
        /* Encode into the start of knx_txt; zero-pad the rest. */
        size_t used = encode_txt_label(knx_txt, sizeof(knx_txt), kv);
        memset(knx_txt + used, 0, sizeof(knx_txt) - used);
        OC_DBG("DNS-SD: TXT record set to \"%s\" (%zu bytes encoded).", kv, used);
    } else {
        OC_DBG("DNS-SD: TXT record cleared (no sleep period).");
        memset(knx_txt, 0, sizeof(knx_txt));
    }
    /* No re-registration needed: the dns_sd_rec.text pointer already points to
     * knx_txt, so the updated buffer is used on the next mDNS response. */
    OC_INF("DNS-SD: Sleep period set to %d.", sp);
#else
    (void)sp;
#endif
}

void knx_dns_sd_stop(void)
{
#ifdef OC_DNS_SD
    OC_DBG("DNS-SD: Stopping service advertisements (zeroing instance and sub-type buffers).");
    /* Zero the instance name.  Zephyr's rec_is_valid() skips records with
     * an empty instance string, suppressing further DNS-SD advertisements
     * for both the primary record and all sub-type records (they share
     * the same knx_instance pointer). */
    memset(knx_instance, 0, sizeof(knx_instance));
    /* Also suppress sub-type records explicitly. */
    memset(knx_subtype_sn, 0, sizeof(knx_subtype_sn));
    memset(knx_subtype_ia, 0, sizeof(knx_subtype_ia));
    memset(knx_subtype_pm, 0, sizeof(knx_subtype_pm));
    OC_INF("DNS-SD: Service advertisements stopped.");
#endif
}
