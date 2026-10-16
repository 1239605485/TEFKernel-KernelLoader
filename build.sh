#!/usr/bin/env bash
# Build six supported targets, test the host runtime, package KernelLoader.
set -euo pipefail
cd "$(dirname "$0")"
: "${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME to the Android NDK directory}"
: "${TEFKERNEL_LIB:?Set TEFKERNEL_LIB to the host Linux TEFKernel library used for packaging}"
mkdir -p build/package-stage dist/gui-test-runtime/lib
for target in linux-x64 linux-x86 windows-x64 windows-x86 android-arm64 android-arm; do
  args=(-DCMAKE_BUILD_TYPE=Release -DKERNELLOADER_BUILD_GUI_DEMO=ON)
  case "$target" in
    linux-x64) suffix=linux.x64.so; args+=(-DKERNELLOADER_BUILD_GUI_TESTS=ON) ;;
    linux-x86) suffix=linux.x86.so; args+=(-DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=i686 -DCMAKE_C_FLAGS=-m32 -DCMAKE_CXX_FLAGS=-m32) ;;
    windows-x64) suffix=windows.x64.dll; args+=(-DCMAKE_SYSTEM_NAME=Windows -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc-posix -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++-posix) ;;
    windows-x86) suffix=windows.x86.dll; args+=(-DCMAKE_SYSTEM_NAME=Windows -DCMAKE_C_COMPILER=i686-w64-mingw32-gcc-posix -DCMAKE_CXX_COMPILER=i686-w64-mingw32-g++-posix) ;;
    android-arm64) suffix=android.arm64.so; args+=(-DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-23) ;;
    android-arm) suffix=android.arm.so; args+=(-DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" -DANDROID_ABI=armeabi-v7a -DANDROID_PLATFORM=android-23) ;;
  esac
  cmake -S . -B "build/$target" "${args[@]}"
  cmake --build "build/$target" -j 2
  if [ "$target" = linux-x64 ]; then ctest --test-dir "build/$target" --output-on-failure; fi
  cp "build/$target/libloader.$suffix" build/package-stage/
  cp "build/$target/libgui_demo.$suffix" dist/gui-test-runtime/lib/
done
cp examples/gui_demo/mod.json examples/gui_demo/gui_test.png dist/gui-test-runtime/
python3 tools/tefpkg_tool.py build build/package-stage dist/kernelloader.tefpkg 0x114514 -c tools/compress_loader.json
python3 - <<'PY'
from pathlib import Path
from zipfile import ZipFile,ZIP_DEFLATED
import json
root=Path.cwd();version=json.loads((root/'Info.json').read_text())['version']
with ZipFile(root/'dist'/f'KernelLoader-v{version}.zip','w',ZIP_DEFLATED) as z:
    for file in [root/'Info.json',root/'Manifest.json',root/'dist/kernelloader.tefpkg']:z.write(file,file.name)
# Runtime directory follows KernelLoader's documented mod.json + private files layout.
# This is a runtime file set, not an invented TEFManager import manifest.
with ZipFile(root/'dist/GUI-test-runtime-files.zip','w',ZIP_DEFLATED) as z:
    for file in (root/'dist/gui-test-runtime').rglob('*'):
        if file.is_file():z.write(file,file.relative_to(root/'dist/gui-test-runtime'))
PY
