/*
 * Copyright (c) 2022 Cascoda Ltd.
 * Copyright (c) 2026 NXP
 * Copyright (c) 2026 KNX Association
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This file was created with the assistance of AI tools.
 * Copyright 2026 KNXA (to the extent protectable under applicable law)
 */

/**
 * @file
 * @brief KNX IoT DNS-SD over Thread, implemented with the OpenThread SRP client.
 *
 * On Thread the device does not run an mDNS responder. It registers its
 * service with the Border Router's SRP server (unicast DNS Update) and the
 * Border Router's advertising proxy publishes it as mDNS on the backbone
 * link. This file keeps the externally visible behaviour identical to the
 * socket port (port/dns-sd_mdns.c):
 *
 *   - service _knx._udp, instance <sn>, host knx-<sn>, port = the UDP port
 *     the KNX unicast socket is actually bound to
 *   - subtypes _<sn>, _ia<iid>-<ia> and, while in programming mode, _pm
 *   - TXT record: "SP=<seconds>" when a sleep period is set, otherwise an
 *     empty TXT record (RFC 6763 clause 6.1; the SRP client emits the single
 *     zero byte itself when no entries are given)
 *   - knx_dns_sd_set_sleep_period() re-publishes immediately
 *   - knx_dns_sd_stop() deregisters host and service at the server, the SRP
 *     equivalent of the mDNS goodbye; a later update re-registers
 *
 * SRP forbids modifying a service entry while it is in the client's list, so
 * changes are staged in `pending` and applied when the entry is out of the
 * list. All OpenThread API calls from the KNX stack thread run under the
 * OpenThread API mutex; the SRP callback already runs on the OpenThread
 * thread with that mutex held.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "oc_core_res.h"
#include "oc_endpoint.h"
#include "port/dns-sd.h"
#include "port/oc_log.h"
#include "port/zephyr/connectivity_thread.h"

#include <openthread.h>
#include <openthread/dns.h>
#include <openthread/srp_client.h>
#include <openthread/srp_client_buffers.h>

#include "api/oc_main.h"

#define KNX_SRP_SERVICE_NAME "_knx._udp"

/* subtype labels: "_" + serial number, "_ia" + iid + "-" + ia, oc_knx.h */
#define SUBTYPE_SN_SIZE (1 + SN_STR_LEN_MAX + 1)
#define SUBTYPE_IA_SIZE (3 + IID_STR_LEN_MAX + 1 + IA_STR_LEN_MAX + 1)
/* "SP=" + up to 5 decimal digits */
#define SP_VALUE_SIZE 8

/** Values the service should advertise; written by the KNX thread, applied
 *  to the SRP entry only while the entry is not in the client's list. */
typedef struct
{
    uint64_t iid;
    uint16_t ia;
    bool pm;
    uint16_t sp;
} knx_srp_values_t;

static otSrpClientBuffersServiceEntry *entry = NULL;  /**< SRP service entry, allocated once */
static char hostname[HNAME_MAX_SIZE + 1];             /**< host name label handed to SRP, domain appended by SRP */
static char pending_hostname[HNAME_MAX_SIZE + 1];     /**< most recent device host name, taken over once SRP released hostname */
static char subtype_sn[SUBTYPE_SN_SIZE];              /**< "_<sn>" */
static char subtype_ia[SUBTYPE_IA_SIZE];              /**< "_ia<iid>-<ia>" */
static const char *subtypes[4];                       /**< NULL-terminated label array for the entry */
static char sp_value[SP_VALUE_SIZE];                  /**< "<seconds>" for the SP TXT entry */
static otDnsTxtEntry txt_sp;                          /**< key "SP", value sp_value */

static knx_srp_values_t pending;   /**< most recent values requested by the stack */
static bool reapply = false;       /**< pending values must still be applied to the entry */
static bool stopped = true;        /**< knx_dns_sd_stop() called, nothing should be registered */

/* ---------------------------------------------------------------- helpers */

static bool entry_in_list(otInstance *inst)
{
    for (const otSrpClientService *s = otSrpClientGetServices(inst); s != NULL; s = s->mNext)
    {
        if (s == &entry->mService)
        {
            return true;
        }
    }
    return false;
}

/** Copy `pending` into the SRP entry. Only valid while the entry is not in the list. */
static void apply_pending(void)
{
    (void)snprintf(subtype_ia, sizeof(subtype_ia), "_ia%llx-%x", (unsigned long long)pending.iid, pending.ia);

    subtypes[0] = subtype_ia;
    subtypes[1] = subtype_sn;
    subtypes[2] = pending.pm ? "_pm" : NULL;
    subtypes[3] = NULL;
    entry->mService.mSubTypeLabels = subtypes;

    if (pending.sp != 0)
    {
        (void)snprintf(sp_value, sizeof(sp_value), "%u", (unsigned)pending.sp);
        txt_sp.mKey = "SP";
        txt_sp.mValue = (const uint8_t *)sp_value;
        txt_sp.mValueLength = (uint16_t)strlen(sp_value);
        entry->mService.mTxtEntries = &txt_sp;
        entry->mService.mNumTxtEntries = 1;
    }
    else
    {
        /* no entries: the SRP client appends the mandatory empty TXT record */
        entry->mService.mTxtEntries = NULL;
        entry->mService.mNumTxtEntries = 0;
    }

    entry->mService.mPort = oc_connectivity_thread_get_port();
    entry->mService.mPriority = 0;
    entry->mService.mWeight = 0;
    entry->mService.mNext = NULL;

    reapply = false;
}

