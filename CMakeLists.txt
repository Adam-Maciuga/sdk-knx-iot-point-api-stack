cmake_minimum_required (VERSION 3.21)
project(knx-iot-stack) 

### Compiler ###
# Compiler options (please do not forget to add own options such as to reduce enum sizes, such as for gcc -fshort-enums).
if(CMAKE_COMPILER_IS_GNUCC AND UNIX)
    # Emit position-independent code, see https://gcc.gnu.org/onlinedocs/gcc/Code-Gen-Options.html    
    add_compile_options(-fPIC) 
endif()

if(MSVC)
  
  # Add (global) command line definitions for the target in the current directory.
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
  # Warning-level flags (such as /W4 /WX-) are intentionally NOT set here.
  #
  # - 'add_compile_options' is directory-scoped and would leak into the FetchContent
  #   third-party dependencies (such as Mbed TLS,TinyCBOR, mDNS, googletest, ...)
  #
  # - Mbed TLS uses its own /W3 and /WX on its targets, the HERE leaked flags would cause 
  #   MSVC D9025 "overriding" warnings on every Mbed TLS source file
  #
  # - The stack warning flags are attached to the kis-common INTERFACE target below, 
  #   which only the stack's own targets consume.

endif()

# Add preprocessor definitions for the current CMakeLists file.
add_compile_definitions(OC_CLIENT OC_SERVER)

### KNX-IoT Stack compilation variables ###

## Stack variables vs device application variables 
#   Each build configuration (Debug, Release,...) needs to set the 'values' as part of 'CmakePresets.json', 
#   in dependence of such a variable 'value' a specific compile flag is set (see cmake 'target_compile_definitions').
#   HENCE, 
#   - stack 'values' that MAY be overwritten by device application don't use FORCE = the stack uses this value as a default 'value'
#   - stack 'values' that SHALL NOT be overwritten by device application uses FORCE = fixed stack 'value'

## Common
set(CLANG_TIDY_ENABLED OFF CACHE BOOL "Enable clang-tidy analysis during compilation.")
set(KNX_LOG_CAST_64_BIT_INTS_ENABLED OFF CACHE BOOL "Cast int64_t and uint64_t values to (int) if platform doesn't support 64 bit int format specifiers")
set(OC_DNS_SD_ENABLED ON CACHE BOOL "Enable DNS SD discovery (in addition to CoAP discovery)")
set(OC_PUBLISHER_TABLE_ENABLED ON CACHE BOOL "Enable publisher table, with this a device can (also) receive (OFF: for plain senders).")
set(OC_PKI_ENABLED OFF CACHE BOOL "Enable PKI security")

## External Libraries
# Mbed TLS
# Set to ON if the Mbed TLS library provided by the KNX-IoT stack should be used, 
# set to OFF if the Mbed TLS library is provided in other ways (e.g. the OS).
set(CONFIG_KNXIOT_MBEDTLS_BUILTIN ON CACHE BOOL "Use the Mbed TLS library provided by the KNX-IoT stack.")

# TinyCBOR
# Set to ON if the TinyCBOR library provided by the KNX-IoT stack should be used, 
# set to OFF if the TinyCBOR  library is provided in other ways (e.g. the OS).
set(CONFIG_KNXIOT_TINYCBOR_BUILTIN ON CACHE BOOL "Use the TinyCBOR library provided by the KNX-IoT stack.")

## Debug 
set(CONFIG_KNX_LOG_TO_FILE OFF CACHE BOOL "Enables that all logging messages are redirected to a file, to use it the 'PRINT' option must be also enabled.")
set(OC_PRINT_ENABLED OFF CACHE BOOL "Enables print messages for the stack (and application; if used). OFF will disable all logging.")
set(OC_DEBUG_ENABLED OFF CACHE BOOL "Enables common debug messages for the stack (and application; if used).")
set(OC_DEBUG_OSCORE_ENABLED OFF CACHE BOOL "Enables security debug messages for the stack.")
set(USE_CONSOLE ON CACHE BOOL "Enables the console as a logging output also for the GUI applications, typically used for debugging reasons.")

### Device ###
# KNX-IoT device specific resource variables 
set(KNX_GOT_MAX_ENTRIES "50" CACHE STRING "Maximum number of group object table entries")
set(KNX_GPT_MAX_ENTRIES "20" CACHE STRING "Maximum number of group publisher table entries")
set(KNX_GRT_MAX_ENTRIES "20" CACHE STRING "Maximum number of group recipient table entries")
set(KNX_SEC_MAX_ENTRIES "20" CACHE STRING "Maximum number of access token table entries")
set(KNX_PAGE_SIZE "20" CACHE STRING "Server default page size for pagination")
set(KNX_PAYLOAD_SIZE "1024" CACHE STRING "Maximum payload bytes, allocated by stack/applications per default (see notes below on compile defintion)" FORCE)

