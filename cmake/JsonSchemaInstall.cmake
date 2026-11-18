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
install(FILES ${PROJECT_BINARY_DIR}/include/jsonschema/export.hpp
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
    COMPATIBILITY SameMinorVersion)

install(FILES
    ${PROJECT_BINARY_DIR}/jsonschemaConfig.cmake
    ${PROJECT_BINARY_DIR}/jsonschemaConfigVersion.cmake
    DESTINATION ${JSONSCHEMA_CMAKE_DIR})

install(FILES ${PROJECT_SOURCE_DIR}/LICENSE ${PROJECT_SOURCE_DIR}/NOTICE
    DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES ${PROJECT_SOURCE_DIR}/src/metaschemas/LICENSE
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    RENAME LICENSE.metaschemas)
