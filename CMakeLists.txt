cmake_minimum_required(VERSION 3.21)

# macOS (before project(), which reads them; other systems ignore them): universal, like MediaGun.
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0 CACHE STRING "") # Qt 6.12's minimum

project(qheif VERSION 1.0.0 LANGUAGES CXX
    DESCRIPTION "Qt image plugin reading HEIC/HEIF and AVIF through libheif")

# libheif, libde265 and dav1d: built by deps/ into this prefix.
set(QHEIF_DEPS_PREFIX "" CACHE PATH "Where deps/ installed libheif, libde265 and dav1d")
option(QHEIF_INSTALL_DEPS "Install the libheif, libde265 and dav1d libraries and licences with the plugin" ON)
option(QHEIF_BUILD_TESTS "Build the tests" ON)

if(QHEIF_DEPS_PREFIX)
    list(PREPEND CMAKE_PREFIX_PATH "${QHEIF_DEPS_PREFIX}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui)
# deps/ pins the version; libheif's package accepts only an exact version.
find_package(libheif REQUIRED CONFIG)
qt_standard_project_setup()

qt_add_plugin(qheif PLUGIN_TYPE imageformats CLASS_NAME QHeifPlugin)
target_sources(qheif PRIVATE src/qheifhandler.cpp src/qheifhandler.h)
target_link_libraries(qheif PRIVATE Qt6::Core Qt6::Gui heif)
target_compile_definitions(qheif PRIVATE QT_NO_CAST_FROM_ASCII QT_NO_FOREACH)

# In the build tree as in Qt: plugins/imageformats (the tests load it from there).
set_target_properties(qheif PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/imageformats"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/imageformats"
)
# Installed into Qt's folder: the libraries go to lib/ (Linux, macOS), next to Qt's.
if(APPLE)
    set_target_properties(qheif PROPERTIES SUFFIX ".dylib" INSTALL_RPATH "@loader_path/../../lib")
elseif(UNIX)
    set_target_properties(qheif PROPERTIES INSTALL_RPATH "\$ORIGIN/../../lib")
endif()

include(GNUInstallDirs)
install(TARGETS qheif
    LIBRARY DESTINATION plugins/imageformats
    RUNTIME DESTINATION plugins/imageformats
)

if(QHEIF_INSTALL_DEPS)
    if(NOT QHEIF_DEPS_PREFIX)
        message(FATAL_ERROR "QHEIF_INSTALL_DEPS needs QHEIF_DEPS_PREFIX")
    endif()
    if(WIN32)
        # Next to the application's executable, like Qt's own DLLs.
        install(DIRECTORY "${QHEIF_DEPS_PREFIX}/bin/" DESTINATION bin
            FILES_MATCHING PATTERN "heif.dll" PATTERN "libde265.dll" PATTERN "dav1d.dll")
    elseif(APPLE)
        install(DIRECTORY "${QHEIF_DEPS_PREFIX}/lib/" DESTINATION lib
            FILES_MATCHING PATTERN "libheif*.dylib" PATTERN "libde265*.dylib" PATTERN "libdav1d*.dylib"
            PATTERN "cmake" EXCLUDE PATTERN "pkgconfig" EXCLUDE)
    else()
        install(DIRECTORY "${QHEIF_DEPS_PREFIX}/lib/" DESTINATION lib
            FILES_MATCHING PATTERN "libheif.so*" PATTERN "libde265.so*" PATTERN "libdav1d.so*"
            PATTERN "cmake" EXCLUDE PATTERN "pkgconfig" EXCLUDE)
    endif()
    install(DIRECTORY "${QHEIF_DEPS_PREFIX}/licenses/" DESTINATION licenses/qheif)
endif()
install(FILES LICENSE DESTINATION licenses/qheif RENAME qheif.LICENSE.txt)

if(QHEIF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
