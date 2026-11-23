# Builds the libraries the plugin needs, decoders only, as shared libraries (LGPL: users must be
# able to replace libheif and libde265):
# - libde265: HEVC, for HEIC/HEIF (LGPL-3.0);
# - dav1d: AV1, for AVIF (BSD-2-Clause);
# - libheif: the HEIF/AVIF container, using both (LGPL-3.0).
#
#   cmake -S deps -B build-deps -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=<prefix>
#   cmake --build build-deps
#
# macOS: one architecture per build (dav1d's meson build can't do universal binaries); pass
# -DCMAKE_OSX_ARCHITECTURES=arm64 or x86_64 and merge the prefixes with tools/merge-macos.sh, which
# also makes every library load its neighbours through @rpath.
# dav1d needs meson and nasm (x86) on the PATH.

cmake_minimum_required(VERSION 3.21)
project(libqheif-deps LANGUAGES C CXX)

include(ExternalProject)

set(LIBDE265_VERSION 1.1.3)
set(LIBDE265_SHA256 554228bd17788c99a7e63b37ab5634722190e6e2bf60c1dcb01cef328e133905)
set(DAV1D_VERSION 1.5.4)
set(DAV1D_SHA256 a1d5b63d2d38ec9bd03acf643caa51fa22edd1e89c5a109c4807717216bbec07)
set(LIBHEIF_VERSION 1.23.7)
set(LIBHEIF_SHA256 f7483e818503ea6224c8863e4a56ac02c7089021a98472c811c12aa19faf2475)

set(PREFIX "${CMAKE_INSTALL_PREFIX}")
set(DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/download" CACHE PATH "Where the source archives are downloaded")

if(APPLE)
    if(NOT CMAKE_OSX_ARCHITECTURES OR CMAKE_OSX_ARCHITECTURES MATCHES ";")
        message(FATAL_ERROR "Set CMAKE_OSX_ARCHITECTURES to one architecture (arm64 or x86_64)")
    endif()
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        # Qt 6.12's minimum
        set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0)
    endif()
endif()

# The CMake projects: same compiler, build type and target.
set(CMAKE_COMMON_ARGS
    -DCMAKE_INSTALL_PREFIX=${PREFIX}
    -DCMAKE_PREFIX_PATH=${PREFIX}
    -DCMAKE_INSTALL_LIBDIR=lib
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_SHARED_LIBS=ON
    -DBUILD_TESTING=OFF
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}
    -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
)
if(APPLE)
    list(APPEND CMAKE_COMMON_ARGS -DCMAKE_INSTALL_NAME_DIR=@rpath "-DCMAKE_INSTALL_RPATH=@loader_path")
elseif(UNIX)
    list(APPEND CMAKE_COMMON_ARGS "-DCMAKE_INSTALL_RPATH=\$ORIGIN")
endif()

ExternalProject_Add(libde265
    URL https://github.com/strukturag/libde265/releases/download/v${LIBDE265_VERSION}/libde265-${LIBDE265_VERSION}.tar.gz
    URL_HASH SHA256=${LIBDE265_SHA256}
    DOWNLOAD_DIR ${DOWNLOAD_DIR}
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    CMAKE_ARGS ${CMAKE_COMMON_ARGS}
        -DENABLE_DECODER=OFF # the dec265 tool, not the library
        -DENABLE_ENCODER=OFF
        -DENABLE_SDL=OFF
        -DENABLE_SHERLOCK265=OFF
)

