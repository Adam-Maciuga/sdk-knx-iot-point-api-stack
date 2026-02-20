cmake_minimum_required (VERSION 3.21)

### KNX-IoT Stack - Platform Ports ###
add_library(kis-port ${CMAKE_CURRENT_SOURCE_DIR}/oc_log.c)

# Detect the used platform and pick the right port
if(WIN32)
    ## KNX-IoT Stack - Windows Port ##
    set(PORT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/windows CACHE STRING "Platform port directory")
    target_link_libraries(kis-port PUBLIC iphlpapi) # add Windows SDK API helper 

    # Note:
    # set(libs ws2_32 bcrypt) moved to deps/CMakeLists.txt

    target_sources(kis-port PRIVATE
        ${PORT_DIR}/mutex.c
        ${PORT_DIR}/network_addresses.c
    ) 
elseif(UNIX)
    ## KNX-IoT Stack - Linux Port ##
    set(PORT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/linux CACHE STRING "Platform port directory")
    target_link_libraries(kis-port PUBLIC pthread m) # add Linux parallel execution helper 
endif()

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

target_include_directories(kis-port PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Platform-specific include path (for ipadapter.h / ipcontext.h used by dns-sd.c)
target_include_directories(kis-port PRIVATE ${PORT_DIR})
# mDNS header-only library (deps/mdns/mdns.h) — PUBLIC because dns-sd.c is a PUBLIC source
# that gets compiled by targets linking to kis-port
target_include_directories(kis-port PUBLIC ${PROJECT_SOURCE_DIR}/deps/mdns)

## KNX-IoT Stack - Platform Port Options ##
target_link_libraries(kis-port PUBLIC kis-common)
if(CONFIG_KNXIOT_MBEDTLS_BUILTIN)
    target_link_libraries(kis-port PUBLIC mbedtls)
endif()

if(CONFIG_KNXIOT_KNX_LOG_TO_FILE)
    target_compile_definitions(kis-port INTERFACE KNX_LOG_TO_FILE)
endif()
