#!/usr/bin/env bash
# Merges the arm64 and x86_64 builds of deps/ into one universal prefix:
#   tools/merge-macos.sh <arm64 prefix> <x86_64 prefix> <universal prefix>
# Each library gets the install name @rpath/<file> and loads its neighbours through @rpath, with
# @loader_path as rpath: the plugin and macdeployqt find them wherever they're copied together.
set -euo pipefail

rm -rf "$3"
mkdir -p "$3"
arm=$(cd "$1" && pwd) x86=$(cd "$2" && pwd) out=$(cd "$3" && pwd)
# Headers, CMake and pkg-config files, licences: the same for both architectures.
cp -R "$arm/" "$out/"

cd "$out/lib"
for lib in *.dylib; do
    [ -L "$lib" ] && continue
    lipo -create "$arm/lib/$lib" "$x86/lib/$lib" -output "$lib"
done

for lib in *.dylib; do
    [ -L "$lib" ] && continue
    install_name_tool -id "@rpath/$(otool -D "$lib" | tail -n 1 | xargs basename)" "$lib"
    # The libraries it loads from this prefix, by their file name.
    otool -L "$lib" | tail -n +2 | awk '{print $1}' | while read -r dep; do
        name=$(basename "$dep")
        if [ -e "$name" ] && [ "$dep" != "@rpath/$name" ]; then
            install_name_tool -change "$dep" "@rpath/$name" "$lib"
        fi
    done
    otool -l "$lib" | grep -A2 LC_RPATH | grep -q '@loader_path' || install_name_tool -add_rpath @loader_path "$lib"
    # install_name_tool invalidates the signature: sign again, ad hoc (the app signs it all later).
    codesign --force --sign - "$lib"
done

echo "Universal libraries in $out/lib:"
for lib in *.dylib; do
    [ -L "$lib" ] && continue
    lipo -info "$lib"
    otool -L "$lib" | tail -n +2
done
