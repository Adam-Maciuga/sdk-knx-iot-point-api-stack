# Zephyr module CMake for the KNX-IoT Point API Stack.
# Included automatically by the Zephyr build system when this stack is
# registered as a module via zephyr/module.yml.
# Mirrors the source file and compile definition logic of the stack's
# top-level CMakeLists.txt and port/CMakeLists.txt for the Zephyr platform.

if(CONFIG_KNXIOT_LIB)

# Resolve the stack root to a clean absolute path.
get_filename_component(STACK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." REALPATH)

# ── External libraries ─────────────────────────────────────────────────────────
# Mirrors the FetchContent logic in port/CMakeLists.txt.

include(FetchContent)

if(CONFIG_KNXIOT_MBEDTLS_BUILTIN)
    # Use the same configuration file as the Linux/Windows build.
    set(MBEDTLS_CONFIG_FILE "${STACK_ROOT}/security/knx_mbedtls_config.h")
    set(UNSAFE_BUILD OFF CACHE BOOL "" FORCE)
    set(MBEDTLS_FATAL_WARNINGS ON CACHE BOOL "" FORCE)
    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
    set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        mbedtls
        URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.5/mbedtls-3.6.5.tar.bz2
        URL_HASH SHA256=4a11f1777bb95bf4ad96721cac945a26e04bf19f57d905f241fe77ebeddf46d8
        DOWNLOAD_EXTRACT_TIMESTAMP ON
    )
    FetchContent_MakeAvailable(mbedtls)

    set(MBEDTLS_INCLUDE_DIRS
        PRIVATE "${STACK_ROOT}"
        PRIVATE "${STACK_ROOT}/include"
        PRIVATE "${STACK_ROOT}/port/zephyr"   # for oc_config.h
    )
    target_include_directories(mbedtls    ${MBEDTLS_INCLUDE_DIRS})
    target_include_directories(mbedcrypto ${MBEDTLS_INCLUDE_DIRS})
    target_include_directories(mbedx509   ${MBEDTLS_INCLUDE_DIRS})

    zephyr_library_link_libraries(mbedtls)
endif()

if(CONFIG_KNXIOT_TINYCBOR_BUILTIN)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_TOOLS ON CACHE BOOL "" FORCE)

    FetchContent_Declare(
        tinycbor
        GIT_REPOSITORY https://github.com/intel/tinycbor.git
        GIT_TAG v7.0
    )
    FetchContent_MakeAvailable(tinycbor)

    zephyr_library_link_libraries(tinycbor)
    zephyr_include_directories(${tinycbor_SOURCE_DIR}/src ${tinycbor_BINARY_DIR})
endif()

# Note:
# mdns header-only library — always required by port/dns-sd.c.
# On Zephyr, CONFIG_MDNS_RESPONDER handles the mDNS responder separately;
# this fetch only provides the mdns.h header used by dns-sd.c.
FetchContent_Declare(
    mdns
    GIT_REPOSITORY https://github.com/mjansson/mdns.git
    GIT_TAG main
    PATCH_COMMAND ${CMAKE_COMMAND} -DPATCH_FILE=${STACK_ROOT}/port/mdns.patch
                  -P "${STACK_ROOT}/cmake/patch.cmake"
)
# Note:
# Use FetchContent_Populate (not MakeAvailable) to download only — the mdns
# library's own CMakeLists.txt builds POSIX example programs that do not
# compile under the Zephyr cross-toolchain.
# We only need the mdns.h header.
FetchContent_GetProperties(mdns)
if(NOT mdns_POPULATED)
    FetchContent_Populate(mdns)
endif()
zephyr_include_directories(${mdns_SOURCE_DIR})

# ── Source files ───────────────────────────────────────────────────────────────

