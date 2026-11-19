# SPDX-FileCopyrightText: 2026 Robert Patterson
# SPDX-License-Identifier: MIT
#
# ---------------------------------------------------------------------------
# Unit tests (label: unit). These must always pass.
# ---------------------------------------------------------------------------
add_executable(jsonschema_unit_tests
    unit/main.cpp
    unit/classification_test.cpp
    unit/content_test.cpp
    unit/custom_keyword_test.cpp
    unit/format_test.cpp
    unit/idna_test.cpp
    unit/json_access_test.cpp
    unit/json_pointer_test.cpp
    unit/output_test.cpp
    unit/regex_test.cpp
    unit/schema_test.cpp
    unit/uri_test.cpp
    unit/validate_test.cpp
    unit/version_test.cpp
)
if(JSONSCHEMA_REGEX STREQUAL "srell")
    target_compile_definitions(jsonschema_unit_tests PRIVATE JSONSCHEMA_REGEX_SRELL)
endif()
find_package(Threads REQUIRED)
target_link_libraries(jsonschema_unit_tests PRIVATE jsonschema_objects doctest::doctest Threads::Threads)
set_target_properties(jsonschema_unit_tests PROPERTIES CXX_EXTENSIONS OFF)
jsonschema_configure_target(jsonschema_unit_tests)
# doctest (2.4.11 and 2.4.12) includes <ciso646> under Clang, which libstdc++ 15 flags
# with a #warning in C++20 mode.
target_compile_options(jsonschema_unit_tests PRIVATE "$<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-#warnings>")

add_test(NAME unit COMMAND jsonschema_unit_tests)
set_tests_properties(unit PROPERTIES LABELS unit)

# ---------------------------------------------------------------------------
# Official JSON-Schema-Test-Suite (label: suite). One CTest test per suite file.
# ---------------------------------------------------------------------------
set(JSONSCHEMA_TEST_SUITE_DIR "${PROJECT_SOURCE_DIR}/third_party/JSON-Schema-Test-Suite"
    CACHE PATH "Location of the JSON-Schema-Test-Suite checkout")

add_executable(jsonschema_suite_runner suite/suite_runner.cpp)
target_link_libraries(jsonschema_suite_runner PRIVATE jsonschema::jsonschema)
set_target_properties(jsonschema_suite_runner PROPERTIES CXX_EXTENSIONS OFF)
jsonschema_configure_target(jsonschema_suite_runner)

set(JSONSCHEMA_SUITE_TESTS_DIR "${JSONSCHEMA_TEST_SUITE_DIR}/validation/tests")

add_executable(jsonschema_annotation_runner suite/annotation_runner.cpp)
target_link_libraries(jsonschema_annotation_runner PRIVATE jsonschema::jsonschema)
set_target_properties(jsonschema_annotation_runner PROPERTIES CXX_EXTENSIONS OFF)
jsonschema_configure_target(jsonschema_annotation_runner)
set(JSONSCHEMA_ANNOTATION_TESTS_DIR "${JSONSCHEMA_TEST_SUITE_DIR}/annotations/tests")
if(EXISTS "${JSONSCHEMA_ANNOTATION_TESTS_DIR}")
    add_test(NAME annotations
        COMMAND jsonschema_annotation_runner --tests-dir "${JSONSCHEMA_ANNOTATION_TESTS_DIR}" --verbose)
    set_tests_properties(annotations PROPERTIES LABELS "suite;suite-annotations")
endif()
set(JSONSCHEMA_EXPECTED_FAILURES "${CMAKE_CURRENT_SOURCE_DIR}/suite/expected-failures.json")

