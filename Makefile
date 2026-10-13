CC ?= cc
AR ?= ar
NINJA ?= ninja
CLANG ?= clang
CLANG_TIDY ?= clang-tidy
GCC_ANALYZER ?= gcc
CPPCHECK ?= cppcheck
CPPCHECK_FLAGS ?= --enable=warning,performance,portability,missingInclude \
	--inconclusive --check-level=exhaustive --force
CPPCHECK_EXTRA_FLAGS ?=
PREFIX ?= /usr/local
DESTDIR ?=
PACKAGE_VERSION ?= 1.0.0
WASM_CC ?= clang
NODE ?= node
SCDOC ?= scdoc
SANITIZERS ?= address,undefined
SANITIZER_CFLAGS ?= -fsanitize=$(SANITIZERS) -fno-omit-frame-pointer
SANITIZER_LDFLAGS ?= -fsanitize=$(SANITIZERS)
SANITIZE_BUILD_DIR ?= build-sanitize
FUZZ_BUILD_DIR ?= build-fuzz
FUZZ_SECONDS ?= 10
FUZZ_SEED ?= 20261013
FUZZ_FLAGS := -O3 -g -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all
FUZZ_EXES := $(addprefix $(FUZZ_BUILD_DIR)/,read_standard read_embedded write_standard write_embedded)

