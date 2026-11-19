# SPDX-FileCopyrightText: 2026 Robert Patterson
# SPDX-License-Identifier: MIT

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS jsonschema
    EXPORT jsonschemaTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/jsonschema
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES
    ${PROJECT_BINARY_DIR}/include/jsonschema/export.hpp
    ${PROJECT_BINARY_DIR}/include/jsonschema/version.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/jsonschema)

set(JSONSCHEMA_CMAKE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/jsonschema)

install(EXPORT jsonschemaTargets
    NAMESPACE jsonschema::
    DESTINATION ${JSONSCHEMA_CMAKE_DIR})

configure_package_config_file(
    ${PROJECT_SOURCE_DIR}/cmake/jsonschemaConfig.cmake.in
    ${PROJECT_BINARY_DIR}/jsonschemaConfig.cmake
    INSTALL_DESTINATION ${JSONSCHEMA_CMAKE_DIR})
write_basic_package_version_file(
    ${PROJECT_BINARY_DIR}/jsonschemaConfigVersion.cmake
    COMPATIBILITY SameMajorVersion)

install(FILES
    ${PROJECT_BINARY_DIR}/jsonschemaConfig.cmake
    ${PROJECT_BINARY_DIR}/jsonschemaConfigVersion.cmake
    DESTINATION ${JSONSCHEMA_CMAKE_DIR})

install(FILES ${PROJECT_SOURCE_DIR}/LICENSE ${PROJECT_SOURCE_DIR}/NOTICE
    DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES ${PROJECT_SOURCE_DIR}/src/metaschemas/LICENSE
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    RENAME LICENSE.metaschemas)
# The IDNA tables are always built in; SRELL's Unicode tables are under the same licence.
install(FILES ${PROJECT_SOURCE_DIR}/src/unicode/LICENSE
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    RENAME LICENSE.unicode)
if(JSONSCHEMA_REGEX STREQUAL "srell" AND NOT JSONSCHEMA_USE_SYSTEM_SRELL)
    install(FILES ${PROJECT_SOURCE_DIR}/third_party/srell/LICENSE
        DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME LICENSE.srell)
endif()

# pkg-config. The prefix is found relative to the .pc file, so an install with
# `cmake --install --prefix` or DESTDIR stays usable. Install directories may be absolute
# (Nix, Guix); they are used as they are then.
if(IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
    set(JSONSCHEMA_PC_PREFIX "${CMAKE_INSTALL_PREFIX}")
else()
    file(RELATIVE_PATH JSONSCHEMA_PC_UP "/root/${CMAKE_INSTALL_LIBDIR}/pkgconfig" "/root")
    string(REGEX REPLACE "/$" "" JSONSCHEMA_PC_UP "${JSONSCHEMA_PC_UP}")
    set(JSONSCHEMA_PC_PREFIX "\${pcfiledir}/${JSONSCHEMA_PC_UP}")
endif()
foreach(kind LIBDIR INCLUDEDIR)
    if(IS_ABSOLUTE "${CMAKE_INSTALL_${kind}}")
        set(JSONSCHEMA_PC_${kind} "${CMAKE_INSTALL_${kind}}")
    else()
        set(JSONSCHEMA_PC_${kind} "\${prefix}/${CMAKE_INSTALL_${kind}}")
    endif()
endforeach()
if(BUILD_SHARED_LIBS)
    set(JSONSCHEMA_PC_CFLAGS "")
else()
    set(JSONSCHEMA_PC_CFLAGS " -DJSONSCHEMA_STATIC_DEFINE")
endif()
configure_file(${PROJECT_SOURCE_DIR}/cmake/jsonschema.pc.in
    ${PROJECT_BINARY_DIR}/jsonschema.pc @ONLY)
install(FILES ${PROJECT_BINARY_DIR}/jsonschema.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
