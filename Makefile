CC := clang
AR := llvm-ar
PREFIX ?= /usr/local

# Base flags without LTO
CFLAGS_BASE := -Wall -Wextra -Wpedantic -Werror -std=c11 -fPIC \
	-ffunction-sections -fdata-sections \
	-fno-stack-protector -D_FORTIFY_SOURCE=0 \
	-I include -O3 -mtune=native

# LTO flags for static library only (executable linking)
CFLAGS_LTO := $(CFLAGS_BASE) -flto

# Non-LTO for shared library (Python/dlopen compatibility)
CFLAGS_SO := $(CFLAGS_BASE)

# Legacy CFLAGS for tests (with LTO)
CFLAGS := $(CFLAGS_LTO)

CEXE := -fuse-ld=lld -Wl,-gc-sections,-icf=safe,-s
LDFLAGS := -fuse-ld=lld

ifeq ($(ARCH),x86_64)
	CFLAGS_BASE += -march=native
	CFLAGS_LTO += -march=native
	CFLAGS_SO += -march=native
	CFLAGS += -march=native
endif

ifeq ($(ARCH),aarch64)
	CFLAGS_BASE += -march=armv8-a
	CFLAGS_LTO += -march=armv8-a
	CFLAGS_SO += -march=armv8-a
	CFLAGS += -march=armv8-a
endif

# =============================================================================
# Standard library (64-bit lengths)
# =============================================================================

all: build/libmirage.so build/libmirage.a

# Shared library - no LTO for Python/dlopen compatibility
build/libmirage.so: build/mirage_so.o
	$(CC) -shared -o $@ $< $(LDFLAGS)

# Static library - with LTO for best performance in C programs
build/libmirage.a: build/mirage_lto.o
	$(AR) rcs $@ $<

build/mirage_so.o: src/mirage.c include/mirage.h
	mkdir -p build
	$(CC) -c $(CFLAGS_SO) src/mirage.c -o $@

build/mirage_lto.o: src/mirage.c include/mirage.h
	mkdir -p build
	$(CC) -c $(CFLAGS_LTO) src/mirage.c -o $@

# =============================================================================
# Embedded library (16-bit lengths, no malloc)
# =============================================================================

embedded: build/libmirage_embedded.so build/libmirage_embedded.a

# Shared library - no LTO for Python/dlopen compatibility
build/libmirage_embedded.so: build/mirage_embedded_so.o
	$(CC) -shared -o $@ $< $(LDFLAGS)

# Static library - with LTO for best performance in C programs
build/libmirage_embedded.a: build/mirage_embedded_lto.o
	$(AR) rcs $@ $<

build/mirage_embedded_so.o: src/mirage_embedded.c include/mirage_embedded.h
	mkdir -p build
	$(CC) -c $(CFLAGS_SO) src/mirage_embedded.c -o $@

build/mirage_embedded_lto.o: src/mirage_embedded.c include/mirage_embedded.h
	mkdir -p build
	$(CC) -c $(CFLAGS_LTO) src/mirage_embedded.c -o $@

# =============================================================================
# Tests - Standard
# =============================================================================

build/test: test/test.c build/libmirage.a
	$(CC) $(CFLAGS) $(CEXE) -o $@ test/test.c build/libmirage.a

build/test_all: test/test_all.c build/libmirage.a
	$(CC) $(CFLAGS) $(CEXE) -o $@ test/test_all.c build/libmirage.a

build/test_fuzz: test/test_fuzz.c build/libmirage.a
	$(CC) $(CFLAGS) $(CEXE) -o $@ test/test_fuzz.c build/libmirage.a

check: build/test build/test_all build/test_fuzz
	./build/test
	./build/test_all
	./build/test_fuzz

# =============================================================================
# Tests - Embedded
# =============================================================================

build/test_embedded: test/test_embedded.c build/libmirage_embedded.a
	$(CC) $(CFLAGS) $(CEXE) -o $@ test/test_embedded.c build/libmirage_embedded.a

check-embedded: build/test_embedded
	./build/test_embedded

# =============================================================================
# Tests - Python cross-language
# =============================================================================

build/test_write_py: test/test_write_py.c build/libmirage.a
	$(CC) $(CFLAGS) $(CEXE) -o $@ test/test_write_py.c build/libmirage.a

