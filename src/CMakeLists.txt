# Sources are compiled once into an object library. The public library is built from
# those objects, and the unit tests link the same objects directly, so tests can
# reach internal (non-exported) functions in shared builds too.
add_library(jsonschema_objects OBJECT
    json/access.cpp
    schema.cpp
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