if(EXISTS "${JSONSCHEMA_SUITE_TESTS_DIR}")
    file(GLOB JSONSCHEMA_SUITE_FILES CONFIGURE_DEPENDS
        RELATIVE "${JSONSCHEMA_SUITE_TESTS_DIR}"
        "${JSONSCHEMA_SUITE_TESTS_DIR}/*.json"
        "${JSONSCHEMA_SUITE_TESTS_DIR}/optional/*.json")
    foreach(suite_file IN LISTS JSONSCHEMA_SUITE_FILES)
        string(REGEX REPLACE "\\.json$" "" suite_name "${suite_file}")
        add_test(NAME "suite/${suite_name}"
            COMMAND jsonschema_suite_runner
                --tests-dir "${JSONSCHEMA_SUITE_TESTS_DIR}"
                --expected-failures "${JSONSCHEMA_EXPECTED_FAILURES}"
                --regex-backend ${JSONSCHEMA_REGEX}
                "${suite_file}")
        if(suite_file MATCHES "^optional/")
            set(suite_labels "suite;suite-optional")
        else()
            set(suite_labels "suite;suite-required")
        endif()
        # Exit code 77: the file has no test cases that apply to 2020-12.
        set_tests_properties("suite/${suite_name}" PROPERTIES LABELS "${suite_labels}" SKIP_RETURN_CODE 77)
    endforeach()
else()
    message(WARNING "JSON-Schema-Test-Suite not found at ${JSONSCHEMA_TEST_SUITE_DIR}; "
                    "run `git submodule update --init` to enable suite tests.")
endif()

# ---------------------------------------------------------------------------
# Command-line tool (label: cli)
# ---------------------------------------------------------------------------
if(JSONSCHEMA_BUILD_CLI)
    set(cli_dir ${CMAKE_CURRENT_SOURCE_DIR}/cli)
    # name; expected exit status; regex the output must match; arguments...
    function(jsonschema_cli_test name status pattern)
        add_test(NAME cli/${name} COMMAND json-schema-cpp ${ARGN})
        set_tests_properties(cli/${name} PROPERTIES LABELS cli PASS_REGULAR_EXPRESSION "${pattern}")
        # PASS_REGULAR_EXPRESSION ignores the exit status, so check it with a second test.
        add_test(NAME cli/${name}/status COMMAND json-schema-cpp --quiet ${ARGN})
        set_tests_properties(cli/${name}/status PROPERTIES LABELS cli)
        if(NOT status EQUAL 0)
            set_tests_properties(cli/${name}/status PROPERTIES WILL_FAIL TRUE)
        endif()
    endfunction()
    jsonschema_cli_test(valid 0 "triangle.json: valid" ${cli_dir}/polygon.json ${cli_dir}/triangle.json)
    jsonschema_cli_test(invalid 1 "/1: the required property \"y\" is missing" ${cli_dir}/polygon.json ${cli_dir}/broken.json)
    jsonschema_cli_test(detailed 1 "\"keywordLocation\": \"/items/\\$ref\"" --output detailed ${cli_dir}/polygon.json ${cli_dir}/broken.json)
    jsonschema_cli_test(annotate-formats 0 "bad-date.json: valid" ${cli_dir}/date.json ${cli_dir}/bad-date.json)
    jsonschema_cli_test(assert-formats 1 "is not a valid date" --assert-formats ${cli_dir}/date.json ${cli_dir}/bad-date.json)
    jsonschema_cli_test(annotate-content 0 "bad-content.json: valid" ${cli_dir}/content.json ${cli_dir}/bad-content.json)
    jsonschema_cli_test(assert-content 1 "is not valid base64" --assert-content ${cli_dir}/content.json ${cli_dir}/bad-content.json)
    jsonschema_cli_test(flag 0 "\"valid\": true" --output flag ${cli_dir}/polygon.json ${cli_dir}/triangle.json)
    add_test(NAME cli/invalid-schema COMMAND json-schema-cpp ${cli_dir}/invalid-schema.json ${cli_dir}/triangle.json)
    set_tests_properties(cli/invalid-schema PROPERTIES LABELS cli
        PASS_REGULAR_EXPRESSION "not valid against its meta-schema")
endif()