build/test_read_py: test/test_read_py.c build/libmirage.a
	$(CC) $(CFLAGS) $(CEXE) -o $@ test/test_read_py.c build/libmirage.a

check-python: build/test_write_py build/test_read_py
	@echo "=== Python writes, C reads ==="
	python3 test/test_write.py build/test_py2c.wxf
	./build/test_write_py build/test_py2c.wxf
	@echo ""
	@echo "=== C writes, Python reads ==="
	./build/test_read_py build/test_c2py.wxf
	python3 test/test_read.py build/test_c2py.wxf
	@echo ""
	@echo "All cross-language tests passed!"

# =============================================================================
# Tests - Julia
# =============================================================================

check-julia: build/libmirage.so
	julia --project=lang/julia -e 'include("lang/julia/test/runtests.jl")'

# =============================================================================
# WebAssembly build
# =============================================================================

build/mirage.wasm: src/mirage_wasm.c src/mirage_embedded.c include/mirage_embedded.h
	clang --target=wasm32 -O2 \
		-nostdlib -Wl,--no-entry -Wl,--export-dynamic \
		-Wl,--initial-memory=2097152 -Wl,--max-memory=16777216 \
		-I include -o $@ src/mirage_wasm.c

wasm: build/mirage.wasm

NODE ?= $(shell which node 2>/dev/null || echo node)

check-js: build/mirage.wasm
	$(NODE) lang/js/test.mjs

# =============================================================================
# Install / Uninstall
# =============================================================================

install: build/libmirage.so build/libmirage.a
	mkdir -p $(PREFIX)/lib
	mkdir -p $(PREFIX)/include
	install -m 0644 build/libmirage.so $(PREFIX)/lib/
	install -m 0644 build/libmirage.a $(PREFIX)/lib/
	install -m 0644 include/mirage.h $(PREFIX)/include/
	install -m 0644 include/mirage_style.h $(PREFIX)/include/
	sudo ldconfig

install-embedded: build/libmirage_embedded.so build/libmirage_embedded.a
	mkdir -p $(PREFIX)/lib
	mkdir -p $(PREFIX)/include
	install -m 0644 build/libmirage_embedded.so $(PREFIX)/lib/
	install -m 0644 build/libmirage_embedded.a $(PREFIX)/lib/
	install -m 0644 include/mirage_embedded.h $(PREFIX)/include/
	sudo ldconfig

uninstall:
	rm -f $(PREFIX)/lib/libmirage.so
	rm -f $(PREFIX)/lib/libmirage.a
	rm -f $(PREFIX)/lib/libmirage_embedded.so
	rm -f $(PREFIX)/lib/libmirage_embedded.a
	rm -f $(PREFIX)/include/mirage.h
	rm -f $(PREFIX)/include/mirage_style.h
	rm -f $(PREFIX)/include/mirage_embedded.h
	sudo ldconfig

# =============================================================================
# Utilities
# =============================================================================

compile_commands.json: Makefile
	@echo '[\n' \
	'{"directory":"$(PWD)","command":"$(CC) -c $(CFLAGS) src/mirage.c -o build/mirage.o","file":"src/mirage.c"},\n' \
	'{"directory":"$(PWD)","command":"$(CC) -c $(CFLAGS) src/mirage_embedded.c -o build/mirage_embedded.o","file":"src/mirage_embedded.c"},\n' \
	'{"directory":"$(PWD)","command":"$(CC) $(CFLAGS) $(CEXE) -o build/test test/test.c build/libmirage.a","file":"test/test.c"},\n' \
	'{"directory":"$(PWD)","command":"$(CC) $(CFLAGS) $(CEXE) -o build/test_fuzz test/test_fuzz.c build/libmirage.a","file":"test/test_fuzz.c"},\n' \
	'{"directory":"$(PWD)","command":"$(CC) $(CFLAGS) $(CEXE) -o build/test_embedded test/test_embedded.c build/libmirage_embedded.a","file":"test/test_embedded.c"}\n]' \
	> $@

format:
	clang-format -style=file -i $$(find src include test -name '*.c' -o -name '*.h')

clean:
	rm -rf build

python: build/libmirage.so
	cd lang/python && pip install .

.PHONY: format clean all embedded wasm check check-embedded check-python check-julia check-js install install-embedded uninstall python
