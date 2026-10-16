#!/usr/bin/env bash
# Original targets remain available; CI builds them on the matching host runners.
set -euo pipefail
cd "$(dirname "$0")"
target="${1:-android-arm64}"
args=(-DCMAKE_BUILD_TYPE=Release -DKERNELLOADER_BUILD_GUI_DEMO=OFF)
case "$target" in
  android-arm64|android-arm)
    : "${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME}"
    abi=arm64-v8a; [ "$target" = android-arm ] && abi=armeabi-v7a
    args+=(-DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" -DANDROID_ABI="$abi" -DANDROID_PLATFORM=android-23 -DANDROID_STL=c++_static) ;;
  linux-x64) ;;
  linux-x86) args+=(-DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=i686 -DCMAKE_C_FLAGS=-m32 -DCMAKE_CXX_FLAGS=-m32) ;;
  windows-x64) args+=(-DCMAKE_SYSTEM_NAME=Windows -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc-posix -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++-posix) ;;
  windows-x86) args+=(-DCMAKE_SYSTEM_NAME=Windows -DCMAKE_C_COMPILER=i686-w64-mingw32-gcc-posix -DCMAKE_CXX_COMPILER=i686-w64-mingw32-g++-posix) ;;
  macos-x64) args+=(-DCMAKE_SYSTEM_NAME=Darwin -DCMAKE_SYSTEM_PROCESSOR=x86_64 -DCMAKE_OSX_ARCHITECTURES=x86_64) ;;
  macos-arm64) args+=(-DCMAKE_SYSTEM_NAME=Darwin -DCMAKE_SYSTEM_PROCESSOR=arm64 -DCMAKE_OSX_ARCHITECTURES=arm64) ;;
  *) echo "Unknown target: $target" >&2; exit 2 ;;
esac
cmake -S . -B "build/$target" "${args[@]}"
cmake --build "build/$target" --target KernelLoader -j 2
mkdir -p "dist/$target"
python3 - "$target" <<'PY'
from pathlib import Path
import shutil,sys
folder=Path('build')/sys.argv[1];destination=Path('dist')/sys.argv[1]
files=[p for p in folder.glob('libloader.*') if p.suffix in {'.so','.dll','.dylib'}]
if not files:raise SystemExit('Missing loader binary')
for p in files:shutil.copy2(p,destination/p.name)
PY