BUILD_DIR := build
DOC_DIR := docs
DOC_BUILD_DIR := $(BUILD_DIR)/docs
DOC_FILES := $(wildcard $(DOC_DIR)/*.md)
DOC_BASENAMES := $(notdir $(DOC_FILES))
MAN_SOURCE := $(DOC_DIR)/mirage.3.scd
MAN_PAGE := $(DOC_BUILD_DIR)/mirage.3

CPPFLAGS ?= -Iinclude
CFLAGS ?= -O3 -g -Wall -Wextra -std=c11 -fPIC
LDFLAGS ?=
LDLIBS ?=

ARCH ?= $(shell uname -m)

ifeq ($(ARCH),x86_64)
	CFLAGS += -march=native -mtune=native
endif

ifeq ($(ARCH),aarch64)
	CFLAGS += -mcpu=native
endif

LIB_SHARED := $(BUILD_DIR)/libmirage.so
LIB_STATIC := $(BUILD_DIR)/libmirage.a
EMBEDDED_SHARED := $(BUILD_DIR)/libmirage_embedded.so
EMBEDDED_STATIC := $(BUILD_DIR)/libmirage_embedded.a

PC_FILE := $(BUILD_DIR)/mirage.pc
PC_EMBEDDED_FILE := $(BUILD_DIR)/mirage-embedded.pc

ALL_EXES := $(BUILD_DIR)/test $(BUILD_DIR)/test_all $(BUILD_DIR)/test_fuzz \
	$(BUILD_DIR)/test_fuzz_embedded $(BUILD_DIR)/test_embedded $(BUILD_DIR)/test_write_py $(BUILD_DIR)/test_read_py
TEST_SOURCES := test/test.c test/test_all.c test/test_fuzz.c test/test_embedded.c \
	test/test_fuzz_embedded.c test/test_write_py.c test/test_read_py.c
ANALYZE_SOURCES := src/mirage.c src/mirage_embedded.c $(TEST_SOURCES)
RUNTIME_TESTS := test test_all test_embedded test_fuzz test_fuzz_embedded

.PHONY: all test check check-slow check-runtime check-analysis check-python check-js wasm clean docs install-lib install-docs install uninstall-lib uninstall-docs uninstall python tidy clang-analyze fanalyzer cppcheck sanitize-check fuzz-check analysis

all: $(LIB_SHARED) $(LIB_STATIC) $(EMBEDDED_SHARED) $(EMBEDDED_STATIC) $(ALL_EXES)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(DOC_BUILD_DIR):
	mkdir -p $(DOC_BUILD_DIR)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIB_SHARED): $(BUILD_DIR)/mirage.o
	$(CC) -shared -o $@ $< $(LDFLAGS) $(LDLIBS)

$(LIB_STATIC): $(BUILD_DIR)/mirage.o
	$(AR) rcs $@ $<

$(EMBEDDED_SHARED): $(BUILD_DIR)/mirage_embedded.o
	$(CC) -shared -o $@ $< $(LDFLAGS) $(LDLIBS)

$(EMBEDDED_STATIC): $(BUILD_DIR)/mirage_embedded.o
	$(AR) rcs $@ $<

$(BUILD_DIR)/test: test/test.c $(LIB_STATIC) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB_STATIC) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_all: test/test_all.c $(LIB_STATIC) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB_STATIC) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_fuzz: test/test_fuzz.c test/test_fuzz_message.h test/test_numericarray.h test/test_decode_contracts.h $(LIB_STATIC) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB_STATIC) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_fuzz_embedded: test/test_fuzz_embedded.c test/test_fuzz_message.h test/test_numericarray.h test/test_decode_contracts.h $(EMBEDDED_STATIC) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(EMBEDDED_STATIC) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_embedded: test/test_embedded.c $(EMBEDDED_STATIC) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(EMBEDDED_STATIC) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_write_py: test/test_write_py.c $(LIB_STATIC) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB_STATIC) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_read_py: test/test_read_py.c $(LIB_STATIC) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB_STATIC) $(LDFLAGS) $(LDLIBS) -o $@

test: $(ALL_EXES)

check-runtime: $(addprefix $(BUILD_DIR)/,$(RUNTIME_TESTS))
	@set -e; for test in $(RUNTIME_TESTS); do ./$(BUILD_DIR)/$$test; done

check-analysis: compile_commands.json
	@set +e; \
	for target in tidy clang-analyze fanalyzer cppcheck; do \
		echo "== $$target (warnings only) =="; \
		$(MAKE) --no-print-directory $$target || \
			echo "$$target reported findings; continuing to sanitizer tests"; \
	done

# C runtime regressions, advisory static analysis, and ASan/UBSan.
# Python bindings remain separate; timed libFuzzer runs are opt-in.
check: check-runtime check-analysis sanitize-check

check-slow: check fuzz-check

check-python: $(BUILD_DIR)/test_write_py $(BUILD_DIR)/test_read_py
	python3 test/test_python_bounds.py
	python3 test/test_write.py $(BUILD_DIR)/test_py2c.wxf
	./$(BUILD_DIR)/test_write_py $(BUILD_DIR)/test_py2c.wxf
	./$(BUILD_DIR)/test_read_py $(BUILD_DIR)/test_c2py.wxf
	python3 test/test_read.py $(BUILD_DIR)/test_c2py.wxf

compile_commands.json: build.ninja
	$(NINJA) -f $< -t compdb cc link_exe > $@

tidy: compile_commands.json
	$(CLANG_TIDY) -quiet --extra-arg=-Wno-unused-command-line-argument -p . $(ANALYZE_SOURCES)

# Included project headers are checked with their C translation units.
# System headers are modeled by cppcheck's standard library configuration.
cppcheck: compile_commands.json
	$(CPPCHECK) --project=$< $(CPPCHECK_FLAGS) \
		--suppress=missingIncludeSystem --error-exitcode=1 $(CPPCHECK_EXTRA_FLAGS)

clang-analyze:
	set -e; \
	for src in $(ANALYZE_SOURCES); do \
		echo "CLANG-ANALYZE $$src"; \
		$(CLANG) $(CPPFLAGS) $(CFLAGS) --analyze -Xanalyzer -analyzer-output=text $$src; \
	done

fanalyzer:
	set -e; \
	for src in $(ANALYZE_SOURCES); do \
		echo "FANALYZER $$src"; \
		$(GCC_ANALYZER) $(CPPFLAGS) $(CFLAGS) -fanalyzer -c $$src -o /dev/null; \
	done

sanitize-check:
	rm -rf $(SANITIZE_BUILD_DIR)
	ASAN_OPTIONS=detect_leaks=1:strict_init_order=1:check_initialization_order=1 \
	UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
	$(MAKE) BUILD_DIR=$(SANITIZE_BUILD_DIR) CC=$(CLANG) \
		CFLAGS='$(CFLAGS) $(SANITIZER_CFLAGS)' \
		LDFLAGS='$(LDFLAGS) $(SANITIZER_LDFLAGS)' \
		check-runtime

analysis: check-analysis

$(FUZZ_BUILD_DIR):
	mkdir -p $@

$(FUZZ_BUILD_DIR)/read_standard: test/fuzz_numericarray.c src/mirage.c include/mirage.h | $(FUZZ_BUILD_DIR)
	$(CLANG) $(CPPFLAGS) $(FUZZ_FLAGS) test/fuzz_numericarray.c src/mirage.c -o $@

$(FUZZ_BUILD_DIR)/read_embedded: test/fuzz_numericarray.c src/mirage_embedded.c include/mirage_embedded.h | $(FUZZ_BUILD_DIR)
	$(CLANG) $(CPPFLAGS) $(FUZZ_FLAGS) -DEMBEDDED test/fuzz_numericarray.c src/mirage_embedded.c -o $@

$(FUZZ_BUILD_DIR)/write_standard: test/fuzz_numericarray.c src/mirage.c include/mirage.h | $(FUZZ_BUILD_DIR)
	$(CLANG) $(CPPFLAGS) $(FUZZ_FLAGS) -DFUZZ_WRITER test/fuzz_numericarray.c src/mirage.c -o $@

$(FUZZ_BUILD_DIR)/write_embedded: test/fuzz_numericarray.c src/mirage_embedded.c include/mirage_embedded.h | $(FUZZ_BUILD_DIR)
	$(CLANG) $(CPPFLAGS) $(FUZZ_FLAGS) -DEMBEDDED -DFUZZ_WRITER test/fuzz_numericarray.c src/mirage_embedded.c -o $@

fuzz-check: $(FUZZ_EXES)
	python3 test/seed_numericarray_fuzz.py $(FUZZ_BUILD_DIR)
	set -e; for exe in $(FUZZ_EXES); do \
		$$exe $${exe}_corpus -max_total_time=$(FUZZ_SECONDS) -max_len=1024 \
			-seed=$(FUZZ_SEED) -artifact_prefix=$(FUZZ_BUILD_DIR)/; \
	done

$(BUILD_DIR)/mirage.wasm: src/mirage_wasm.c src/mirage_embedded.c include/mirage_embedded.h | $(BUILD_DIR)
	$(WASM_CC) --target=wasm32 -O2 -nostdlib \
		-Wl,--no-entry -Wl,--export-dynamic \
		-Wl,--initial-memory=2097152 -Wl,--max-memory=16777216 \
		-Iinclude -o $@ src/mirage_wasm.c

wasm: $(BUILD_DIR)/mirage.wasm

check-js: $(BUILD_DIR)/mirage.wasm
	$(NODE) lang/js/test.mjs

$(DOC_BUILD_DIR)/.stamp: $(DOC_FILES) | $(DOC_BUILD_DIR)
	cp $(DOC_FILES) $(DOC_BUILD_DIR)/
	touch $@

$(MAN_PAGE): $(MAN_SOURCE) | $(DOC_BUILD_DIR)
	$(SCDOC) < $< > $@

docs: $(DOC_BUILD_DIR)/.stamp $(MAN_PAGE)

$(PC_FILE): $(DOC_DIR)/mirage.pc.in | $(BUILD_DIR)
	sed \
		-e 's|@PREFIX@|$(PREFIX)|g' \
		-e 's|@LIBDIR@|$(PREFIX)/lib|g' \
		-e 's|@INCLUDEDIR@|$(PREFIX)/include|g' \
		-e 's|@VERSION@|$(PACKAGE_VERSION)|g' \
		$< > $@

$(PC_EMBEDDED_FILE): $(DOC_DIR)/mirage-embedded.pc.in | $(BUILD_DIR)
	sed \
		-e 's|@PREFIX@|$(PREFIX)|g' \
		-e 's|@LIBDIR@|$(PREFIX)/lib|g' \
		-e 's|@INCLUDEDIR@|$(PREFIX)/include|g' \
		-e 's|@VERSION@|$(PACKAGE_VERSION)|g' \
		$< > $@

install-lib: $(LIB_SHARED) $(LIB_STATIC) $(EMBEDDED_SHARED) $(EMBEDDED_STATIC) $(PC_FILE) $(PC_EMBEDDED_FILE)
	install -d $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PREFIX)/include $(DESTDIR)$(PREFIX)/lib/pkgconfig
	install -m 0644 $(LIB_SHARED) $(DESTDIR)$(PREFIX)/lib/
	install -m 0644 $(LIB_STATIC) $(DESTDIR)$(PREFIX)/lib/
	install -m 0644 $(EMBEDDED_SHARED) $(DESTDIR)$(PREFIX)/lib/
	install -m 0644 $(EMBEDDED_STATIC) $(DESTDIR)$(PREFIX)/lib/
	install -m 0644 include/mirage.h $(DESTDIR)$(PREFIX)/include/
	install -m 0644 include/mirage_style.h $(DESTDIR)$(PREFIX)/include/
	install -m 0644 include/mirage_embedded.h $(DESTDIR)$(PREFIX)/include/
	install -m 0644 $(PC_FILE) $(DESTDIR)$(PREFIX)/lib/pkgconfig/
	install -m 0644 $(PC_EMBEDDED_FILE) $(DESTDIR)$(PREFIX)/lib/pkgconfig/

install-docs: docs
	install -d $(DESTDIR)$(PREFIX)/share/doc/mirage
	install -d $(DESTDIR)$(PREFIX)/share/man/man3
	install -m 0644 $(DOC_FILES) $(DESTDIR)$(PREFIX)/share/doc/mirage/
	install -m 0644 $(MAN_PAGE) $(DESTDIR)$(PREFIX)/share/man/man3/

install: install-lib install-docs

uninstall-lib:
	rm -f $(DESTDIR)$(PREFIX)/lib/libmirage.so
	rm -f $(DESTDIR)$(PREFIX)/lib/libmirage.a
	rm -f $(DESTDIR)$(PREFIX)/lib/libmirage_embedded.so
	rm -f $(DESTDIR)$(PREFIX)/lib/libmirage_embedded.a
	rm -f $(DESTDIR)$(PREFIX)/include/mirage.h
	rm -f $(DESTDIR)$(PREFIX)/include/mirage_style.h
	rm -f $(DESTDIR)$(PREFIX)/include/mirage_embedded.h
	rm -f $(DESTDIR)$(PREFIX)/lib/pkgconfig/mirage.pc
	rm -f $(DESTDIR)$(PREFIX)/lib/pkgconfig/mirage-embedded.pc

uninstall-docs:
	rm -f $(addprefix $(DESTDIR)$(PREFIX)/share/doc/mirage/,$(DOC_BASENAMES))
	rm -f $(DESTDIR)$(PREFIX)/share/man/man3/mirage.3
	rmdir $(DESTDIR)$(PREFIX)/share/doc/mirage 2>/dev/null || true

uninstall: uninstall-lib uninstall-docs

python: $(LIB_SHARED)
	cd lang/python && { pip install --break-system-packages -e .; } || { pip install -e .; }

clean:
	rm -rf $(BUILD_DIR) $(SANITIZE_BUILD_DIR) $(FUZZ_BUILD_DIR)
	rm -f compile_commands.json
