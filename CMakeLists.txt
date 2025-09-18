cmake_minimum_required (VERSION 3.15)
project(knx-iot-stack) 

# load module to fetch external content
include(FetchContent)

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
  # W4 = max warn level
  # WX = all compile warnings are errors 
  add_compile_options(/W4 /WX-)
endif()

# add preprocessor definitiosn for the current CMakeLists file
add_compile_definitions(OC_DYNAMIC_ALLOCATION OC_CLIENT OC_SERVER)

# Stack compilation variables 
# - each build configuration (debug,release,...) needs to set the values as part of 'CmakePresets.json'
# - in dependence of a variables value a specific complie flag is set (see cmake 'target_compile_definitions')

## Common
set(CLANG_TIDY_ENABLED OFF CACHE BOOL "Enable clang-tidy analysis during compilation.")
set(KNX_LOG_CAST_64_BIT_INTS_ENABLED OFF CACHE BOOL "Cast int64_t and uint64_t values to (int) if platform doesn't support 64 bit int format specifiers")
set(OC_DNS_SD_ENABLED ON CACHE BOOL "Enable DNS SD discovery (in addition to CoAP discovery)")
set(OC_PUBLISHER_TABLE_ENABLED ON CACHE BOOL "Enable publisher table, with this a device can (also) receive (OFF: for plain senders).")
set(OC_PKI_ENABLED OFF CACHE BOOL "Enable PKI security")
set(OC_USE_MULTICAST_SCOPE_2 ON CACHE BOOL "devices send 'additionally' group multicast events with scope 2, e.g.; for PC based applications.")

### Allows to use an own security framework, with ON the KNX MBEDTLS framework is used. 
set(OC_USE_KNX_MBEDTLS ON CACHE BOOL "Use MBEDTLS associated with KNX IoT repository")

## Debug 
set(OC_LOG_TO_FILE_ENABLED OFF CACHE BOOL "Enables that all logging messages are redirected to a file, to use it the 'PRINT' option must be also enabled.")
set(OC_USE_STORAGE ON CACHE BOOL "Enables that the configuration data are persisted in a (file-based) storage.")
set(OC_OSCORE_ENABLED ON CACHE BOOL "Enables oscore, OFF may be used for debugging reasons")
set(OC_PRINT_ENABLED OFF CACHE BOOL "Enables print messages for the stack (and application; if used). OFF will disable all logging.")
set(OC_DEBUG_ENABLED OFF CACHE BOOL "Enables common debug messages for the stack (and application; if used).")
set(OC_DEBUG_OSCORE_ENABLED OFF CACHE BOOL "Enables security debug messages for the stack.")
set(USE_CONSOLE ON CACHE BOOL "Enables the console as a logging output also for the GUI applications, typically used for debugging reasons.")
set(OC_REPLAY_PROTECTION_ENABLED ON CACHE BOOL "Enables security replay protection using the Echo option.")

## iot device specific resource variables
set(KNX_GOT_MAX_ENTRIES "50" CACHE STRING "Maximum number of group object table entries")
set(KNX_GPT_MAX_ENTRIES "20" CACHE STRING "Maximum number of group publisher table entries")
set(KNX_GRT_MAX_ENTRIES "20" CACHE STRING "Maximum number of group recipient table entries")
set(KNX_SEC_MAX_ENTRIES "20" CACHE STRING "Maximum number of access token table entries")
set(KNX_PAGE_SIZE "20" CACHE STRING "Server default page size for pagination")
set(KNX_SPAKE_ITERATIONS "50000" CACHE STRING "Number of SHA256 iterations used within the SPAKE2+ handshake")

# dont show below variables in any (command) GUI
mark_as_advanced(KNX_SPAKE_ITERATIONS)

set(API_SOURCES
    ${PROJECT_SOURCE_DIR}/api/oc_base64.c
    ${PROJECT_SOURCE_DIR}/api/oc_blockwise.c
    ${PROJECT_SOURCE_DIR}/api/oc_buffer.c
    ${PROJECT_SOURCE_DIR}/api/oc_client_api.c
    ${PROJECT_SOURCE_DIR}/api/oc_clock.c
    ${PROJECT_SOURCE_DIR}/api/oc_core_res.c
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
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/aesce.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/aria.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/asn1parse.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/asn1write.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/base64.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/bignum.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/bignum_core.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/bignum_mod.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/bignum_mod_raw.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/block_cipher.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/camellia.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ccm.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/chacha20.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/chachapoly.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/cipher.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/cipher_wrap.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/constant_time.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/cmac.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ctr_drbg.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/des.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/dhm.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ecdh.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ecdsa.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ecjpake.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ecp.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ecp_curves.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ecp_curves_new.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/entropy.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/entropy_poll.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/error.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/gcm.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/hkdf.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/hmac_drbg.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/lmots.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/lms.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/md.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/md5.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/memory_buffer_alloc.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/nist_kw.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/oid.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/padlock.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/pem.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/pk.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/pk_ecc.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/pk_wrap.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/pkcs12.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/pkcs5.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/pkparse.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/pkwrite.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/platform.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/platform_util.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/poly1305.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/psa_crypto.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/psa_crypto_aead.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/psa_crypto_cipher.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/psa_crypto_client.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/psa_crypto_driver_wrappers_no_static.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/psa_crypto_ecp.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/psa_crypto_ffdh.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/psa_crypto_hash.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/psa_crypto_mac.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/psa_crypto_pake.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/psa_crypto_rsa.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/psa_crypto_se.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/psa_crypto_slot_management.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/psa_crypto_storage.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/psa_its_file.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/psa_util.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ripemd160.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/rsa.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/rsa_alt_helpers.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/sha1.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/sha256.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/sha512.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/sha3.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/threading.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/timing.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/version.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/version_features.c
)

