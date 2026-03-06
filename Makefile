CC ?= cc
AR ?= ar
PREFIX ?= /usr/local
WASM_CC ?= clang
NODE ?= node

BUILD_DIR := build

CPPFLAGS ?= -Iinclude
CFLAGS ?= -O2 -g -Wall -Wextra -std=c11 -fPIC
LDFLAGS ?=
LDLIBS ?=

LIB_SHARED := $(BUILD_DIR)/libmirage.so
LIB_STATIC := $(BUILD_DIR)/libmirage.a
EMBEDDED_SHARED := $(BUILD_DIR)/libmirage_embedded.so
EMBEDDED_STATIC := $(BUILD_DIR)/libmirage_embedded.a

TEST_STANDARD := $(BUILD_DIR)/test $(BUILD_DIR)/test_all $(BUILD_DIR)/test_fuzz
TEST_PY := $(BUILD_DIR)/test_write_py $(BUILD_DIR)/test_read_py

.PHONY: all embedded test check check-embedded check-python wasm check-js install install-embedded uninstall python clean

all: $(LIB_SHARED) $(LIB_STATIC)

embedded: $(EMBEDDED_SHARED) $(EMBEDDED_STATIC)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

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

test: $(TEST_STANDARD) $(BUILD_DIR)/test_embedded $(TEST_PY)

$(BUILD_DIR)/test: test/test.c $(LIB_STATIC) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB_STATIC) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_all: test/test_all.c $(LIB_STATIC) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB_STATIC) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_fuzz: test/test_fuzz.c $(LIB_STATIC) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB_STATIC) $(LDFLAGS) $(LDLIBS) -o $@

check: $(TEST_STANDARD)
	./$(BUILD_DIR)/test
	./$(BUILD_DIR)/test_all
	./$(BUILD_DIR)/test_fuzz

$(BUILD_DIR)/test_embedded: test/test_embedded.c $(EMBEDDED_STATIC) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(EMBEDDED_STATIC) $(LDFLAGS) $(LDLIBS) -o $@

check-embedded: $(BUILD_DIR)/test_embedded
	./$(BUILD_DIR)/test_embedded

$(BUILD_DIR)/test_write_py: test/test_write_py.c $(LIB_STATIC) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB_STATIC) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_read_py: test/test_read_py.c $(LIB_STATIC) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB_STATIC) $(LDFLAGS) $(LDLIBS) -o $@

check-python: $(TEST_PY)
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

install: all
	install -d $(PREFIX)/lib $(PREFIX)/include
	install -m 0644 $(LIB_SHARED) $(PREFIX)/lib/
	install -m 0644 $(LIB_STATIC) $(PREFIX)/lib/
	install -m 0644 include/mirage.h $(PREFIX)/include/
	install -m 0644 include/mirage_style.h $(PREFIX)/include/

install-embedded: embedded
	install -d $(PREFIX)/lib $(PREFIX)/include
	install -m 0644 $(EMBEDDED_SHARED) $(PREFIX)/lib/
	install -m 0644 $(EMBEDDED_STATIC) $(PREFIX)/lib/
	install -m 0644 include/mirage_embedded.h $(PREFIX)/include/

uninstall:
	rm -f $(PREFIX)/lib/libmirage.so
	rm -f $(PREFIX)/lib/libmirage.a
	rm -f $(PREFIX)/lib/libmirage_embedded.so
	rm -f $(PREFIX)/lib/libmirage_embedded.a
	rm -f $(PREFIX)/include/mirage.h
	rm -f $(PREFIX)/include/mirage_style.h
	rm -f $(PREFIX)/include/mirage_embedded.h

python: $(LIB_SHARED)
	cd lang/python && pip install .

clean:
	rm -rf $(BUILD_DIR)
