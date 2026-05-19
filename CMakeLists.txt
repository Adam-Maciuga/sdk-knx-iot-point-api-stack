cmake_minimum_required (VERSION 3.21)
project(knx-iot-stack) 

### Compiler ###
# Compiler options (please do not forget to add own options such as to reduce enum sizes, such as for gcc -fshort-enums).
if(CMAKE_COMPILER_IS_GNUCC AND UNIX)
    # Emit position-independent code, see https://gcc.gnu.org/onlinedocs/gcc/Code-Gen-Options.html    
    add_compile_options(-fPIC) 
endif()

if(CMAKE_COMPILER_IS_GNUCC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wmissing-declarations -Wmissing-prototypes")
endif(CMAKE_COMPILER_IS_GNUCC)

if(CMAKE_COMPILER_IS_CLANG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wmissing-declarations -Wmissing-prototypes -Wdocumentation -Wno-documentation-deprecated-sync -Wunreachable-code")
endif(CMAKE_COMPILER_IS_CLANG)

if(MSVC)
  # Add command line definitiosn for the target in the current directory.
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
  # Add compile options for the target in the current directory and below.
  # W4 = max warn level
  # WX = all compile warnings are errors 
  add_compile_options(/W4 /WX-)
endif()

# Add preprocessor definitions for the current CMakeLists file.
add_compile_definitions(OC_DYNAMIC_ALLOCATION OC_CLIENT OC_SERVER)

### KNX-IoT Stack compilation variables ###
# - each build configuration (Debug, Release,...) needs to set the values as part of 'CmakePresets.json'
# - in dependence of a variables value a specific compile flag is set (see cmake 'target_compile_definitions')

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
set(OC_REPLAY_PROTECTION_ENABLED ON CACHE BOOL "Enables security replay protection using the Echo option.")

## KNX-IoT device specific resource variables
set(KNX_GOT_MAX_ENTRIES "50" CACHE STRING "Maximum number of group object table entries")
set(KNX_GPT_MAX_ENTRIES "20" CACHE STRING "Maximum number of group publisher table entries")
set(KNX_GRT_MAX_ENTRIES "20" CACHE STRING "Maximum number of group recipient table entries")
set(KNX_SEC_MAX_ENTRIES "20" CACHE STRING "Maximum number of access token table entries")
set(KNX_PAGE_SIZE "20" CACHE STRING "Server default page size for pagination")
set(KNX_IOT_SPAKE2PLUS_ITERATIONS "50000" CACHE STRING "Number of SHA256 iterations used within the SPAKE2+ handshake")

# KNX reset "Process Time" in seconds
set(KNX_RESPONSE_TIME_SECONDS "2" CACHE STRING "KNX reset Process Time in seconds")

# KNX read on init delay time betwwen to read on init requests (ms)
set(KNX_READ_ON_INIT_DELAY_MILLISECONDS "100" CACHE STRING "KNX read on init delay time between to subsequent read on init requests (milliseconds)")

# IPv6 multicast scope
# Scope 2 MAY be needed for the virtual DEMO applications when:
# - the devices are running on the same PC (e.g. for testing with EITT)
# - a network router does not allow to send multicast messages with scope 5 (site local)
# the scope is used by stack (discovery) and application (push button demo) messages
set(KNX_MULTICAST_SCOPE "5" CACHE STRING "IPv6 multicast scope, used on snd/rcv (mc) stack and s-mode messages" FORCE)

# IPv6 unicast port
# Specified in clause 2.6.3.1 with COAP_DEFAULT_PORT and shall be used for unicast messages
# - the optional put /dev/port is NOT implemented, the chosen port is used all the time
# - if the port is defined with '0' the OS assign an ephemeral port (useful for virtual apps on the same machine)
set(KNX_UNICAST_PORT "0" CACHE STRING "IPv6 unicast port, used on snd/rcv (uc) stack and s-mode messages" FORCE)

## Hidden Variables
# Note:
# Don't show variables below in any (command) GUI.
mark_as_advanced(KNX_IOT_SPAKE2PLUS_ITERATIONS)

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
    ${PROJECT_SOURCE_DIR}/util/oc_memb.c
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

## Security
# OSCORE
target_compile_definitions(kis-common INTERFACE KNX_IOT_SPAKE2PLUS_ITERATIONS=${KNX_IOT_SPAKE2PLUS_ITERATIONS})

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

### TBD FIXME refactor this section  ###
# Process individual stack/ (demo)application behavior 

## Sizes (tables, pagination, ...)
add_compile_definitions(GOT_MAX_ENTRIES=${KNX_GOT_MAX_ENTRIES})     # group object table size
add_compile_definitions(GPT_MAX_ENTRIES=${KNX_GPT_MAX_ENTRIES})     # publisher table size
add_compile_definitions(GRT_MAX_ENTRIES=${KNX_GRT_MAX_ENTRIES})     # recipient table size
add_compile_definitions(G_AT_MAX_ENTRIES=${KNX_SEC_MAX_ENTRIES})    # access token table size
add_compile_definitions(PAGE_SIZE=${KNX_PAGE_SIZE})                 # server default page size

# KNX reset Process Time (seconds)
add_compile_definitions(KNX_RESPONSE_TIME_SECONDS=${KNX_RESPONSE_TIME_SECONDS})

# KNX read on init delay time between to subsequent read requests (milliseconds)
add_compile_definitions(KNX_READ_ON_INIT_DELAY_MILLISECONDS=${KNX_READ_ON_INIT_DELAY_MILLISECONDS})

# IPv6 multicast scope
add_compile_definitions(KNX_MULTICAST_SCOPE=${KNX_MULTICAST_SCOPE})

# IPv6 unicast port
add_compile_definitions(KNX_UNICAST_PORT=${KNX_UNICAST_PORT})
