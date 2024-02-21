cmake_minimum_required (VERSION 3.15)
project(knx-iot-stack) 

# compiler options 
if(CMAKE_COMPILER_IS_GNUCC AND UNIX)
  # emit position-independent code, see https://gcc.gnu.org/onlinedocs/gcc/Code-Gen-Options.html    
  add_compile_options(-fPIC)
endif()

if(CMAKE_COMPILER_IS_GNUCC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wmissing-declarations -Wmissing-prototypes")
endif(CMAKE_COMPILER_IS_GNUCC)

if(CMAKE_COMPILER_IS_CLANG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wmissing-declarations -Wmissing-prototypes -Wdocumentation -Wno-documentation-deprecated-sync -Wunreachable-code")
endif(CMAKE_COMPILER_IS_CLANG)

if(MSVC)
  # add command line definitiosn for the target in the current directory
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
  # add compile options for the target in the current directory and below
  add_compile_options(/W1 /WX-)
endif()

# add preprocessor definitiosn for the current CMakeLists file
add_compile_definitions(OC_DYNAMIC_ALLOCATION OC_CLIENT OC_SERVER)

# Stack specific variables 
set(OC_DNS_SD_ENABLED OFF CACHE BOOL "Enable DNS SD")
set(OC_PUBLISHER_TABLE_ENABLED ON CACHE BOOL "enable Publisher table")
set(OC_OSCORE_ENABLED ON CACHE BOOL "Enable oscore")
set(OC_IOT_ROUTER_ENABLED OFF CACHE BOOL "Enable IoT Router code")
set(OC_PKI_ENABLED OFF CACHE BOOL "Enable PKI security")
set(OC_PRINT_ENABLED ON CACHE BOOL "Enable print messages. Setting this to OFF will disable virtually all logging.")
set(OC_PRINT_APP_ENABLED ON CACHE BOOL "Enable application log messages. Independent from OC_PRINT_ENABLED")
set(OC_DEBUG_ENABLED ON CACHE BOOL "Enable debug messages")
set(OC_DEBUG_OSCORE_ENABLED OFF CACHE BOOL "Enable security debug messages")
set(OC_LOG_TO_FILE_ENABLED OFF CACHE BOOL "redirect debug messages to file")
set(CLANG_TIDY_ENABLED OFF CACHE BOOL "Enable clang-tidy analysis during compilation.")
set(OC_USE_STORAGE ON CACHE BOOL "Persistent storage of data.")
set(OC_USE_MULTICAST_SCOPE_2 ON CACHE BOOL "devices send also group multicast events with scope2.")
set(OC_REPLAY_PROTECTION_ENABLED OFF CACHE BOOL "Enable replay protection using the Echo option")
set(OC_TRUST_FIRST_MCAST_ENABLED ON CACHE BOOL "Trust first multicast message from an unsynchronised client")

# KNX specific variables
set(KNX_LOG_CAST_64_BIT_INTS_ENABLED OFF CACHE BOOL "Cast int64_t and uint64_t values to (int) if platform doesn't support 64 bit int format specifiers")
set(KNX_GAMT_MAX_ENTRIES "" CACHE STRING "Maximum number of group address mapping table entries")
set(KNX_GOT_MAX_ENTRIES "" CACHE STRING "Maximum number of group object table entries")
set(KNX_GPT_MAX_ENTRIES "" CACHE STRING "Maximum number of group publisher table entries")
set(KNX_GRT_MAX_ENTRIES "" CACHE STRING "Maximum number of group recipient table entries")
set(KNX_SEC_MAX_ENTRIES "" CACHE STRING "Maximum number of group security table (auth/at) entries")

set(KNX_SPAKE_MIN_IT "1000" CACHE STRING "Minimum number of SHA256 iterations used within the SPAKE2+ handshake")
set(KNX_PAGE_SIZE "" CACHE STRING "Default page size for pagination")
set(KNX_SPAKE_MAX_IT "100000" CACHE STRING "Maximum number of SHA256 iterations used within the SPAKE2+ handshake")

# dont show below variables in any (command) GUI
mark_as_advanced(KNX_SPAKE_MIN_IT KNX_SPAKE_MAX_IT)

set(API_SOURCES
    ${PROJECT_SOURCE_DIR}/api/oc_base64.c
    ${PROJECT_SOURCE_DIR}/api/oc_blockwise.c
    ${PROJECT_SOURCE_DIR}/api/oc_buffer.c
    ${PROJECT_SOURCE_DIR}/api/oc_client_api.c
    ${PROJECT_SOURCE_DIR}/api/oc_clock.c
    ${PROJECT_SOURCE_DIR}/api/oc_core_res.c
    ${PROJECT_SOURCE_DIR}/api/oc_device_mode.c
    ${PROJECT_SOURCE_DIR}/api/oc_discovery.c
    ${PROJECT_SOURCE_DIR}/api/oc_endpoint.c
    ${PROJECT_SOURCE_DIR}/api/oc_helpers.c
    ${PROJECT_SOURCE_DIR}/api/oc_main.c
    ${PROJECT_SOURCE_DIR}/api/oc_network_events.c
    ${PROJECT_SOURCE_DIR}/api/oc_rep.c
    ${PROJECT_SOURCE_DIR}/api/oc_replay.c
    ${PROJECT_SOURCE_DIR}/api/oc_ri.c
    ${PROJECT_SOURCE_DIR}/api/oc_server_api.c
    ${PROJECT_SOURCE_DIR}/api/oc_session_events.c
    ${PROJECT_SOURCE_DIR}/api/oc_uuid.c
    ${PROJECT_SOURCE_DIR}/api/oc_knx.c
    ${PROJECT_SOURCE_DIR}/api/oc_knx_client.c
    ${PROJECT_SOURCE_DIR}/api/oc_knx_dev.c
    ${PROJECT_SOURCE_DIR}/api/oc_knx_fb.c
    ${PROJECT_SOURCE_DIR}/api/oc_knx_fp.c
    ${PROJECT_SOURCE_DIR}/api/oc_knx_gm.c
    ${PROJECT_SOURCE_DIR}/api/oc_knx_helpers.c
    ${PROJECT_SOURCE_DIR}/api/oc_knx_p.c
    ${PROJECT_SOURCE_DIR}/api/oc_knx_sec.c
    ${PROJECT_SOURCE_DIR}/api/oc_knx_swu.c
    ${PROJECT_SOURCE_DIR}/api/oc_knx_sub.c
    ${PROJECT_SOURCE_DIR}/api/c-timestamp/timestamp_compare.c
    ${PROJECT_SOURCE_DIR}/api/c-timestamp/timestamp_format.c
    ${PROJECT_SOURCE_DIR}/api/c-timestamp/timestamp_parse.c
    ${PROJECT_SOURCE_DIR}/api/c-timestamp/timestamp_tm.c
    ${PROJECT_SOURCE_DIR}/api/c-timestamp/timestamp_valid.c
)

set(CBOR_SOURCES
     ${PROJECT_SOURCE_DIR}/deps/tinycbor/src/cborerrorstrings.c
     ${PROJECT_SOURCE_DIR}/deps/tinycbor/src/cborencoder.c
     ${PROJECT_SOURCE_DIR}/deps/tinycbor/src/cborencoder_close_container_checked.c
     ${PROJECT_SOURCE_DIR}/deps/tinycbor/src/cborparser.c
     ${PROJECT_SOURCE_DIR}/deps/tinycbor/src/cborpretty.c
)

set(CRYPTO_SOURCES
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/aes.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/aesni.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/arc4.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/aria.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/asn1parse.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/asn1write.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/base64.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/bignum.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/blowfish.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/camellia.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ccm.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/chacha20.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/chachapoly.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/cipher.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/cipher_wrap.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/cmac.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ctr_drbg.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/des.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/dhm.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ecdh.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ecdsa.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ecjpake.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ecp.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ecp_curves.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/entropy.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/entropy_poll.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/error.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/gcm.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/havege.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/hkdf.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/hmac_drbg.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/md.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/md2.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/md4.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/md5.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/md_wrap.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/memory_buffer_alloc.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/nist_kw.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/oid.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/padlock.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/pem.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/pk.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/pk_wrap.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/pkcs12.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/pkcs5.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/pkparse.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/pkwrite.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/platform.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/platform_util.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/poly1305.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ripemd160.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/rsa.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/rsa_internal.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/sha1.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/sha256.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/sha512.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/threading.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/timing.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/version.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/version_features.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/xtea.c
)

set(X509_SOURCES
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/certs.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/pkcs11.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/x509.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/x509_create.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/x509_crl.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/x509_crt.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/x509_csr.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/x509write_crt.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/x509write_csr.c
)

set(TLS_SOURCES
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/debug.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/net_sockets.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ssl_cache.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ssl_ciphersuites.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ssl_cli.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ssl_cookie.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ssl_srv.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ssl_ticket.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ssl_tls.c
)

set(CORE_SOURCES
    # Utilities that are used deep within the stack
    ${PROJECT_SOURCE_DIR}/util/oc_etimer.c
    ${PROJECT_SOURCE_DIR}/util/oc_list.c
    ${PROJECT_SOURCE_DIR}/util/oc_memb.c
    ${PROJECT_SOURCE_DIR}/util/oc_mem_trace.c
    ${PROJECT_SOURCE_DIR}/util/oc_mmem.c
    ${PROJECT_SOURCE_DIR}/util/oc_process.c
    ${PROJECT_SOURCE_DIR}/util/oc_timer.c
    # Security
    ${PROJECT_SOURCE_DIR}/security/oc_oscore_context.c
    ${PROJECT_SOURCE_DIR}/security/oc_oscore_crypto.c
    ${PROJECT_SOURCE_DIR}/security/oc_oscore_engine.c
    ${PROJECT_SOURCE_DIR}/security/oc_spake2plus.c
    ${PROJECT_SOURCE_DIR}/security/oc_tls.c
)

set(COAP_SOURCES
    ${PROJECT_SOURCE_DIR}/messaging/coap/coap.c
    ${PROJECT_SOURCE_DIR}/messaging/coap/coap_signal.c
    ${PROJECT_SOURCE_DIR}/messaging/coap/engine.c
    ${PROJECT_SOURCE_DIR}/messaging/coap/observe.c
    ${PROJECT_SOURCE_DIR}/messaging/coap/oscore.c
    ${PROJECT_SOURCE_DIR}/messaging/coap/separate.c
    ${PROJECT_SOURCE_DIR}/messaging/coap/transactions.c
)

set(KNX_INCLUDE_DIRS 
    ${PROJECT_SOURCE_DIR}/
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/security
    ${PROJECT_SOURCE_DIR}/messaging/coap/
    ${PROJECT_SOURCE_DIR}/api
    ${PROJECT_SOURCE_DIR}/api/c-timestamp
)

# virtual library targets
add_library(kis-common INTERFACE)

# physical library targets
add_library(kisClientServer ${CORE_SOURCES} ${COAP_SOURCES} ${API_SOURCES})
add_library(tinycbor-master ${CBOR_SOURCES})
add_library(mbedtls STATIC ${TLS_SOURCES})
add_library(mbedx509 STATIC ${X509_SOURCES})
add_library(mbedcrypto STATIC ${CRYPTO_SOURCES})
add_library(kis-port ${PROJECT_SOURCE_DIR}/port/oc_log.c)
add_library(kis-link SHARED ${PROJECT_SOURCE_DIR}/dll/stack_callback.c)

# target with no individual output, will be build always 
add_custom_target(lib DEPENDS mbedcrypto mbedx509 mbedtls)

# physical exe targets, actuator demo (e.g. receiving command) > assumes WIN32/UNIX is in place
add_executable(lsab_minimal_all ${PROJECT_SOURCE_DIR}/apps/lsab_minimal_all.c)

# specific Cmake package 
include (GenerateExportHeader)

GENERATE_EXPORT_HEADER(kis-link
    BASE_NAME kis-link
    EXPORT_MACRO_NAME kis-link_EXPORT
    EXPORT_FILE_NAME kis-link_export.h
    STATIC_DEFINE kis-link_BUILT_AS_STATIC
)

# Detect the used platform and pick the right port
if(UNIX)
        set(PORT_DIR ${PROJECT_SOURCE_DIR}/port/linux)      # linux port
        target_link_libraries(kis-port PUBLIC pthread m)

        target_sources(kis-port PUBLIC
            ${PORT_DIR}/abort.c
            ${PORT_DIR}/clock.c
            ${PORT_DIR}/dns-sd.c
            ${PORT_DIR}/ipadapter.c
            ${PORT_DIR}/random.c
            ${PORT_DIR}/storage.c
            ${PORT_DIR}/tcpadapter.c
        )
elseif(WIN32)
        set(PORT_DIR ${PROJECT_SOURCE_DIR}/port/windows)    # windows port
        set(libs ws2_32)                                    # ensure loading WIN32 socket DLL

        target_sources(kis-port PUBLIC
            ${PORT_DIR}/abort.c
            ${PORT_DIR}/clock.c
            ${PORT_DIR}/dns-sd.c
            ${PORT_DIR}/ipadapter.c
            ${PORT_DIR}/random.c
            ${PORT_DIR}/storage.c
            ${PORT_DIR}/tcpadapter.c
        )

        target_sources(kis-port PRIVATE
            ${PORT_DIR}/mutex.c
            ${PORT_DIR}/network_addresses.c
        )
        target_link_libraries(kis-port PUBLIC iphlpapi)     # add Windows SDK API helper 
endif()

target_compile_definitions(mbedcrypto PUBLIC MBEDTLS_CONFIG_FILE=\"knx_mbedtls_config.h\")
target_compile_definitions(kisClientServer PUBLIC OC_CLIENT OC_SERVER)
target_compile_definitions(kis-link PUBLIC OC_SERVER OC_CLIENT)

# Specifies include directories to use when compiling a given target
target_include_directories(kis-link PRIVATE ${PROJECT_BINARY_DIR} ${KNX_INCLUDE_DIRS})
target_include_directories(kisClientServer PUBLIC ${KNX_INCLUDE_DIRS})
target_include_directories(tinycbor-master PUBLIC ${PROJECT_SOURCE_DIR}/tinycbor/src)
target_include_directories(kis-port PUBLIC ${PORT_DIR}/../)
target_include_directories(mbedcrypto PUBLIC
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/include
    ${PROJECT_SOURCE_DIR}/security
    ${PORT_DIR}
)

# Specifies linked libraries to build a target
target_link_libraries(kis-link PUBLIC kisClientServer)
target_link_libraries(lsab_minimal_all mbedtls kisClientServer)
target_link_libraries(kisClientServer PUBLIC kis-common mbedtls tinycbor-master kis-port)
target_link_libraries(kis-port PUBLIC kis-common mbedtls)
target_link_libraries(mbedcrypto ${libs})
target_link_libraries(mbedx509  mbedcrypto)
target_link_libraries(mbedtls  mbedx509)   

set_target_properties(mbedcrypto PROPERTIES OUTPUT_NAME mbedcrypto)
set_target_properties(mbedx509 PROPERTIES OUTPUT_NAME mbedx509)
set_target_properties(mbedtls PROPERTIES OUTPUT_NAME mbedtls)

install(TARGETS mbedtls mbedx509 mbedcrypto
    DESTINATION ${LIB_INSTALL_DIR}
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

# Process individual stack/ (demo)application behavior 
if(OC_OSCORE_ENABLED)
   target_compile_definitions(kis-common INTERFACE OC_OSCORE)
   target_compile_definitions(kis-common INTERFACE OC_SPAKE)
   target_compile_definitions(kis-common INTERFACE KNX_MIN_IT=${KNX_SPAKE_MIN_IT})
   target_compile_definitions(kis-common INTERFACE KNX_MAX_IT=${KNX_SPAKE_MAX_IT})
endif()

if(OC_IOT_ROUTER_ENABLED)
    target_compile_definitions(kis-common INTERFACE OC_IOT_ROUTER)
endif()

if(OC_PRINT_ENABLED)
    target_compile_definitions(kis-common INTERFACE OC_PRINT)
endif()

if(OC_PRINT_APP_ENABLED)
    target_compile_definitions(kis-common INTERFACE OC_PRINT_APP)
endif()

if(OC_DEBUG_ENABLED)
    target_compile_definitions(kis-common INTERFACE OC_DEBUG)
endif()

if(OC_DEBUG_OSCORE_ENABLED)
    target_compile_definitions(kis-common INTERFACE OC_DEBUG_OSCORE)
endif()

if(OC_REPLAY_PROTECTION_ENABLED)
    target_compile_definitions(kis-common INTERFACE OC_REPLAY_PROTECTION)
endif()

if(OC_TRUST_FIRST_MCAST_ENABLED)
    target_compile_definitions(kis-common INTERFACE OC_TRUST_FIRST_MCAST)
endif()



if(OC_DNS_SD_ENABLED)
    target_compile_definitions(kis-common INTERFACE OC_DNS_SD)
endif()

if(OC_PUBLISHER_TABLE_ENABLED)
    target_compile_definitions(kis-common INTERFACE OC_PUBLISHER_TABLE)
endif()

if(OC_LOG_TO_FILE_ENABLED)
    target_compile_definitions(kis-port INTERFACE OC_LOG_TO_FILE)
endif()

if (OC_USE_STORAGE)
    target_compile_definitions(kis-port PUBLIC OC_USE_STORAGE)
endif()

if (OC_USE_STORAGE)
    target_compile_definitions(kisClientServer PUBLIC OC_USE_STORAGE)
endif()

if (OC_USE_MULTICAST_SCOPE_2)
    target_compile_definitions(kisClientServer PUBLIC OC_USE_MULTICAST_SCOPE_2)
endif()

if(OC_LOG_TO_FILE_ENABLED)
    target_compile_definitions(kis-port INTERFACE OC_LOG_TO_FILE)
endif()

if(KNX_LOG_CAST_64_BIT_INTS_ENABLED)
    target_compile_definitions(kis-common INTERFACE KNX_LOG_CAST_64_BIT_INTS)
endif()

if(NOT ${KNX_GAMT_MAX_ENTRIES} EQUAL "")
    add_compile_definitions(GAMT_MAX_ENTRIES=${KNX_GAMT_MAX_ENTRIES})
endif()

if(NOT ${KNX_GOT_MAX_ENTRIES} EQUAL "")
    add_compile_definitions(GOT_MAX_ENTRIES=${KNX_GOT_MAX_ENTRIES})
endif()

if(NOT ${KNX_GPT_MAX_ENTRIES} EQUAL "")
    add_compile_definitions(GPT_MAX_ENTRIES=${KNX_GPT_MAX_ENTRIES})
endif()

if(NOT ${KNX_GRT_MAX_ENTRIES} EQUAL "")
    add_compile_definitions(GRT_MAX_ENTRIES=${KNX_GRT_MAX_ENTRIES})
endif()

if(NOT ${KNX_SEC_MAX_ENTRIES} EQUAL "")
    add_compile_definitions(G_AT_MAX_ENTRIES=${KNX_SEC_MAX_ENTRIES})
endif()

if(NOT ${KNX_PAGE_SIZE} EQUAL "")
    add_compile_definitions(PAGE_SIZE=${KNX_PAGE_SIZE})
endif()