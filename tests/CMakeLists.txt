# Tests for the KNX-IoT Point API Stack
#
# Layout:
#   tests/unit/     — Google Test unit tests (C++ exercising C functions)
#   tests/runtime/  — Python/pytest network-level conformance tests

cmake_minimum_required(VERSION 3.21)

# ── Unit tests (Google Test) ───────────────────────────────────────────────────
add_subdirectory(unit)

# ── Runtime test server ────────────────────────────────────────────────────────
# Headless CoAP device for network-level conformance tests (like EITT).
# NOT a GTest binary — started by the Python test suite in tests/runtime/.
# Only built on platforms with real networking (not mock port).
add_executable(runtime_test_server runtime/runtime_test_server.c)

target_include_directories(runtime_test_server PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/port
    ${PROJECT_SOURCE_DIR}/api
)

target_link_libraries(runtime_test_server PRIVATE
    kisTestControl
    kisClientServer
    kisClientServer_tail
)

# Don't register as a CTest target — it's a server, not a test.
# The Python tests drive it via subprocess.
