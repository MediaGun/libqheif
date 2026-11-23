# libqheif

A Qt 6 image plugin that reads **HEIC/HEIF** photos (iPhones and other phones) and **AVIF** images,
through [libheif](https://github.com/strukturag/libheif). Made for MediaGun, the same way as
[libqpsd](https://github.com/MediaGun/libqpsd): Qt has no plugin for these formats on Windows or
Linux, and its macOS one depends on the system.

- Reads only: the primary image of a file (not its thumbnail, depth map or other burst frames),
  rotated and mirrored as the file says, with its colour space (ICC profile or CICP).
- 8-bit images become `RGB888`/`RGBA8888`, 10 to 16-bit ones `RGBX64`/`RGBA64`.
- Format keys `heic`, `heif`, `hif`, `avif`; files are also recognised by their content.
- Qt 6.12, CMake only.

The code is based on the HEIF plugin of KDE's KImageFormats, as published standalone in
[qt-heic-image-plugin](https://github.com/novomesk/qt-heic-image-plugin) by Daniel Novomeský.
Writing, AVCI and HEJ2 were removed, AVIF added.

## Libraries

Built by `deps/` from their release archives (checked by SHA-256), as shared libraries, decoders only:

| Library | Version | For | Licence |
|---|---|---|---|
| libheif | 1.23.7 | the HEIF/AVIF container | LGPL-3.0 |
| libde265 | 1.1.3 | HEVC (HEIC) | LGPL-3.0 |
| dav1d | 1.5.4 | AV1 (AVIF) | BSD-2-Clause |

To update one, change its version and SHA-256 in `deps/CMakeLists.txt`.

## Building

Needs Qt 6.12, CMake, Ninja, meson and, for x86, nasm.

```sh
cmake -S deps -B build-deps -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$PWD/deps-prefix
cmake --build build-deps

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<Qt>/gcc_64 \
      -DQHEIF_DEPS_PREFIX=$PWD/deps-prefix -DCMAKE_INSTALL_PREFIX=$PWD/install
cmake --build build
ctest --test-dir build
cmake --install build
```

macOS: build `deps/` once with `-DCMAKE_OSX_ARCHITECTURES=arm64` and once with `x86_64`, then
`tools/merge-macos.sh <arm64 prefix> <x86_64 prefix> <universal prefix>`; the plugin is universal.

`install/` is laid out like Qt's folder, and is meant to be extracted into it:

| Path | What |
|---|---|
| `plugins/imageformats/` | the plugin (`libqheif.so`, `qheif.dll`, `libqheif.dylib`) |
| `lib/` (Linux, macOS), `bin/` (Windows) | libheif, libde265, dav1d |
| `licenses/qheif/` | the licences of the plugin and the libraries |

On Linux and macOS the plugin finds the libraries in `../../lib`. On Windows they must be next to
the application's executable (or on the `PATH`).

## Releases

CI (`.github/workflows/build.yaml`) builds and tests every push and pull request on Linux (Ubuntu
22.04), Windows and macOS (universal). A tag `qt-<Qt version>-<n>`, e.g. `qt-6.12.0-1`, publishes
`libqheif-linux.zip`, `libqheif-windows.zip` and `libqheif-macos.zip` as a GitHub release.

## Licence

The plugin is under the LGPL-2.0-or-later (`LICENSE`: LGPL-2.1). libheif and libde265 are under the
LGPL-3.0 and are shipped as separate shared libraries, so they can be replaced. The test images in
`tests/data` come from libheif.

HEIC images are compressed with HEVC, which is covered by patents.