/** Make sure the SRP client knows the host name; false when the host is
 *  still being removed and the caller must wait for the callback. */
static bool ensure_host(otInstance *inst)
{
    const otSrpClientHostInfo *host = otSrpClientGetHostInfo(inst);

    /*
      the SRP host state alone tells whether the host name is in use, no own flag needed:
      - otSrpClientSetHostName() moves the host from REMOVED to TO_ADD (OpenThread srp_client.cpp, Client::SetHostName)
      - knx_dns_sd_stop() moves it to TO_REMOVE / REMOVING, and to REMOVED once the server confirmed
      so any state but REMOVED means named: registered or on the way (true), or still being removed (false, wait for
      the callback)
    */
    if (host->mState != OT_SRP_CLIENT_ITEM_STATE_REMOVED)
    {
        return host->mState != OT_SRP_CLIENT_ITEM_STATE_TO_REMOVE &&
               host->mState != OT_SRP_CLIENT_ITEM_STATE_REMOVING;
    }

    /*
      SRP keeps the pointer to hostname until the host is removed, so a host name changed since (set and store, factory
      reset; knx_dns_sd_stop() first) is taken over only once the removal completed
      - no state check needed here: the return above leaves only REMOVED, so SRP no longer uses hostname
      - copied from pending_hostname, never from device->iot_hostname: this mostly runs in srp_client_callback on the
        OpenThread thread, while the KNX thread may free and reallocate iot_hostname (oc_core_set_device_hostname)
        outside the OpenThread mutex
    */
    memcpy(hostname, pending_hostname, sizeof(hostname));

    otError err = otSrpClientSetHostName(inst, hostname);
    if (err == OT_ERROR_INVALID_STATE)
    {
        /* not expected, the host is REMOVED here; safety net, kept as before */
        return host->mState == OT_SRP_CLIENT_ITEM_STATE_REGISTERED ||
               host->mState == OT_SRP_CLIENT_ITEM_STATE_ADDING ||
               host->mState == OT_SRP_CLIENT_ITEM_STATE_TO_ADD;
    }
    if (err != OT_ERROR_NONE)
    {
        OC_ERR("DNS-SD: SRP set host name failed: %d", err);
        return false;
    }
    (void)otSrpClientEnableAutoHostAddress(inst);
    return true;
}

/** Apply pending values and hand the entry to the SRP client. Entry must be out of the list. */
static void add_service(otInstance *inst)
{
    apply_pending();

    otError err = otSrpClientAddService(inst, &entry->mService);
    if (err != OT_ERROR_NONE)
    {
        OC_ERR("DNS-SD: SRP add service failed: %d", err);
        reapply = true;   /* retry on the next callback or update */
        return;
    }
    OC_INF("DNS-SD: SRP registering %s.%s port %u ia %x pm %d sp %u", entry->mService.mInstanceName,
           KNX_SRP_SERVICE_NAME, entry->mService.mPort, pending.ia, pending.pm, pending.sp);
}

/** Publish `pending`: add the entry if it is free, otherwise start its removal
 *  and let the callback re-add it. Caller holds the OpenThread mutex. */
static void publish(otInstance *inst)
{
    reapply = true;

    if (!ensure_host(inst))
    {
        return;   /* callback continues once the host removal is done */
    }

    if (!entry_in_list(inst))
    {
        add_service(inst);
        return;
    }

    switch (entry->mService.mState)
    {
    case OT_SRP_CLIENT_ITEM_STATE_REGISTERED:
    case OT_SRP_CLIENT_ITEM_STATE_TO_REFRESH:
    case OT_SRP_CLIENT_ITEM_STATE_REFRESHING:
        (void)otSrpClientRemoveService(inst, &entry->mService);
        break;
    default:
        /* TO_ADD / ADDING: wait for registration, then remove and re-add
         * TO_REMOVE / REMOVING: removal already on its way */
        break;
    }
}

/* -------------------------------------------------------------- callbacks */