# Network
# IPv6 unicast port
# Specified in clause 2.6.3.1 with COAP_DEFAULT_PORT and shall be used for unicast messages
# - the optional put /dev/port is NOT implemented, the chosen port is used all the time
# - if the port is defined with '0' the OS assign an ephemeral port (useful for virtual apps on the same machine)
set(KNX_UNICAST_PORT "0" CACHE STRING "IPv6 unicast port, used on snd/rcv (uc) stack and s-mode messages")

# IPv6 multicast scope (used for sending and receiving multicast messages).
# Scope 2 (link-local) is appropriate for isolated single-segment networks
# or when the router blocks site-local multicast.
# Scope 5 (site-local) is the default for routed deployments.
set(KNX_MULTICAST_SCOPE "5" CACHE STRING "IPv6 multicast scope, used on snd/rcv (mc) stack and s-mode messages")

# KNX read on init delay time between two subsequent read on init requests (ms)
set(KNX_READ_ON_INIT_DELAY_MILLISECONDS "100" CACHE STRING "KNX read on init delay time between two subsequent read on init requests (milliseconds)")

# KNX reset "Process Time" in seconds
set(KNX_RESPONSE_TIME_SECONDS "2" CACHE STRING "KNX reset Process Time in seconds")

### KNX-IoT Stack ###
set(KNX_INCLUDE_DIRS 
    ${PROJECT_SOURCE_DIR}/
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/security		# TODO this should not be PUBLIC
    ${PROJECT_SOURCE_DIR}/messaging/coap/	# TODO this should not be PUBLIC
    ${PROJECT_SOURCE_DIR}/api			# TODO this should not be PUBLIC
    ${PROJECT_SOURCE_DIR}/api/c-timestamp	# TODO this should not be PUBLIC
)

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

