CC := clang
AR := llvm-ar
TARGET := normal
PREFIX ?= /usr/local

CFLAGS := -Wall -Wextra -Wpedantic -Werror -std=c11 -fPIC -flto \
	-fno-stack-protector -D_FORTIFY_SOURCE=0 \
	-I quicksand/include -Os -ffast-math -mtune=native
LDFLAGS := -flto


ifeq ($(TARGET),embedded)
SOURCE := mirage+embedded.c
else
SOURCE := mirage.c
endif

all: build/mirage.o build/mirage.a

build/mirage.so: build/mirage.o
	$(CC) -shared -o build/mirage.so build/mirage.o $(LDFLAGS)

build/mirage.a: build/mirage.o
	$(AR) rcs build/mirage.a build/mirage.o

build/mirage.o: $(SOURCE)
	mkdir -p build
	$(CC) -c $(CFLAGS) $(SOURCE) -o build/mirage.o

compile_commands.json: Makefile
	@echo '[\n' \
	'{"directory":"$(PWD)","command":"$(CC) -c $(CFLAGS) $(SOURCE) -o build/mirage.o","file":"$(SOURCE)"},\n' \
	'{"directory":"$(PWD)","command":"$(CC) -shared -o build/mirage.so build/mirage.o $(LDFLAGS)","file":"build/mirage.so"},\n' \
	'{"directory":"$(PWD)","command":"$(AR) rcs build/mirage.a build/mirage.o","file":"build/mirage.a"},\n' \
	'{"directory":"$(PWD)","command":"$(CC) -o build/test/test_mirage test.c $(CFLAGS) build/mirage.a","file":"test.c"}\n]' \
	> $@

format:
	clang-format -style=file -i $$(find . | grep -e '\.c$$' -e '\.h$$')

clean:
	rm -rf build


.PHONY: format clean all