static void srp_client_callback(otError err, const otSrpClientHostInfo *host, const otSrpClientService *services,
                                const otSrpClientService *removed, void *ctx)
{
    (void)services;
    (void)ctx;
    otInstance *inst = openthread_get_default_instance();

    if (err != OT_ERROR_NONE)
    {
        OC_ERR("DNS-SD: SRP update failed: %d", err);
        if (reapply && !stopped && entry_in_list(inst) && (entry->mService.mState == OT_SRP_CLIENT_ITEM_STATE_REMOVING))
        {
            /* the server did not confirm the removal; drop the entry locally and re-add */
            (void)otSrpClientClearService(inst, &entry->mService);
            add_service(inst);
        }
        return;
    }

    for (const otSrpClientService *s = removed; s != NULL; s = s->mNext)
    {
        if (s == &entry->mService)
        {
            OC_INF("DNS-SD: SRP service deregistered.");
        }
    }

    if (stopped)
    {
        if (host->mState == OT_SRP_CLIENT_ITEM_STATE_REMOVED)
        {
            OC_INF("DNS-SD: SRP host deregistered.");
        }
        return;
    }

    if (reapply)
    {
        publish(inst);   /* host removed -> re-name; entry free -> add; else keep removing */
        return;
    }

    if (entry_in_list(inst) && (entry->mService.mState == OT_SRP_CLIENT_ITEM_STATE_REGISTERED))
    {
        OC_INF("DNS-SD: SRP registration successful.");
    }
}

static void srp_autostart_callback(const otSockAddr *server, void *ctx)
{
    (void)ctx;
    OC_INF("DNS-SD: SRP server %s.", server != NULL ? "found" : "lost");
}

/* ------------------------------------------------------------- public API */

int knx_dns_sd_update_service(const oc_device_info_t *device)
{
    otInstance *inst = openthread_get_default_instance();

   if (!device || !device->serialnumber)
    {
      OC_WRN("DNS-SD: Invalid (NULL) device or serial number provided, cannot update service.");
      return -1;
    }  
  
   if (!inst)
    {
        OC_WRN("DNS-SD: OpenThread not initialised yet!");
        return -1;
    }

    /*
       The advertisement depends on device data the stack loads from storage during its
       initialisation (see oc_knx_load_device()). Until then nothing is announced and the
       one-time setup below never sees an unset serial number. The registration at the end of
       oc_main_init() is the first one.

     */
    if (!oc_main_initialized())
    {
        OC_DBG("DNS-SD: Device data not loaded yet, the service update is postponed.");
        return 0;
    }

    openthread_mutex_lock();

    pending.iid = device->iid;
    pending.ia = device->ia;
    pending.pm = device->pm;
    stopped = false;

    // the device host name CAN BE CHANGED at runtime (therefore we need to set here always)
    // it is a label only, such as "knx-<sn>", the SRP client appends the domain itself
    /*
      copied to pending_hostname, not to hostname:
      - SRP keeps the pointer to hostname until the host is removed, a rename (knx_dns_sd_stop() first) arrives here
        while the old host is still being removed; ensure_host() takes it over once the host is REMOVED
      - copied here under the OpenThread mutex on the KNX thread, so the SRP callback reads a stable copy instead of
        iot_hostname, which the KNX thread may free and reallocate
    */
    (void)snprintf(pending_hostname, sizeof(pending_hostname), "%s", oc_string_checked(device->iot_hostname));

    if (entry == NULL)
    {
        /* one-time setup: static subtype, SRP entry and callbacks */
        (void)snprintf(subtype_sn, sizeof(subtype_sn), "_%s", device->serialnumber);

        entry = otSrpClientBuffersAllocateService(inst);
        if (entry == NULL)
        {
            openthread_mutex_unlock();
            OC_ERR("DNS-SD: no SRP service buffer available!");
            return -1;
        }
        uint16_t size;
        char *name = otSrpClientBuffersGetServiceEntryServiceNameString(entry, &size);
        (void)snprintf(name, size, "%s", KNX_SRP_SERVICE_NAME);
        name = otSrpClientBuffersGetServiceEntryInstanceNameString(entry, &size);
        (void)snprintf(name, size, "%s", device->serialnumber);

        otSrpClientSetCallback(inst, srp_client_callback, NULL);
        otSrpClientEnableAutoStartMode(inst, srp_autostart_callback, NULL);
    }

    publish(inst);

    openthread_mutex_unlock();
    return 0;
}

void knx_dns_sd_set_sleep_period(uint16_t sp)
{
    otInstance *inst = openthread_get_default_instance();

    openthread_mutex_lock();

    pending.sp = sp;
    OC_INF("DNS-SD: sleep period set to %u.", (unsigned)sp);

    /* re-publish at once, like the mDNS port re-announces its TXT record */
    if ((entry != NULL) && !stopped && (inst != NULL))
    {
        publish(inst);
    }

    openthread_mutex_unlock();
}

uint16_t knx_dns_sd_get_used_port(void)
{
    return oc_connectivity_thread_get_port();
}

void knx_dns_sd_stop(void)
{
    otInstance *inst = openthread_get_default_instance();

    openthread_mutex_lock();

    stopped = true;
    reapply = false;

    if ((inst == NULL) || (entry == NULL))
    {
        openthread_mutex_unlock();
        return;
    }

    /* SRP goodbye: remove host and services at the server (key lease kept so
     * the name stays ours), a following update re-registers */
    otError err = otSrpClientRemoveHostAndServices(inst, false, true);
    if ((err != OT_ERROR_NONE) && (err != OT_ERROR_ALREADY))
    {
        OC_WRN("DNS-SD: SRP deregistration failed: %d", err);
    }
    openthread_mutex_unlock();
}
