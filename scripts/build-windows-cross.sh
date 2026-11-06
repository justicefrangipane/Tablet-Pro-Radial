#!/usr/bin/env bash
# Optional Linux-to-Windows compilation. No SDK downloads or system installation.
# Required environment:
#   TABLETPRO_MINGW_ROOT=/path/to/MinGW/usr (contains bin/ and x86_64-w64-mingw32/)
#   QT_WINDOWS_ROOT=/path/to/Qt/6.8.3/mingw_64
#   QT_HOST_ROOT=/path/to/Qt/6.8.3/gcc_64
# Optional: CMAKE_BIN=cmake TABLETPRO_CROSS_BUILD=/path/to/build
# This compiles the EXE; Windows runtime testing still requires a Windows device.
set -euo pipefail

project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
: "${TABLETPRO_MINGW_ROOT:?Set TABLETPRO_MINGW_ROOT to the extracted cross-toolchain usr directory}"
: "${QT_WINDOWS_ROOT:?Set QT_WINDOWS_ROOT to the official Windows MinGW Qt kit}"
: "${QT_HOST_ROOT:?Set QT_HOST_ROOT to the matching native Linux Qt kit}"
build_dir="${TABLETPRO_CROSS_BUILD:-$project_root/build-cross-windows}"
cmake_bin="${CMAKE_BIN:-cmake}"
compiler="$TABLETPRO_MINGW_ROOT/bin/x86_64-w64-mingw32-g++-posix"
if [[ ! -x "$compiler" ]]; then
    compiler="$TABLETPRO_MINGW_ROOT/bin/x86_64-w64-mingw32-g++"
fi
[[ -x "$compiler" ]] || { printf '%s\n' 'The MinGW x64 C++ compiler was not found.' >&2; exit 1; }
[[ -x "$QT_HOST_ROOT/libexec/moc" ]] || { printf '%s\n' 'The native Qt moc tool was not found.' >&2; exit 1; }

"$cmake_bin" -S "$project_root" -B "$build_dir" \
    -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_SYSTEM_PROCESSOR=x86_64 \
    "-DCMAKE_CXX_COMPILER=$compiler" \
    "-DCMAKE_RC_COMPILER=$TABLETPRO_MINGW_ROOT/bin/x86_64-w64-mingw32-windres" \
    "-DCMAKE_FIND_ROOT_PATH=$TABLETPRO_MINGW_ROOT/x86_64-w64-mingw32;$QT_WINDOWS_ROOT" \
    -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
    "-DCMAKE_PREFIX_PATH=$QT_WINDOWS_ROOT" "-DQT_HOST_PATH=$QT_HOST_ROOT" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
"$cmake_bin" --build "$build_dir" --parallel "${TABLETPRO_BUILD_JOBS:-4}"
printf 'Windows executable compiled: %s/tabletpro_radial.exe\n' "$build_dir"
printf '%s\n' 'Deploy the Qt/QML/runtime DLL dependencies before running on Windows.'
