cmake_minimum_required(VERSION 3.20)
project(cget VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CGET_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(CGET_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(CGET_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(CGET_BUILD_INTEGRATION_TESTS "Build integration tests" OFF)
option(CGET_BUILD_STRESS_TESTS "Build stress test executable" OFF)

if (CGET_ENABLE_ASAN AND CGET_ENABLE_TSAN)
    message(FATAL_ERROR "CGET_ENABLE_ASAN and CGET_ENABLE_TSAN cannot both be enabled")
endif()

find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

add_library(cget_core
        src/cli/cli_parser.cpp
        src/config/config_manager.cpp
        src/core/chunk_planner.cpp
        src/core/download_manager.cpp
        src/core/errors.cpp
        src/core/logger.cpp
        src/core/task_state.cpp
        src/core/types.cpp
        src/crypto/sha256.cpp
        src/engine/download_engine.cpp
        src/filesystem/filesystem_service.cpp
        src/network/http_protocol_handler.cpp
        src/network/protocol_registry.cpp
        src/persistence/json.cpp
        src/persistence/persistence_store.cpp
)

target_include_directories(cget_core PUBLIC include)
target_link_libraries(cget_core PUBLIC CURL::libcurl Threads::Threads)

if (MSVC)
    target_compile_options(cget_core PRIVATE /W4)
    if (CGET_WARNINGS_AS_ERRORS)
        target_compile_options(cget_core PRIVATE /WX)
    endif()
else()
    target_compile_options(cget_core PRIVATE -Wall -Wextra -Wpedantic)
    if (CGET_WARNINGS_AS_ERRORS)
        target_compile_options(cget_core PRIVATE -Werror)
    endif()
endif()

function(cget_apply_sanitizers target)
    if (CGET_ENABLE_ASAN)
        target_compile_options(${target} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address)
    endif()
    if (CGET_ENABLE_TSAN)
        target_compile_options(${target} PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=thread)
    endif()
endfunction()

if (NOT MSVC)
    cget_apply_sanitizers(cget_core)
endif()

add_executable(cget main.cpp)
target_link_libraries(cget PRIVATE cget_core)
if (NOT MSVC)
    cget_apply_sanitizers(cget)
endif()

enable_testing()
add_executable(cget_unit_tests
        tests/unit/test_core.cpp
)
target_link_libraries(cget_unit_tests PRIVATE cget_core)
if (NOT MSVC)
    cget_apply_sanitizers(cget_unit_tests)
endif()
add_test(NAME cget_unit_tests COMMAND cget_unit_tests)

if (CGET_BUILD_STRESS_TESTS)
    add_executable(cget_stress_tests tests/stress/stress_queue.cpp)
    target_link_libraries(cget_stress_tests PRIVATE cget_core)
    if (NOT MSVC)
        cget_apply_sanitizers(cget_stress_tests)
    endif()
    add_test(NAME cget_stress_tests COMMAND cget_stress_tests)
endif()
