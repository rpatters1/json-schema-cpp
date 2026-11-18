# ---------------------------------------------------------------------------
# Unit tests (label: unit). These must always pass.
# ---------------------------------------------------------------------------
add_executable(jsonschema_unit_tests
    unit/main.cpp
    unit/json_access_test.cpp
    unit/json_pointer_test.cpp
    unit/schema_test.cpp
    unit/uri_test.cpp
)
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