set(CORE_SOURCES
    # Utilities that are used deep within the stack
    ${PROJECT_SOURCE_DIR}/util/oc_etimer.c
    ${PROJECT_SOURCE_DIR}/util/oc_list.c
    ${PROJECT_SOURCE_DIR}/util/oc_mmem.c
    ${PROJECT_SOURCE_DIR}/util/oc_process.c
    ${PROJECT_SOURCE_DIR}/util/oc_timer.c
    # Security
    ${PROJECT_SOURCE_DIR}/security/oc_oscore_context.c
    ${PROJECT_SOURCE_DIR}/security/oc_oscore_crypto.c
    ${PROJECT_SOURCE_DIR}/security/oc_oscore_engine.c
    ${PROJECT_SOURCE_DIR}/security/oc_tls.c
    ${PROJECT_SOURCE_DIR}/security/spake2plus.c
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

## KNX-IoT Stack Options ##
# virtual library targets, used to provide common (compile) settings 
add_library(kis-common INTERFACE)

# - stack warning flags are attached to the kis-common INTERFACE target 
#   (consumed only by the stack's own targets) instead of via directory-scoped 
#   add_compile_options / CMAKE_C_FLAGS, they do NOT leak into the FetchContent 
#   third-party dependencies (see details above on ### Compiler ###). 
#
# - the GNU/Clang flags are guarded with # $<COMPILE_LANGUAGE:C> because they are not
#   valid for C++ (the unit tests consume kis-common transitively).

if(MSVC)
    # W4 = max warn level, WX- = do NOT treat warnings as errors (MSVC default).
    target_compile_options(kis-common INTERFACE /W4 /WX-)
endif()
if(CMAKE_COMPILER_IS_GNUCC)
    target_compile_options(kis-common INTERFACE
        $<$<COMPILE_LANGUAGE:C>:-Wmissing-declarations>
        $<$<COMPILE_LANGUAGE:C>:-Wmissing-prototypes>
    )
endif()
if(CMAKE_COMPILER_IS_CLANG)
    target_compile_options(kis-common INTERFACE
        $<$<COMPILE_LANGUAGE:C>:-Wmissing-declarations>
        $<$<COMPILE_LANGUAGE:C>:-Wmissing-prototypes>
        $<$<COMPILE_LANGUAGE:C>:-Wdocumentation>
        $<$<COMPILE_LANGUAGE:C>:-Wno-documentation-deprecated-sync>
        $<$<COMPILE_LANGUAGE:C>:-Wunreachable-code>
    )
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

if(OC_DNS_SD_ENABLED)
    target_compile_definitions(kis-common INTERFACE OC_DNS_SD)
endif()

if(OC_PUBLISHER_TABLE_ENABLED)
    target_compile_definitions(kis-common INTERFACE OC_PUBLISHER_TABLE)
endif()

if(KNX_LOG_CAST_64_BIT_INTS_ENABLED)
    target_compile_definitions(kis-common INTERFACE KNX_LOG_CAST_64_BIT_INTS)
endif()

### KNX-IoT Stack - Platform Ports ###
add_subdirectory(port)

### KNX-IoT Stack ###
# physical library targets, with default type STATIC
add_library(kisClientServer ${CORE_SOURCES} ${COAP_SOURCES} ${API_SOURCES})

# Stack can send + receive
target_compile_definitions(kisClientServer PUBLIC OC_CLIENT OC_SERVER)                   

# Specifies include directories to use when compiling a given target
target_include_directories(kisClientServer PUBLIC ${KNX_INCLUDE_DIRS})

## KNX-IoT Stack Options (2) ##
target_link_libraries(kisClientServer PUBLIC kis-common)
if(CONFIG_KNXIOT_MBEDTLS_BUILTIN)
    target_link_libraries(kisClientServer PUBLIC mbedtls)
    # Note:
    # spake2plus.c uses private TF-PSA-Crypto ECP/BigNum headers for raw
    # P-256 point arithmetic that has no PSA equivalent in Mbed TLS 4.x.
    # See docs/mbedtls_3.x_to_4.x_migration.md, section "SPAKE2+ Migration Design".
    target_include_directories(kisClientServer PRIVATE
        "${mbedtls_SOURCE_DIR}/tf-psa-crypto/drivers/builtin/include"
    )
endif()
if(CONFIG_KNXIOT_TINYCBOR_BUILTIN)
    target_link_libraries(kisClientServer PUBLIC tinycbor)
endif()
target_link_libraries(kisClientServer PUBLIC kis-port)

## Process individual stack/application behavior ##
target_compile_definitions(kis-common INTERFACE
    ## Sizes (tables, pagination, ...)
    GOT_MAX_ENTRIES=${KNX_GOT_MAX_ENTRIES}     # group object table size
    GPT_MAX_ENTRIES=${KNX_GPT_MAX_ENTRIES}     # publisher table size
    GRT_MAX_ENTRIES=${KNX_GRT_MAX_ENTRIES}     # recipient table size
    G_AT_MAX_ENTRIES=${KNX_SEC_MAX_ENTRIES}    # access token table size
    PAGE_SIZE=${KNX_PAGE_SIZE}                 # server default page size

    ## Network
    # IPv6 unicast port
    KNX_UNICAST_PORT=${KNX_UNICAST_PORT}

    # IPv6 multicast scope
    KNX_MULTICAST_SCOPE=${KNX_MULTICAST_SCOPE}

    # KNX read on init delay time between to subsequent read requests (milliseconds)
    KNX_READ_ON_INIT_DELAY_MILLISECONDS=${KNX_READ_ON_INIT_DELAY_MILLISECONDS}

    # KNX reset Process Time (seconds)
    KNX_RESPONSE_TIME_SECONDS=${KNX_RESPONSE_TIME_SECONDS}

    
    ## KNX payload size (bytes)
    #  - defines the default value that is used for stack AND application 'malloc/calloc' calls
    #    such as for application read responses or stack discovery responses
    #  - must be carfully choosen since stack resources may need much more bytes than application datapoints
    #    (stack needs a minimum of 1k for discovery responses + extra buffer,
    #     this depends on how many application resources are defined and responded)
    #  - cannot be overwritten at runtime by applications
    KNX_PAYLOAD_SIZE=${KNX_PAYLOAD_SIZE}
)

### Test Control Endpoints ###
# Optional library that registers /test/restart, /test/factory-reset and
# /test/trigger endpoints.  Link kisTestControl in any app that needs
# automated testing (e.g. eitt_virtual_gui, runtime_test_server).
add_library(kisTestControl STATIC ${PROJECT_SOURCE_DIR}/api/oc_test_control.c)
target_include_directories(kisTestControl PUBLIC ${PROJECT_SOURCE_DIR}/api)
target_link_libraries(kisTestControl PUBLIC kisClientServer)

### Unit Tests ###
option(BUILD_TESTING "Build unit tests (requires a C++ compiler)" OFF)
option(ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(BUILD_TESTING)
    enable_language(CXX)
    enable_testing()
    if(ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
    add_subdirectory(tests)
endif()