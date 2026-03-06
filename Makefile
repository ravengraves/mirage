CC ?= cc
AR ?= ar
PREFIX ?= /usr/local
DESTDIR ?=
PACKAGE_VERSION ?= 1.0.0
WASM_CC ?= clang
NODE ?= node
SCDOC ?= scdoc

BUILD_DIR := build
DOC_DIR := doc
DOC_BUILD_DIR := $(BUILD_DIR)/doc
DOC_FILES := $(wildcard $(DOC_DIR)/*.md)
DOC_BASENAMES := $(notdir $(DOC_FILES))
MAN_SOURCE := $(DOC_DIR)/mirage.3.scd
MAN_PAGE := $(DOC_BUILD_DIR)/mirage.3

CPPFLAGS ?= -Iinclude
CFLAGS ?= -O2 -g -Wall -Wextra -std=c11 -fPIC
LDFLAGS ?=
LDLIBS ?=

LIB_SHARED := $(BUILD_DIR)/libmirage.so
LIB_STATIC := $(BUILD_DIR)/libmirage.a
EMBEDDED_SHARED := $(BUILD_DIR)/libmirage_embedded.so
EMBEDDED_STATIC := $(BUILD_DIR)/libmirage_embedded.a

PC_FILE := $(BUILD_DIR)/mirage.pc
PC_EMBEDDED_FILE := $(BUILD_DIR)/mirage-embedded.pc

ALL_EXES := $(BUILD_DIR)/test $(BUILD_DIR)/test_all $(BUILD_DIR)/test_fuzz \
	$(BUILD_DIR)/test_embedded $(BUILD_DIR)/test_write_py $(BUILD_DIR)/test_read_py

.PHONY: all test check check-python check-js wasm clean docs install-lib install-docs install uninstall-lib uninstall-docs uninstall python

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

$(BUILD_DIR)/test_fuzz: test/test_fuzz.c $(LIB_STATIC) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB_STATIC) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_embedded: test/test_embedded.c $(EMBEDDED_STATIC) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(EMBEDDED_STATIC) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_write_py: test/test_write_py.c $(LIB_STATIC) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB_STATIC) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_read_py: test/test_read_py.c $(LIB_STATIC) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB_STATIC) $(LDFLAGS) $(LDLIBS) -o $@

test: $(ALL_EXES)

check: $(BUILD_DIR)/test $(BUILD_DIR)/test_all $(BUILD_DIR)/test_fuzz $(BUILD_DIR)/test_embedded
	./$(BUILD_DIR)/test
	./$(BUILD_DIR)/test_all
	./$(BUILD_DIR)/test_fuzz
	./$(BUILD_DIR)/test_embedded

check-python: $(BUILD_DIR)/test_write_py $(BUILD_DIR)/test_read_py
	python3 test/test_write.py $(BUILD_DIR)/test_py2c.wxf
	./$(BUILD_DIR)/test_write_py $(BUILD_DIR)/test_py2c.wxf
	./$(BUILD_DIR)/test_read_py $(BUILD_DIR)/test_c2py.wxf
	python3 test/test_read.py $(BUILD_DIR)/test_c2py.wxf

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
	cd lang/python && pip install .

clean:
	rm -rf $(BUILD_DIR)
