# EMS specific stuff

## iot device specific resource variables
set(KNX_GOT_MAX_ENTRIES "50" CACHE STRING "Maximum number of group object table entries")
set(KNX_GPT_MAX_ENTRIES "20" CACHE STRING "Maximum number of group publisher table entries")
set(KNX_GRT_MAX_ENTRIES "20" CACHE STRING "Maximum number of group recipient table entries")
set(KNX_SEC_MAX_ENTRIES "20" CACHE STRING "Maximum number of access token table entries")
set(KNX_PAGE_SIZE "20" CACHE STRING "Server default page size for pagination")
set(KNX_SPAKE_ITERATIONS "50000" CACHE STRING "Number of SHA256 iterations used within the SPAKE2+ handshake")

if(WIN32)     
        add_executable(PV_virtual_gui WIN32
            ${PROJECT_SOURCE_DIR}/apps/EMS/knx_iot_virtual_PV.cpp
            ${PROJECT_SOURCE_DIR}/apps/EMS/knx_iot_virtual_PV.c
            ${PROJECT_SOURCE_DIR}/apps/knx_iot_virtual.c
            ${MANIFEST_RC} 
        )

        add_executable(CEM_virtual_gui WIN32
            ${PROJECT_SOURCE_DIR}/apps/EMS/knx_iot_virtual_CEM.cpp
            ${PROJECT_SOURCE_DIR}/apps/EMS/knx_iot_virtual_CEM.c
            ${PROJECT_SOURCE_DIR}/apps/knx_iot_virtual.c
            ${MANIFEST_RC} 
        )

        add_executable(Charger_virtual_gui WIN32
            ${PROJECT_SOURCE_DIR}/apps/EMS/knx_iot_virtual_Charger.cpp
            ${PROJECT_SOURCE_DIR}/apps/EMS/knx_iot_virtual_Charger.c
            ${PROJECT_SOURCE_DIR}/apps/knx_iot_virtual.c
            ${MANIFEST_RC} 
        )              

        target_link_libraries(PV_virtual_gui wx::net wx::core wx::base wx::media kisClientServer)
        target_compile_definitions(PV_virtual_gui PUBLIC KNX_GUI)

        target_link_libraries(CEM_virtual_gui wx::net wx::core wx::base wx::media kisClientServer)
        target_compile_definitions(CEM_virtual_gui PUBLIC KNX_GUI)

        target_link_libraries(Charger_virtual_gui wx::net wx::core wx::base wx::media kisClientServer)
        target_compile_definitions(Charger_virtual_gui PUBLIC KNX_GUI)

        set(GUI_FLAG "/SUBSYSTEM:WINDOWS")            
        set_target_properties(PV_virtual_gui PROPERTIES LINK_FLAGS ${GUI_FLAG})
        set_target_properties(CEM_virtual_gui PROPERTIES LINK_FLAGS ${GUI_FLAG})
        set_target_properties(Charger_virtual_gui PROPERTIES LINK_FLAGS ${GUI_FLAG})        

endif()

set(KNX_INCLUDE_DIRS 
    ${PROJECT_SOURCE_DIR}/
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/security
    ${PROJECT_SOURCE_DIR}/messaging/coap/
    ${PROJECT_SOURCE_DIR}/api
    ${PROJECT_SOURCE_DIR}/api/c-timestamp
)

# Process individual stack/ (demo)application behavior
## Sizes (tables, pagination, ...)
add_compile_definitions(GOT_MAX_ENTRIES=${KNX_GOT_MAX_ENTRIES})     # group object table size
add_compile_definitions(GPT_MAX_ENTRIES=${KNX_GPT_MAX_ENTRIES})     # publisher table size
add_compile_definitions(GRT_MAX_ENTRIES=${KNX_GRT_MAX_ENTRIES})     # recipient table size
add_compile_definitions(G_AT_MAX_ENTRIES=${KNX_SEC_MAX_ENTRIES})    # access token table size (independent from OS_OSCORE, hence not added to target compile definitions)
add_compile_definitions(PAGE_SIZE=${KNX_PAGE_SIZE})                 # server default page size