set(API_SOURCES
    ${STACK_ROOT}/api/oc_base64.c
    ${STACK_ROOT}/api/oc_blockwise.c
    ${STACK_ROOT}/api/oc_buffer.c
    ${STACK_ROOT}/api/oc_client_api.c
    ${STACK_ROOT}/api/oc_clock.c
    ${STACK_ROOT}/api/oc_core_res.c
    ${STACK_ROOT}/api/oc_discovery.c
    ${STACK_ROOT}/api/oc_endpoint.c
    ${STACK_ROOT}/api/oc_helpers.c
    ${STACK_ROOT}/api/oc_main.c
    ${STACK_ROOT}/api/oc_network_events.c
    ${STACK_ROOT}/api/oc_rep.c
    ${STACK_ROOT}/api/oc_replay.c
    ${STACK_ROOT}/api/oc_ri.c
    ${STACK_ROOT}/api/oc_server_api.c
    ${STACK_ROOT}/api/oc_session_events.c
    ${STACK_ROOT}/api/oc_uuid.c
    ${STACK_ROOT}/api/oc_knx.c
    ${STACK_ROOT}/api/oc_knx_client.c
    ${STACK_ROOT}/api/oc_knx_dev.c
    ${STACK_ROOT}/api/oc_knx_fb.c
    ${STACK_ROOT}/api/oc_knx_fp.c
    ${STACK_ROOT}/api/oc_knx_helpers.c
    ${STACK_ROOT}/api/oc_knx_p.c
    ${STACK_ROOT}/api/oc_knx_sec.c
    ${STACK_ROOT}/api/oc_knx_swu.c
    ${STACK_ROOT}/api/oc_knx_sub.c
    ${STACK_ROOT}/api/c-timestamp/timestamp_compare.c
    ${STACK_ROOT}/api/c-timestamp/timestamp_format.c
    ${STACK_ROOT}/api/c-timestamp/timestamp_parse.c
    ${STACK_ROOT}/api/c-timestamp/timestamp_tm.c
    ${STACK_ROOT}/api/c-timestamp/timestamp_valid.c
)

set(CORE_SOURCES
    ${STACK_ROOT}/util/oc_etimer.c
    ${STACK_ROOT}/util/oc_list.c
    ${STACK_ROOT}/util/oc_memb.c
    ${STACK_ROOT}/util/oc_mem_trace.c
    ${STACK_ROOT}/util/oc_mmem.c
    ${STACK_ROOT}/util/oc_process.c
    ${STACK_ROOT}/util/oc_timer.c
    # Security
    ${STACK_ROOT}/security/oc_oscore_context.c
    ${STACK_ROOT}/security/oc_oscore_crypto.c
    ${STACK_ROOT}/security/oc_oscore_engine.c
    ${STACK_ROOT}/security/oc_spake2plus.c
    ${STACK_ROOT}/security/oc_tls.c
)

set(COAP_SOURCES
    ${STACK_ROOT}/messaging/coap/coap.c
    ${STACK_ROOT}/messaging/coap/coap_signal.c
    ${STACK_ROOT}/messaging/coap/engine.c
    ${STACK_ROOT}/messaging/coap/observe.c
    ${STACK_ROOT}/messaging/coap/oscore.c
    ${STACK_ROOT}/messaging/coap/separate.c
    ${STACK_ROOT}/messaging/coap/transactions.c
)

# Platform port — common
set(PORT_SOURCES
    ${STACK_ROOT}/port/oc_log.c
)

# Platform port — Zephyr-specific
list(APPEND PORT_SOURCES
    ${STACK_ROOT}/port/zephyr/abort.c
    ${STACK_ROOT}/port/zephyr/clock.c
    ${STACK_ROOT}/port/zephyr/random.c
    ${STACK_ROOT}/port/zephyr/storage.c
)

# Network backend selection:
#   OpenThread  — connectivity_thread.c  +  dns-sd-thread.c  (OT SRP)
#   Wi-Fi       — connectivity_wifi.c    +  dns-sd-wifi.c    (Zephyr BSD sockets + mDNS/DNS-SD)
# Note: dns-sd-thread.c uses OpenThread SRP client instead of POSIX mDNS sockets.
# Note: connectivity_thread.c is the OpenThread equivalent of ipadapter.c —
#       provides oc_connectivity_init, oc_send_buffer, etc.
# Note: connectivity_wifi.c + dns-sd-wifi.c are the Wi-Fi equivalents using
#       Zephyr BSD sockets and the Zephyr DNS-SD / mDNS responder API.
if(CONFIG_NET_L2_OPENTHREAD)
    list(APPEND PORT_SOURCES
        ${STACK_ROOT}/port/zephyr/connectivity_thread.c
        ${STACK_ROOT}/port/zephyr/dns-sd-thread.c
    )
elseif(CONFIG_WIFI)
    list(APPEND PORT_SOURCES
        ${STACK_ROOT}/port/zephyr/connectivity_wifi.c
        ${STACK_ROOT}/port/zephyr/dns-sd-wifi.c
    )
else()
    message(FATAL_ERROR
        "KNX-IoT stack: No supported network backend selected. "
        "Enable CONFIG_NET_L2_OPENTHREAD (Thread) or CONFIG_WIFI (Wi-Fi).")
