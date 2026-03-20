cmake_minimum_required (VERSION 3.21)

### KNX-IoT Stack - Platform Ports ###
add_library(kis-port ${CMAKE_CURRENT_SOURCE_DIR}/oc_log.c)

# Detect the used platform and pick the right port
if(WIN32)
    ## KNX-IoT Stack - Windows Port ##
    set(PORT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/windows CACHE STRING "Platform port directory")

    # Add Windows SDK API helper 
    target_link_libraries(kis-port PUBLIC iphlpapi)

    # Note:
    # set(libs ws2_32 bcrypt) moved to deps/CMakeLists.txt

    target_sources(kis-port PRIVATE
        ${PORT_DIR}/mutex.c
        ${PORT_DIR}/network_addresses.c
    ) 
elseif(UNIX)
    ## KNX-IoT Stack - Linux Port ##
    set(PORT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/linux CACHE STRING "Platform port directory")

    # Add Linux parallel execution helper 
    target_link_libraries(kis-port PUBLIC pthread m)
endif()

# Load module to fetch external content.
include(FetchContent)

### Mbed TLS (provided by the KNX-IoT stack) ###
if(CONFIG_KNXIOT_MBEDTLS_BUILTIN)
    # Note:
    # All *.c files from Mbed TLS v3.6.5 directory "mbedtls/library" are used, other directories are not required.

    # Note:
    # Make sure all KNX-IoT related options are already set, they are used in "knx_mbedtls_config.h".
    set(MBEDTLS_CONFIG_FILE "${PROJECT_SOURCE_DIR}/security/knx_mbedtls_config.h") # TODO refactor config file

    # Be strict and reduce compilation resources
    set(UNSAFE_BUILD OFF CACHE BOOL "" FORCE)
    set(MBEDTLS_FATAL_WARNINGS ON CACHE BOOL "" FORCE)
    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)

    # mbedtls/library/CMakeLists.txt
    set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
    set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
    #set(LINK_WITH_PTHREAD OFF CACHE BOOL "" FORCE)
    #set(LINK_WITH_TRUSTED_STORAGE OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        mbedtls
        # v3.6.5 is a LTS security update.
        # Note:
        # The git approach uses ~250MB more disk space, so we go with the release version.
        URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.5/mbedtls-3.6.5.tar.bz2
        URL_HASH SHA256=4a11f1777bb95bf4ad96721cac945a26e04bf19f57d905f241fe77ebeddf46d8
        DOWNLOAD_EXTRACT_TIMESTAMP ON
    )

    FetchContent_MakeAvailable(mbedtls)

    # Additional include directories for "knx_mbedtls_config.h".
    # TBD FIXME #include "port/oc_assert.h" why is that one needed?
    set(MBEDTLS_INCLUDE_DIRS
        PRIVATE "${PROJECT_SOURCE_DIR}"
        PRIVATE "${PROJECT_SOURCE_DIR}/include"
        PRIVATE "${PORT_DIR}"
    ) 

    target_include_directories(mbedtls ${MBEDTLS_INCLUDE_DIRS})
    target_include_directories(mbedcrypto ${MBEDTLS_INCLUDE_DIRS}) 
    target_include_directories(mbedx509 ${MBEDTLS_INCLUDE_DIRS})

    if(WIN32)
        # Note:
        # Mbed TLS v3.6.5 throws -Werror=format= errors when using MinWG
        # Remove the error flag from Mbed TLS targets
        target_compile_options(mbedtls PRIVATE -Wno-error=format)
        target_compile_options(mbedcrypto PRIVATE -Wno-error=format)
        target_compile_options(mbedx509 PRIVATE -Wno-error=format)
    endif()
endif()

### TinyCBOR (provided by the KNX-IoT stack) ###
if(CONFIG_KNXIOT_TINYCBOR_BUILTIN)
    # Set options for TinyCBOR
    # default:
    # option(WITH_FLOATING_POINT "Use floating point code in TinyCBOR" ON)          # OFF  # TODO FIXME we use floating point, why were the sources not added in the old submodule approach?
    # option(WITH_FREESTANDING "Compile TinyCBOR in C freestanding mode" OFF)       # OFF  # TODO Do we want to set this to ON or OFF?
    # if(WITH_FLOATING_POINT AND NOT WITH_FREESTANDING)
    #   option(WITH_CBOR2JSON "Compile code to convert from CBOR to JSON" ON)       # OFF  # TODO Do we want/can we set this to OFF to reduce size?
    #   option(BUILD_EXAMPLES "Compile the TinyCBOR examples" OFF)
    #   option(BUILD_TOOLS "Compile the TinyCBOR tools" ON)
    # endif() 
    #set(WITH_FLOATING_POINT ON CACHE BOOL "" FORCE)
    #set(WITH_FREESTANDING OFF CACHE BOOL "" FORCE)
    #set(WITH_CBOR2JSON OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_TOOLS ON CACHE BOOL "" FORCE)

    FetchContent_Declare(
        tinycbor
        GIT_REPOSITORY https://github.com/intel/tinycbor.git
        GIT_TAG v7.0
    )

    FetchContent_MakeAvailable(tinycbor)
endif()

### mDNS/DNS-SD library ###
# Note:
# Use git apply instead of patch for cross-platform compatibility (patch not available on Windows).
FetchContent_Declare(
    mdns
    GIT_REPOSITORY https://github.com/mjansson/mdns.git
    # Note:
    # No working tag for all platforms yet, so stay with main for now.
    GIT_TAG main
    # Note:
    # Issues #96 and #97 have not been fixed yet.
    PATCH_COMMAND ${CMAKE_COMMAND} -DPATCH_FILE=${PROJECT_SOURCE_DIR}/port/mdns.patch -P "${PROJECT_SOURCE_DIR}/cmake/patch.cmake"
)

FetchContent_MakeAvailable(mdns)

target_sources(kis-port PUBLIC
    ${PORT_DIR}/abort.c
    ${PORT_DIR}/clock.c
    ${CMAKE_CURRENT_SOURCE_DIR}/dns-sd.c
    ${PORT_DIR}/ipadapter.c
    ${PORT_DIR}/oc_network_interface.c
    ${PORT_DIR}/random.c
    ${PORT_DIR}/storage.c
    ${PORT_DIR}/tcpadapter.c
)

target_include_directories(kis-port 
    PRIVATE "${PROJECT_SOURCE_DIR}"
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    # Platform-specific include path (for ipadapter.h / ipcontext.h used by dns-sd.c)
    PUBLIC "${PORT_DIR}"
)
target_include_directories(kis-port PRIVATE "${tinycbor_SOURCE_DIR}/src" PRIVATE "${tinycbor_BINARY_DIR}")
# mDNS header-only library (mdns/mdns.h)
# Note:
# PUBLIC because dns-sd.c is a PUBLIC source that gets compiled by targets linking to kis-port.
target_include_directories(kis-port PUBLIC "${mdns_SOURCE_DIR}")

## KNX-IoT Stack - Platform Port Options ##
target_link_libraries(kis-port PUBLIC kis-common)
if(CONFIG_KNXIOT_MBEDTLS_BUILTIN)
    target_link_libraries(kis-port PUBLIC mbedtls)
endif()

if(CONFIG_KNX_LOG_TO_FILE)
    target_compile_definitions(kis-port INTERFACE KNX_LOG_TO_FILE)
endif()
