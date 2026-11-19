# SPDX-FileCopyrightText: 2026 Robert Patterson
# SPDX-License-Identifier: MIT
#
# A consumer of the installed package, built outside the main project:
#   cmake -S tests/package -B build-package -DCMAKE_PREFIX_PATH=<install prefix>
#   cmake --build build-package && ctest --test-dir build-package
cmake_minimum_required(VERSION 3.21)
project(jsonschema_package_test LANGUAGES CXX)

find_package(jsonschema 1.0 REQUIRED CONFIG)

add_executable(package_test main.cpp)
target_link_libraries(package_test PRIVATE jsonschema::jsonschema)

enable_testing()
add_test(NAME package_test COMMAND package_test)
if(WIN32)
    # The installed DLLs are in <prefix>/bin.
    set_tests_properties(package_test PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${jsonschema_DIR}/../../../bin")
endif()
