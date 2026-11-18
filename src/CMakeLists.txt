# SPDX-FileCopyrightText: 2026 Robert Patterson
# SPDX-License-Identifier: MIT
#
# Sources are compiled once into an object library. The public library is built from
# those objects, and the unit tests link the same objects directly, so tests can
# reach internal (non-exported) functions in shared builds too.
# The 2020-12 meta-schemas, embedded as raw string literals (src/metaschemas/README.md).
set(JSONSCHEMA_METASCHEMA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/metaschemas/2020-12)
set(JSONSCHEMA_METASCHEMAS
    schema
    meta/applicator meta/content meta/core meta/format-annotation meta/format-assertion
    meta/meta-data meta/unevaluated meta/validation)
set(JSONSCHEMA_METASCHEMA_ENTRIES "")
list(LENGTH JSONSCHEMA_METASCHEMAS JSONSCHEMA_METASCHEMA_COUNT)
foreach(name IN LISTS JSONSCHEMA_METASCHEMAS)
    set(file ${JSONSCHEMA_METASCHEMA_DIR}/${name}.json)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${file})
    file(READ ${file} content)
    string(APPEND JSONSCHEMA_METASCHEMA_ENTRIES
        "    {\"https://json-schema.org/draft/2020-12/${name}\", R\"jsonschema(${content})jsonschema\"},\n")
endforeach()
configure_file(metaschemas/metaschemas.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/metaschemas.cpp @ONLY)

add_library(jsonschema_objects OBJECT
    compiler.cpp
    eval.cpp
    formats.cpp
    json/access.cpp
    json/pointer.cpp
    keywords/applicator.cpp
    keywords/core.cpp
    keywords/keywords.cpp
    keywords/validation.cpp
    output.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/metaschemas.cpp
    regex.cpp
    registry.cpp
    schema.cpp
    unicode/idna.cpp
    unicode/unicode.cpp
    uri.cpp
)

set(JSONSCHEMA_GENERATED_INCLUDE_DIR ${PROJECT_BINARY_DIR}/include)

target_compile_features(jsonschema_objects PUBLIC cxx_std_20)
target_include_directories(jsonschema_objects
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${JSONSCHEMA_GENERATED_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(jsonschema_objects PUBLIC nlohmann_json::nlohmann_json)

# The regex backend is private to the library: no public header includes it (doc 5.2).
if(JSONSCHEMA_REGEX STREQUAL "srell")
    if(JSONSCHEMA_USE_SYSTEM_SRELL)
        find_path(JSONSCHEMA_SRELL_INCLUDE_DIR srell.hpp REQUIRED)
    else()
        set(JSONSCHEMA_SRELL_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/third_party/srell)
    endif()
    target_include_directories(jsonschema_objects SYSTEM PRIVATE ${JSONSCHEMA_SRELL_INCLUDE_DIR})
    target_compile_definitions(jsonschema_objects PRIVATE JSONSCHEMA_REGEX_SRELL)
elseif(NOT JSONSCHEMA_REGEX STREQUAL "std")
    message(FATAL_ERROR "JSONSCHEMA_REGEX must be srell or std, not ${JSONSCHEMA_REGEX}")
endif()

set_target_properties(jsonschema_objects PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
if(BUILD_SHARED_LIBS)
    set_target_properties(jsonschema_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_definitions(jsonschema_objects PUBLIC jsonschema_EXPORTS)
else()
    target_compile_definitions(jsonschema_objects PUBLIC JSONSCHEMA_STATIC_DEFINE)
endif()
jsonschema_configure_target(jsonschema_objects)

add_library(jsonschema $<TARGET_OBJECTS:jsonschema_objects>)
add_library(jsonschema::jsonschema ALIAS jsonschema)

include(GenerateExportHeader)
generate_export_header(jsonschema
    BASE_NAME JSONSCHEMA
    EXPORT_FILE_NAME ${JSONSCHEMA_GENERATED_INCLUDE_DIR}/jsonschema/export.hpp)

target_compile_features(jsonschema PUBLIC cxx_std_20)
target_include_directories(jsonschema
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${JSONSCHEMA_GENERATED_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(jsonschema PUBLIC nlohmann_json::nlohmann_json)
if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(jsonschema PUBLIC JSONSCHEMA_STATIC_DEFINE)
endif()
set_target_properties(jsonschema PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR})
jsonschema_configure_target(jsonschema)
