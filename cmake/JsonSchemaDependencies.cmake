include(FetchContent)

# nlohmann_json >= 3.11: use an installed package if present, otherwise fetch it.
set(JSONSCHEMA_NLOHMANN_JSON_MIN_VERSION 3.11)
set(JSONSCHEMA_NLOHMANN_JSON_FETCH_VERSION 3.11.3)

if(NOT TARGET nlohmann_json::nlohmann_json)
    if(JSONSCHEMA_USE_SYSTEM_NLOHMANN_JSON)
        find_package(nlohmann_json ${JSONSCHEMA_NLOHMANN_JSON_MIN_VERSION} REQUIRED CONFIG)
    else()
        find_package(nlohmann_json ${JSONSCHEMA_NLOHMANN_JSON_MIN_VERSION} QUIET CONFIG)
    endif()
endif()

if(NOT TARGET nlohmann_json::nlohmann_json)
    message(STATUS "json-schema-cpp: fetching nlohmann_json ${JSONSCHEMA_NLOHMANN_JSON_FETCH_VERSION}")
    set(JSON_BuildTests OFF CACHE INTERNAL "")
    set(JSON_Install ${JSONSCHEMA_INSTALL} CACHE INTERNAL "")
    FetchContent_Declare(nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v${JSONSCHEMA_NLOHMANN_JSON_FETCH_VERSION}/json.tar.xz
        URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(nlohmann_json)
else()
    message(STATUS "json-schema-cpp: using nlohmann_json ${nlohmann_json_VERSION}")
endif()

if(JSONSCHEMA_BUILD_TESTS)
    if(NOT TARGET doctest::doctest)
        find_package(doctest 2.4 QUIET CONFIG)
    endif()
    if(NOT TARGET doctest::doctest)
        # doctest is header-only. Its own CMakeLists.txt declares a minimum CMake version
        # that CMake 4 rejects, so it is never added; only the header directory is used.
        message(STATUS "json-schema-cpp: fetching doctest")
        FetchContent_Declare(doctest
            URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.11.tar.gz
            URL_HASH SHA256=632ed2c05a7f53fa961381497bf8069093f0d6628c5f26286161fbd32a560186
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            SOURCE_SUBDIR do-not-add-subdirectory)
        FetchContent_MakeAvailable(doctest)
        add_library(jsonschema_doctest INTERFACE)
        target_include_directories(jsonschema_doctest SYSTEM INTERFACE ${doctest_SOURCE_DIR})
        add_library(doctest::doctest ALIAS jsonschema_doctest)
    endif()
endif()
