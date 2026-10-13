#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLCHAIN_PREFIX="${TOOLCHAIN_PREFIX:-x86_64-w64-mingw32}"
CC="${CC:-${TOOLCHAIN_PREFIX}-gcc}"
AR="${AR:-${TOOLCHAIN_PREFIX}-ar}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build/windows-mingw}"

for tool in "$CC" "$AR"; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		echo "missing tool: $tool" >&2
		exit 1
	fi
done

COMMON_FLAGS=( -O2 -g -Wall -Wextra -std=c11 -I"$ROOT/include" )

mkdir -p "$BUILD_DIR"

"$CC" "${COMMON_FLAGS[@]}" -c "$ROOT/src/mirage.c" -o "$BUILD_DIR/mirage.o"
"$CC" "${COMMON_FLAGS[@]}" -c "$ROOT/src/mirage_embedded.c" -o "$BUILD_DIR/mirage_embedded.o"

"$AR" rcs "$BUILD_DIR/libmirage.a" "$BUILD_DIR/mirage.o"
"$AR" rcs "$BUILD_DIR/libmirage_embedded.a" "$BUILD_DIR/mirage_embedded.o"

"$CC" -shared -o "$BUILD_DIR/libmirage.dll" "$BUILD_DIR/mirage.o" \
	-Wl,--out-implib,"$BUILD_DIR/libmirage.dll.a"
"$CC" -shared -o "$BUILD_DIR/libmirage_embedded.dll" "$BUILD_DIR/mirage_embedded.o" \
	-Wl,--out-implib,"$BUILD_DIR/libmirage_embedded.dll.a"

"$CC" "${COMMON_FLAGS[@]}" "$ROOT/test/test.c" "$BUILD_DIR/libmirage.a" -o "$BUILD_DIR/test.exe"
"$CC" "${COMMON_FLAGS[@]}" "$ROOT/test/test_all.c" "$BUILD_DIR/libmirage.a" -o "$BUILD_DIR/test_all.exe"
"$CC" "${COMMON_FLAGS[@]}" "$ROOT/test/test_fuzz.c" "$BUILD_DIR/libmirage.a" -o "$BUILD_DIR/test_fuzz.exe"
"$CC" "${COMMON_FLAGS[@]}" "$ROOT/test/test_fuzz_embedded.c" "$BUILD_DIR/libmirage_embedded.a" -o "$BUILD_DIR/test_fuzz_embedded.exe"
"$CC" "${COMMON_FLAGS[@]}" "$ROOT/test/test_embedded.c" "$BUILD_DIR/libmirage_embedded.a" -o "$BUILD_DIR/test_embedded.exe"
"$CC" "${COMMON_FLAGS[@]}" "$ROOT/test/test_write_py.c" "$BUILD_DIR/libmirage.a" -o "$BUILD_DIR/test_write_py.exe"
"$CC" "${COMMON_FLAGS[@]}" "$ROOT/test/test_read_py.c" "$BUILD_DIR/libmirage.a" -o "$BUILD_DIR/test_read_py.exe"

echo "Windows MinGW build succeeded: $BUILD_DIR"
ls -1 "$BUILD_DIR"