set(X509_SOURCES
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/pkcs7.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/x509.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/x509_create.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/x509_crl.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/x509_crt.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/x509_csr.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/x509write.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/x509write_crt.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/x509write_csr.c
)

set(TLS_SOURCES
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/debug.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/mps_reader.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/mps_trace.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/net_sockets.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ssl_cache.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ssl_ciphersuites.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ssl_client.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ssl_cookie.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ssl_debug_helpers_generated.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ssl_msg.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ssl_ticket.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ssl_tls.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ssl_tls12_client.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ssl_tls12_server.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ssl_tls13_keys.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ssl_tls13_server.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ssl_tls13_client.c
    ${PROJECT_SOURCE_DIR}/deps/mbedtls/library/ssl_tls13_generic.c
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

# virtual library targets, used to provide common (compile) settings 
add_library(kis-common INTERFACE)

# physical library targets, with default type STATIC
add_library(kisClientServer ${CORE_SOURCES} ${COAP_SOURCES} ${API_SOURCES})
add_library(tinycbor-master ${CBOR_SOURCES})

# integrate MBTLS manually (lean build process) and not with cmake 'add_subdirectory' functionality (directly the mbedtls cmake file)
# mbed tls version is defined in git .submodles file, see repository(3.6.4)
if(OC_USE_KNX_MBEDTLS)
    add_library(mbedtls ${TLS_SOURCES})
    add_library(mbedx509 ${X509_SOURCES})
    add_library(mbedcrypto ${CRYPTO_SOURCES})
endif()

if(UNIX OR WIN32)
    add_library(kis-port ${PROJECT_SOURCE_DIR}/port/oc_log.c)

    ## LSSB NON GUI demo
    add_executable(lssb_virtual_cl 
      ${PROJECT_SOURCE_DIR}/apps/lssb/knx_iot_virtual_lssb.c
      ${PROJECT_SOURCE_DIR}/apps/knx_iot_virtual.c
    )
    
     ## LSAB NON GUI demo
    add_executable(lsab_virtual_cl 
       ${PROJECT_SOURCE_DIR}/apps/lsab/knx_iot_virtual_lsab.c
       ${PROJECT_SOURCE_DIR}/apps/knx_iot_virtual.c
    )
endif()

# Detect the used platform and pick the right port
if(UNIX)
        set(PORT_DIR ${PROJECT_SOURCE_DIR}/port/linux)      # linux port
        target_link_libraries(kis-port PUBLIC pthread m)    # add Linux parallel execution helper 

        target_sources(kis-port PUBLIC
            ${PORT_DIR}/abort.c
            ${PORT_DIR}/clock.c
            ${PORT_DIR}/dns-sd.c
            ${PORT_DIR}/ipadapter.c
            ${PORT_DIR}/random.c
            ${PORT_DIR}/storage.c
            ${PORT_DIR}/tcpadapter.c
        )
endif()

if(WIN32)
        
        # Add manifest for windows GUI applications when using MinGW
        if(MINGW)
            set(MANIFEST_RC ${CMAKE_SOURCE_DIR}/apps/manifest.rc)
        else()
            set(MANIFEST_RC "")
        endif()
        
        set(PORT_DIR ${PROJECT_SOURCE_DIR}/port/windows)    # windows port
        target_link_libraries(kis-port PUBLIC iphlpapi)     # add Windows SDK API helper 

        set(libs ws2_32 bcrypt)                             # ensure loading WIN32 socket DLL and bcrypt DLL 

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
        
        # fetch GUI framework
        FetchContent_Declare(
          wxWidgets
          URL https://github.com/wxWidgets/wxWidgets/releases/download/v3.2.8/wxWidgets-3.2.8.7z
          DOWNLOAD_EXTRACT_TIMESTAMP true
        )

        # prefer to use extraction timestamp rather than archive timepstamp 
        option(DOWNLOAD_EXTRACT_TIMESTAMP "" ON) 
        option(wxBUILD_SHARED "" OFF)

        message(STATUS "Fetching wxWidgets, kindly wait...")
        FetchContent_MakeAvailable(wxWidgets)

        # physical exe targets
        ## EITT GUI (for certification)
        add_executable(eitt_virtual_gui WIN32
            ${PROJECT_SOURCE_DIR}/apps/eitt/knx_iot_virtual_eitt.cpp
            ${PROJECT_SOURCE_DIR}/apps/eitt/knx_iot_virtual_eitt.c
            ${PROJECT_SOURCE_DIR}/apps/knx_iot_virtual.c
            ${MANIFEST_RC} 
        )

        ## LSSB GUI 
        add_executable(lssb_virtual_gui WIN32
            ${PROJECT_SOURCE_DIR}/apps/lssb/knx_iot_virtual_lssb.cpp
            ${PROJECT_SOURCE_DIR}/apps/lssb/knx_iot_virtual_lssb.c
            ${PROJECT_SOURCE_DIR}/apps/knx_iot_virtual.c
            ${MANIFEST_RC} 
        )

        ## LSAB GUI 
        add_executable(lsab_virtual_gui WIN32
            ${PROJECT_SOURCE_DIR}/apps/lsab/knx_iot_virtual_lsab.cpp
            ${PROJECT_SOURCE_DIR}/apps/lsab/knx_iot_virtual_lsab.c
            ${PROJECT_SOURCE_DIR}/apps/knx_iot_virtual.c
            ${MANIFEST_RC} 
        )

        target_link_libraries(eitt_virtual_gui wx::net wx::core wx::base kisClientServer)

        target_link_libraries(lssb_virtual_gui wx::net wx::core wx::base kisClientServer)
        target_compile_definitions(lssb_virtual_gui PUBLIC KNX_GUI)

        target_link_libraries(lsab_virtual_gui wx::net wx::core wx::base kisClientServer)
        target_compile_definitions(lsab_virtual_gui PUBLIC KNX_GUI)

        if(USE_CONSOLE)
          if(MSVC)
                      set(GUI_FLAG "/SUBSYSTEM:CONSOLE")
                      set(CONSOLE_FLAG "/SUBSYSTEM:CONSOLE")
          else()
                      set(GUI_FLAG "-mconsole")
                      set(CONSOLE_FLAG "-mconsole")
          endif()
                
          # Console versions
          set_target_properties(lssb_virtual_cl PROPERTIES LINK_FLAGS ${CONSOLE_FLAG})
          set_target_properties(lsab_virtual_cl PROPERTIES LINK_FLAGS ${CONSOLE_FLAG})

          # GUI versions
          set_target_properties(eitt_virtual_gui PROPERTIES LINK_FLAGS ${GUI_FLAG})
          set_target_properties(lssb_virtual_gui PROPERTIES LINK_FLAGS ${GUI_FLAG})
          set_target_properties(lsab_virtual_gui PROPERTIES LINK_FLAGS ${GUI_FLAG})
        endif()

endif()

if(UNIX)
    # use system-installed wxWidgets on Linux
    find_package(wxWidgets REQUIRED net core base)
    include(${wxWidgets_USE_FILE})
    
    ## EITT GUI (Linux)
    add_executable(eitt_virtual_gui
        ${PROJECT_SOURCE_DIR}/apps/eitt/knx_iot_virtual_eitt.cpp
        ${PROJECT_SOURCE_DIR}/apps/eitt/knx_iot_virtual_eitt.c
        ${PROJECT_SOURCE_DIR}/apps/knx_iot_virtual.c
    )

    ## LSSB GUI (Linux)
    add_executable(lssb_virtual_gui
        ${PROJECT_SOURCE_DIR}/apps/lssb/knx_iot_virtual_lssb.cpp
        ${PROJECT_SOURCE_DIR}/apps/lssb/knx_iot_virtual_lssb.c
        ${PROJECT_SOURCE_DIR}/apps/knx_iot_virtual.c
    )

    ## LSAB GUI (Linux)
    add_executable(lsab_virtual_gui
        ${PROJECT_SOURCE_DIR}/apps/lsab/knx_iot_virtual_lsab.cpp
        ${PROJECT_SOURCE_DIR}/apps/lsab/knx_iot_virtual_lsab.c
        ${PROJECT_SOURCE_DIR}/apps/knx_iot_virtual.c
    )

    target_link_libraries(eitt_virtual_gui ${wxWidgets_LIBRARIES} kisClientServer)
    target_link_libraries(lssb_virtual_gui ${wxWidgets_LIBRARIES} kisClientServer)
    target_link_libraries(lsab_virtual_gui ${wxWidgets_LIBRARIES} kisClientServer)

    target_compile_definitions(lssb_virtual_gui PUBLIC KNX_GUI)
    target_compile_definitions(lsab_virtual_gui PUBLIC KNX_GUI)
endif()

# When using KNX standard security source MBTLS -> use config definitions defined by Intel
if(OC_USE_KNX_MBEDTLS)
    target_compile_definitions(mbedcrypto PUBLIC MBEDTLS_CONFIG_FILE=\"knx_mbedtls_config.h\")
endif()

# Stack can send + receive
target_compile_definitions(kisClientServer PUBLIC OC_CLIENT OC_SERVER)                   

# Specifies include directories to use when compiling a given target
target_include_directories(kisClientServer PUBLIC ${KNX_INCLUDE_DIRS})
target_include_directories(tinycbor-master PUBLIC ${PROJECT_SOURCE_DIR}/tinycbor/src)

if(OC_USE_KNX_MBEDTLS)
    target_include_directories(mbedcrypto PUBLIC
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/deps/mbedtls/include
        ${PROJECT_SOURCE_DIR}/security
        ${PORT_DIR}
    )
endif()

if(UNIX OR WIN32)
    # Specifies linked libraries (dependencies/references) to build a target
    ## applications ...
    target_link_libraries(lssb_virtual_cl kisClientServer)
    target_link_libraries(lsab_virtual_cl kisClientServer)

    ## ... from
    target_link_libraries(kis-port PUBLIC kis-common mbedtls)
    target_include_directories(kis-port PUBLIC ${PORT_DIR}/../)
endif()

if(OC_USE_KNX_MBEDTLS)
    target_link_libraries(kisClientServer PUBLIC kis-common mbedtls tinycbor-master kis-port)
else()
    target_link_libraries(kisClientServer PUBLIC kis-common tinycbor-master kis-port)
endif()

if(OC_USE_KNX_MBEDTLS)
    target_link_libraries(mbedcrypto ${libs})
    target_link_libraries(mbedx509 mbedcrypto)
    target_link_libraries(mbedtls mbedx509)

    set_target_properties(mbedcrypto PROPERTIES OUTPUT_NAME mbedcrypto)
    set_target_properties(mbedx509 PROPERTIES OUTPUT_NAME mbedx509)
    set_target_properties(mbedtls PROPERTIES OUTPUT_NAME mbedtls)

    install(TARGETS mbedcrypto mbedx509 mbedtls
            PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif()

# Process individual stack/ (demo)application behavior 

## Sizes (tables, pagination, ...)
add_compile_definitions(GOT_MAX_ENTRIES=${KNX_GOT_MAX_ENTRIES})     # group object table size
add_compile_definitions(GPT_MAX_ENTRIES=${KNX_GPT_MAX_ENTRIES})     # publisher table size
add_compile_definitions(GRT_MAX_ENTRIES=${KNX_GRT_MAX_ENTRIES})     # recipient table size
add_compile_definitions(G_AT_MAX_ENTRIES=${KNX_SEC_MAX_ENTRIES})    # access token table size (independent from OS_OSCORE, hence not added to target compile definitions)
add_compile_definitions(PAGE_SIZE=${KNX_PAGE_SIZE})                 # server default page size

## Security 
if(OC_OSCORE_ENABLED)
   target_compile_definitions(kis-common INTERFACE OC_OSCORE)
   target_compile_definitions(kis-common INTERFACE OC_SPAKE)
   target_compile_definitions(kis-common INTERFACE OC_SPAKE_IT=${KNX_SPAKE_ITERATIONS})
endif()

if(OC_PRINT_ENABLED)
    target_compile_definitions(kis-common INTERFACE OC_PRINT)
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

if(OC_DNS_SD_ENABLED)
    target_compile_definitions(kis-common INTERFACE OC_DNS_SD)
endif()

if(OC_PUBLISHER_TABLE_ENABLED)
    target_compile_definitions(kis-common INTERFACE OC_PUBLISHER_TABLE)
endif()

if(OC_LOG_TO_FILE_ENABLED)
    if(UNIX OR WIN32)
        target_compile_definitions(kis-port INTERFACE OC_LOG_TO_FILE)
    endif()
endif()

if (OC_USE_STORAGE)
    if(UNIX OR WIN32)
        target_compile_definitions(kis-port PUBLIC OC_USE_STORAGE)
    endif()
    target_compile_definitions(kisClientServer PUBLIC OC_USE_STORAGE)
endif()

if (OC_USE_MULTICAST_SCOPE_2)
    target_compile_definitions(kisClientServer PUBLIC OC_USE_MULTICAST_SCOPE_2)
endif()

if(KNX_LOG_CAST_64_BIT_INTS_ENABLED)
    target_compile_definitions(kis-common INTERFACE KNX_LOG_CAST_64_BIT_INTS)
endif()
