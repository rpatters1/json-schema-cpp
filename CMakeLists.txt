# SPDX-FileCopyrightText: 2026 Robert Patterson
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.21)

project(json-schema-cpp
    VERSION 1.0.0
    DESCRIPTION "A complete JSON Schema 2020-12 validator for nlohmann::json"
    HOMEPAGE_URL "https://github.com/rpatters1/json-schema-cpp"
    LANGUAGES CXX)

set(JSONSCHEMA_IS_TOP_LEVEL ${PROJECT_IS_TOP_LEVEL})

option(JSONSCHEMA_BUILD_TESTS "Build json-schema-cpp tests" ${JSONSCHEMA_IS_TOP_LEVEL})
option(JSONSCHEMA_INSTALL "Generate install rules for json-schema-cpp" ${JSONSCHEMA_IS_TOP_LEVEL})
option(JSONSCHEMA_BUILD_CLI "Build the json-schema-cpp command-line tool" ${JSONSCHEMA_IS_TOP_LEVEL})
option(JSONSCHEMA_BUILD_BOWTIE "Build the Bowtie harness (tools/bowtie)" ${JSONSCHEMA_BUILD_TESTS})
option(JSONSCHEMA_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(JSONSCHEMA_USE_SYSTEM_NLOHMANN_JSON "Require an installed nlohmann_json instead of fetching it if missing" OFF)
set(JSONSCHEMA_REGEX "srell" CACHE STRING "Regex backend for pattern/patternProperties: srell (ECMA-262, default) or std (std::regex)")
set_property(CACHE JSONSCHEMA_REGEX PROPERTY STRINGS srell std)
option(JSONSCHEMA_USE_SYSTEM_SRELL "Use an installed SRELL (e.g. vcpkg) instead of the vendored copy" OFF)
set(JSONSCHEMA_SANITIZE "" CACHE STRING "Comma-separated sanitizers for this project's targets (e.g. address,undefined)")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
# Put executables and DLLs side by side, so that Windows shared builds can run the tests
# (a missing DLL otherwise opens a modal error dialog that blocks CTest).
if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
endif()

include(JsonSchemaDependencies)
include(JsonSchemaWarnings)

add_subdirectory(src)
if(JSONSCHEMA_BUILD_CLI)
    add_subdirectory(tools/cli)
endif()
if(JSONSCHEMA_BUILD_BOWTIE)
    if(JSONSCHEMA_BUILD_TESTS)
        enable_testing()
    endif()
    add_subdirectory(tools/bowtie)
endif()

if(JSONSCHEMA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(JSONSCHEMA_INSTALL)
    include(JsonSchemaInstall)
endif()
