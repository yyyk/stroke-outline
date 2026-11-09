# Install rules and a CMake package, so other projects can use an installed copy:
#
#   cmake --preset native
#   cmake --build --preset native-release
#   cmake --install build/native --config Release --prefix <prefix>
#
#   # consumer's CMakeLists.txt
#   find_package(stroke_outline 0.1 REQUIRED)
#   target_link_libraries(app PRIVATE stroke_outline::stroke_outline)
#
# Installs the static library, the public headers (include/stroke_outline/ only;
# internal headers stay private) and the package files under
# <prefix>/lib/cmake/stroke_outline. On by default only for a top-level build,
# so pulling this project in with add_subdirectory/FetchContent doesn't add its
# install rules to the parent.
option(STROKE_OUTLINE_INSTALL "Generate install rules and a CMake package" ${PROJECT_IS_TOP_LEVEL})

if (NOT STROKE_OUTLINE_INSTALL)
    return()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(STROKE_OUTLINE_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/${TARGET_NAME}")

install(TARGETS ${TARGET_NAME}
    EXPORT ${TARGET_NAME}Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY include/stroke_outline
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp")

install(EXPORT ${TARGET_NAME}Targets
    NAMESPACE ${TARGET_NAME}::
    DESTINATION ${STROKE_OUTLINE_CMAKE_DIR})

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/${TARGET_NAME}Config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}Config.cmake"
    INSTALL_DESTINATION ${STROKE_OUTLINE_CMAKE_DIR})

# 0.x: only the same minor version is compatible.
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}ConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion)

# License and third-party notices, as the licenses require.
install(FILES LICENSE THIRD_PARTY_NOTICES
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/${TARGET_NAME})

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}ConfigVersion.cmake"
    DESTINATION ${STROKE_OUTLINE_CMAKE_DIR})
