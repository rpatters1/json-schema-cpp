# SPDX-FileCopyrightText: 2026 Robert Patterson
# SPDX-License-Identifier: MIT
#
# Feeds SESSION to HARNESS on standard input and compares its output with EXPECTED,
# ignoring the implementation metadata line (which names the compiler).

execute_process(
    COMMAND ${HARNESS}
    INPUT_FILE ${SESSION}
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
    RESULT_VARIABLE status)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "harness exited with ${status}: ${errors}")
endif()
file(READ ${EXPECTED} expected)
string(REPLACE "\r\n" "\n" output "${output}")
string(REPLACE "\r\n" "\n" expected "${expected}")
# Drop the start response (line 1) from both before comparing. (Not REGEX REPLACE with
# "^": CMake applies it at every match position, which would drop every line.)
foreach(var output expected)
    string(FIND "${${var}}" "\n" newline)
    math(EXPR rest "${newline} + 1")
    string(SUBSTRING "${${var}}" ${rest} -1 ${var})
endforeach()
if(NOT output STREQUAL expected)
    message(FATAL_ERROR "unexpected harness output:\n${output}\nexpected:\n${expected}")
endif()