endif()

zephyr_library_named(knx_iot_stack)
zephyr_library_sources(
    ${API_SOURCES}
    ${CORE_SOURCES}
    ${COAP_SOURCES}
    ${PORT_SOURCES}
)

# ── Include directories ────────────────────────────────────────────────────────
# Mirrors KNX_INCLUDE_DIRS from the stack's top-level CMakeLists.txt.
# Note: some are marked TODO in the stack as not intended to be public.
zephyr_include_directories(
    ${STACK_ROOT}
    ${STACK_ROOT}/include
    ${STACK_ROOT}/port/zephyr        # provides oc_config.h for this platform
    ${STACK_ROOT}/security           # TODO: should not be public
    ${STACK_ROOT}/messaging/coap     # TODO: should not be public
    ${STACK_ROOT}/api                # TODO: should not be public
    ${STACK_ROOT}/api/c-timestamp    # TODO: should not be public
)

# ── Compile definitions ────────────────────────────────────────────────────────
# Note: the internal compile definition names differ from the CMake variable
# names (e.g. OC_PRINT_ENABLED → OC_PRINT, OC_REPLAY_PROTECTION_ENABLED → OC_REPLAY_PROTECTION).
#
# zephyr_compile_definitions() (not zephyr_library_compile_definitions()) is
# used so that definitions propagate to ALL targets, including the application.
# This is necessary because the stack headers use #ifdef OC_DYNAMIC_ALLOCATION
# etc. to control their API, so any source file that includes a stack header
# must be compiled with the same set of definitions.

## Protocol
zephyr_compile_definitions(
    $<$<BOOL:${CONFIG_KNXIOT_DYNAMIC_ALLOCATION}>:OC_DYNAMIC_ALLOCATION>
    $<$<BOOL:${CONFIG_KNXIOT_CLIENT}>:OC_CLIENT>
    $<$<BOOL:${CONFIG_KNXIOT_SERVER}>:OC_SERVER>
    $<$<BOOL:${CONFIG_KNXIOT_PUBLISHER_TABLE}>:OC_PUBLISHER_TABLE>
)

## Table sizes
zephyr_compile_definitions(
    GOT_MAX_ENTRIES=${CONFIG_KNXIOT_GOT_MAX_ENTRIES}
    GPT_MAX_ENTRIES=${CONFIG_KNXIOT_GPT_MAX_ENTRIES}
    GRT_MAX_ENTRIES=${CONFIG_KNXIOT_GRT_MAX_ENTRIES}
    G_AT_MAX_ENTRIES=${CONFIG_KNXIOT_SEC_MAX_ENTRIES}
    PAGE_SIZE=${CONFIG_KNXIOT_PAGE_SIZE}
)

## Network
zephyr_compile_definitions(
    $<$<BOOL:${CONFIG_KNXIOT_DNS_SD}>:OC_DNS_SD>
    OC_SENDER_MULTICAST_SCOPE=${CONFIG_KNXIOT_SENDER_MULTICAST_SCOPE}
    KNX_RESPONSE_TIME_SECONDS=${CONFIG_KNXIOT_RESET_PROCESS_TIME}
)

## Security
zephyr_compile_definitions(
    OC_SPAKE # TODO: verify whether OC_SPAKE should always be on or be conditional
    OC_SPAKE_IT=${CONFIG_KNXIOT_SPAKE_ITERATIONS}
    $<$<BOOL:${CONFIG_KNXIOT_REPLAY_PROTECTION}>:OC_REPLAY_PROTECTION>
    $<$<BOOL:${CONFIG_KNXIOT_TLS_PKI}>:OC_PKI_ENABLED>
)

## Logging
# OC_PRINT_ENABLED → OC_PRINT, OC_DEBUG_ENABLED → OC_DEBUG (internal names).
if(CONFIG_KNXIOT_LOG_LEVEL GREATER 0)
    zephyr_compile_definitions(OC_PRINT)
endif()
if(CONFIG_KNXIOT_LOG_LEVEL GREATER_EQUAL 4)
    zephyr_compile_definitions(OC_DEBUG)
endif()
zephyr_library_compile_definitions(
    $<$<BOOL:${CONFIG_KNXIOT_DEBUG_OSCORE}>:OC_DEBUG_OSCORE>
    $<$<BOOL:${CONFIG_KNXIOT_LOG_CAST_64_BIT_INTS}>:KNX_LOG_CAST_64_BIT_INTS>
)

endif() # CONFIG_KNXIOT_LIB
