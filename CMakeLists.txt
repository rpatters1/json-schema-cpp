cmake_minimum_required(VERSION 3.21)

project(json-schema-cpp
    VERSION 0.0.1
    DESCRIPTION "A complete JSON Schema 2020-12 validator for nlohmann::json"
    HOMEPAGE_URL "https://github.com/rpatters1/json-schema-cpp"
    LANGUAGES CXX)

set(JSONSCHEMA_IS_TOP_LEVEL ${PROJECT_IS_TOP_LEVEL})

option(JSONSCHEMA_BUILD_TESTS "Build json-schema-cpp tests" ${JSONSCHEMA_IS_TOP_LEVEL})
option(JSONSCHEMA_INSTALL "Generate install rules for json-schema-cpp" ${JSONSCHEMA_IS_TOP_LEVEL})
option(JSONSCHEMA_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(JSONSCHEMA_USE_SYSTEM_NLOHMANN_JSON "Require an installed nlohmann_json instead of fetching it if missing" OFF)
set(JSONSCHEMA_SANITIZE "" CACHE STRING "Comma-separated sanitizers for this project's targets (e.g. address,undefined)")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(JsonSchemaDependencies)
include(JsonSchemaWarnings)

add_subdirectory(src)

if(JSONSCHEMA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(JSONSCHEMA_INSTALL)
    include(JsonSchemaInstall)
endif()
