# SPDX-FileCopyrightText: 2026 Robert Patterson
# SPDX-License-Identifier: MIT

add_executable(json-schema-cpp main.cpp)
target_link_libraries(json-schema-cpp PRIVATE jsonschema::jsonschema)
set_target_properties(json-schema-cpp PROPERTIES CXX_EXTENSIONS OFF)
jsonschema_configure_target(json-schema-cpp)

if(JSONSCHEMA_INSTALL)
    include(GNUInstallDirs)
    install(TARGETS json-schema-cpp RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