# dav1d is built with meson. On macOS a cross file gives it the architecture.
find_program(MESON meson REQUIRED)
set(DAV1D_MESON_ARGS
    --prefix=${PREFIX} --libdir=lib --buildtype=release --default-library=shared
    -Denable_tools=false -Denable_tests=false -Denable_examples=false -Dlogging=false
)
if(APPLE)
    if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
        set(DAV1D_CPU_FAMILY x86_64)
    else()
        set(DAV1D_CPU_FAMILY aarch64)
    endif()
    set(DAV1D_CROSS_FILE "${CMAKE_BINARY_DIR}/dav1d-${CMAKE_OSX_ARCHITECTURES}.ini")
    file(WRITE ${DAV1D_CROSS_FILE}
"[binaries]
c = 'clang'
ar = 'ar'
strip = 'strip'
nasm = 'nasm'
[built-in options]
c_args = ['-arch', '${CMAKE_OSX_ARCHITECTURES}', '-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}']
c_link_args = ['-arch', '${CMAKE_OSX_ARCHITECTURES}', '-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}']
[host_machine]
system = 'darwin'
cpu_family = '${DAV1D_CPU_FAMILY}'
cpu = '${CMAKE_OSX_ARCHITECTURES}'
endian = 'little'
")
    list(APPEND DAV1D_MESON_ARGS --cross-file=${DAV1D_CROSS_FILE})
endif()

ExternalProject_Add(dav1d
    URL https://code.videolan.org/videolan/dav1d/-/archive/${DAV1D_VERSION}/dav1d-${DAV1D_VERSION}.tar.gz
    URL_HASH SHA256=${DAV1D_SHA256}
    DOWNLOAD_DIR ${DOWNLOAD_DIR}
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    CONFIGURE_COMMAND ${MESON} setup <BINARY_DIR> <SOURCE_DIR> ${DAV1D_MESON_ARGS}
    BUILD_COMMAND ${MESON} compile -C <BINARY_DIR>
    INSTALL_COMMAND ${MESON} install -C <BINARY_DIR>
)

# Only what reading HEIC/HEIF and AVIF needs: no encoders, no plugins loaded at run time, no tools.
ExternalProject_Add(libheif
    URL https://github.com/strukturag/libheif/releases/download/v${LIBHEIF_VERSION}/libheif-${LIBHEIF_VERSION}.tar.gz
    URL_HASH SHA256=${LIBHEIF_SHA256}
    DOWNLOAD_DIR ${DOWNLOAD_DIR}
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    DEPENDS libde265 dav1d
    CMAKE_ARGS ${CMAKE_COMMON_ARGS}
        -DENABLE_PLUGIN_LOADING=OFF
        -DWITH_LIBDE265=ON -DWITH_LIBDE265_PLUGIN=OFF
        -DWITH_DAV1D=ON -DWITH_DAV1D_PLUGIN=OFF
        -DWITH_X265=OFF -DWITH_KVAZAAR=OFF -DWITH_UVG266=OFF -DWITH_VVDEC=OFF -DWITH_VVENC=OFF
        -DWITH_X264=OFF -DWITH_OpenH264_DECODER=OFF
        -DWITH_AOM_DECODER=OFF -DWITH_AOM_ENCODER=OFF -DWITH_SvtEnc=OFF -DWITH_RAV1E=OFF
        -DWITH_JPEG_DECODER=OFF -DWITH_JPEG_ENCODER=OFF
        -DWITH_OpenJPEG_DECODER=OFF -DWITH_OpenJPEG_ENCODER=OFF -DWITH_OPENJPH_ENCODER=OFF
        -DWITH_FFMPEG_DECODER=OFF -DWITH_UNCOMPRESSED_CODEC=OFF
        -DWITH_LIBSHARPYUV=OFF -DWITH_GDK_PIXBUF=OFF
        -DWITH_EXAMPLES=OFF -DWITH_EXAMPLE_HEIF_VIEW=OFF -DBUILD_DOCUMENTATION=OFF
        -DWITH_FUZZERS=OFF
        -DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=ON -DCMAKE_DISABLE_FIND_PACKAGE_Brotli=ON
)

# The licences that go with the libraries.
ExternalProject_Get_Property(libde265 SOURCE_DIR)
set(LIBDE265_SOURCE_DIR ${SOURCE_DIR})
ExternalProject_Get_Property(dav1d SOURCE_DIR)
set(DAV1D_SOURCE_DIR ${SOURCE_DIR})
ExternalProject_Get_Property(libheif SOURCE_DIR)
set(LIBHEIF_SOURCE_DIR ${SOURCE_DIR})
add_custom_target(licenses ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PREFIX}/licenses
    COMMAND ${CMAKE_COMMAND} -E copy ${LIBDE265_SOURCE_DIR}/COPYING ${PREFIX}/licenses/libde265-${LIBDE265_VERSION}.COPYING.txt
    COMMAND ${CMAKE_COMMAND} -E copy ${DAV1D_SOURCE_DIR}/COPYING ${PREFIX}/licenses/dav1d-${DAV1D_VERSION}.COPYING.txt
    COMMAND ${CMAKE_COMMAND} -E copy ${LIBHEIF_SOURCE_DIR}/COPYING ${PREFIX}/licenses/libheif-${LIBHEIF_VERSION}.COPYING.txt
    DEPENDS libheif
)
