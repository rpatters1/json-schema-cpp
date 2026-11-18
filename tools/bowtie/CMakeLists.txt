# SPDX-FileCopyrightText: 2026 Robert Patterson
# SPDX-License-Identifier: MIT

add_executable(jsonschema_bowtie_harness harness.cpp)
target_link_libraries(jsonschema_bowtie_harness PRIVATE jsonschema::jsonschema)
target_compile_definitions(jsonschema_bowtie_harness PRIVATE JSONSCHEMA_VERSION="${PROJECT_VERSION}")
set_target_properties(jsonschema_bowtie_harness PROPERTIES CXX_EXTENSIONS OFF)
jsonschema_configure_target(jsonschema_bowtie_harness)

if(JSONSCHEMA_BUILD_TESTS)
    # A scripted protocol session: start, dialect, run (flag and annotations), stop.
    add_test(NAME bowtie/session
        COMMAND ${CMAKE_COMMAND}
            -DHARNESS=$<TARGET_FILE:jsonschema_bowtie_harness>
            -DSESSION=${CMAKE_CURRENT_SOURCE_DIR}/session.jsonl
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/session.expected.jsonl
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run-session.cmake)
    set_tests_properties(bowtie/session PROPERTIES LABELS bowtie)
endif()
