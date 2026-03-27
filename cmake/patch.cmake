# patch.cmake

# Write to a log file to confirm script execution
file(WRITE "${CMAKE_BINARY_DIR}/patch.log" "patch.cmake executed at ${CMAKE_CURRENT_LIST_FILE}\n")
file(APPEND "${CMAKE_BINARY_DIR}/patch.log" "PATCH_FILE: ${PATCH_FILE}\n")
file(APPEND "${CMAKE_BINARY_DIR}/patch.log" "CMAKE_BINARY_DIR: ${CMAKE_BINARY_DIR}\n")

# Find Git executable (use QUIET to prevent errors in script mode if not found)
find_package(Git QUIET)
if(NOT GIT_EXECUTABLE)
    # Fallback: search for git in PATH
    find_program(GIT_EXECUTABLE git)
endif()

file(APPEND "${CMAKE_BINARY_DIR}/patch.log" "GIT_EXECUTABLE: ${GIT_EXECUTABLE}\n")

if(NOT GIT_EXECUTABLE)
    file(APPEND "${CMAKE_BINARY_DIR}/patch.log" "ERROR: Git executable not found!\n")
    message(FATAL_ERROR "Git executable not found!")
endif()

# Detect system and set NULL_DEVICE
if(WIN32 AND NOT MINGW)
    set(NULL_DEVICE NUL)
else()
    set(NULL_DEVICE /dev/null)
endif()

file(APPEND "${CMAKE_BINARY_DIR}/patch.log" "Applying patch...\n")
message(STATUS "patch.cmake: Applying patch from ${PATCH_FILE}")

execute_process(
    COMMAND
        ${GIT_EXECUTABLE} apply --check -p1 --ignore-whitespace "${PATCH_FILE}"
    WORKING_DIRECTORY .
    RESULT_VARIABLE check_result
    OUTPUT_VARIABLE check_output
    ERROR_VARIABLE check_error
)

file(APPEND "${CMAKE_BINARY_DIR}/patch.log" "Check patch result: ${check_result}\n")
file(APPEND "${CMAKE_BINARY_DIR}/patch.log" "Check patch output: ${check_output}\n")
file(APPEND "${CMAKE_BINARY_DIR}/patch.log" "Check patch error: ${check_error}\n")

if(check_result EQUAL 0)
    message(STATUS "patch.cmake: Patch ready to apply")
    execute_process(
        COMMAND
            ${GIT_EXECUTABLE} apply -p1 --ignore-whitespace "${PATCH_FILE}"
        WORKING_DIRECTORY .
        RESULT_VARIABLE patch_result
        OUTPUT_VARIABLE patch_output
        ERROR_VARIABLE patch_error
    )
    file(APPEND "${CMAKE_BINARY_DIR}/patch.log" "Apply patch result: ${patch_result}\n")
    file(APPEND "${CMAKE_BINARY_DIR}/patch.log" "Apply patch output: ${patch_output}\n")
    file(APPEND "${CMAKE_BINARY_DIR}/patch.log" "Apply patch error: ${patch_error}\n")
    
    if(patch_result EQUAL 0)
        message(STATUS "patch.cmake: Patch successfully applied.")
        file(APPEND "${CMAKE_BINARY_DIR}/patch.log" "Patch successfully applied.\n")
    else()
        message(STATUS "patch.cmake: Patch failed with code ${patch_result}!")
        file(APPEND "${CMAKE_BINARY_DIR}/patch.log" "Patch failed with code ${patch_result}!\n")
    endif()
else()
    message(STATUS "patch.cmake: Patch already applied.")
    file(APPEND "${CMAKE_BINARY_DIR}/patch.log" "Patch already applied.\n")
endif()

file(APPEND "${CMAKE_BINARY_DIR}/patch.log" "patch.cmake completed\n")
