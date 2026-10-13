/* libFuzzer entry point; build separately for each API and read/write mode. */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef EMBEDDED
#include "mirage_embedded.h"
typedef int16_t count_t;
#define COUNT_MAX INT16_MAX
#else
#include "mirage.h"
typedef int64_t count_t;
#define COUNT_MAX INT64_MAX
#endif

int LLVMFuzzerTestOneInput(const uint8_t *input, size_t size) {
#ifdef FUZZ_WRITER
	if(size < 8) return 0;
	uint8_t storage[257], before[257], native[257], output[257];
	for(size_t i = 0; i < sizeof(native); ++i) native[i] = (uint8_t) i;
	memset(storage, 0xA5, sizeof(storage));
	memset(output, 0xA5, sizeof(output));
	count_t capacity = 2 + input[0] % 254, rank = input[1] % 5;
	int type = (input[2] % 15) - 1, offset = input[7] & 1;
	count_t dims[4];
	for(int i = 0; i < 4; ++i) dims[i] = input[3 + i] == 255 ? COUNT_MAX : input[3 + i];
	mirage_msg msg;
	mirage_init(&msg, storage + offset, capacity);
	assert(mirage_write_start(&msg) == 2);
	memcpy(before, storage, sizeof(storage));
	count_t written = mirage_write_numericarray(&msg, (mirage_array_type) type, rank, dims, native + offset);
	if(written < 0) {
		assert(msg.cursor == 2 && msg.length == 2);
		assert(memcmp(storage, before, sizeof(storage)) == 0);
		return 0;
	}
	assert(written == msg.cursor - 2 && msg.cursor == msg.length && msg.length <= capacity);
	assert(memcmp(storage + offset + msg.length, before + offset + msg.length,
		sizeof(storage) - offset - msg.length) == 0);
	count_t end = msg.cursor;
	msg.cursor = 2;
	count_t decoded_rank = 99, decoded_dims[4];
	mirage_array_type decoded_type;
	count_t consumed = mirage_read_numericarray(&msg, &decoded_type, &decoded_rank,
		decoded_dims, 4, output + offset, 256);
	assert(consumed == written && msg.cursor == end && decoded_rank == rank && (int) decoded_type == type);
	int empty = 0;
	for(int i = 0; i < rank; ++i) {
		assert(decoded_dims[i] == dims[i]);
		if(dims[i] == 0) empty = 1;
	}
	size_t count = empty ? 0 : 1;
	if(!empty) for(int i = 0; i < rank; ++i) {
		assert(dims[i] <= 256 && count <= 256 / (size_t) dims[i]);
		count *= (size_t) dims[i];
	}
	const int widths[] = {1, 2, 4, 8, 1, 2, 4, 8, 4, 8, 8, 16};
	size_t bytes = count * widths[type];
	assert(bytes <= 256 && memcmp(native + offset, output + offset, bytes) == 0);
	for(size_t i = offset + bytes; i < sizeof(output); ++i) assert(output[i] == 0xA5);
	if(offset) assert(storage[0] == 0xA5 && output[0] == 0xA5);
#else
	if(!size || size > 1024) return 0;
	uint8_t *wire = malloc(size);
	assert(wire);
	memcpy(wire, input, size);
	const count_t rank_caps[] = {-2, -1, 0, 1, 16}, byte_caps[] = {-2, -1, 0, 1, 1024};
	for(int a = 0; a < 5; ++a) for(int b = 0; b < 5; ++b) {
		mirage_msg msg;
		mirage_init(&msg, wire, (count_t) size);
		msg.length = (count_t) size;
		mirage_array_type type = (mirage_array_type) 99;
		count_t rank = 99, dims[16];
		uint8_t out[1024];
		for(int i = 0; i < 16; ++i) dims[i] = 99;
		memset(out, 0xA5, sizeof(out));
		count_t rc = mirage_read_numericarray(&msg, &type, &rank,
			rank_caps[a] == -1 && (size & 1) ? NULL : dims, rank_caps[a],
			byte_caps[b] == -1 && (size & 1) ? NULL : out, byte_caps[b]);
		assert(msg.length == (count_t) size && msg.cursor >= 0 && msg.cursor <= (count_t) size);
		if(rc < 0) {
			assert(msg.cursor == 0 && rank == 99 && type == (mirage_array_type) 99);
			for(int i = 0; i < 16; ++i) assert(dims[i] == 99);
			for(int i = 0; i < 1024; ++i) assert(out[i] == 0xA5);
		} else {
			assert(rc == msg.cursor && rank >= 0 && (unsigned) type < 12);
			if(rank_caps[a] >= 0) assert(rank <= rank_caps[a]);
			for(int i = rank_caps[a] == -1 ? 0 : rank_caps[a]; i < 16; ++i) assert(dims[i] == 99);
			for(int i = byte_caps[b] == -1 ? 0 : byte_caps[b]; i < 1024; ++i) assert(out[i] == 0xA5);
		}
		assert(memcmp(wire, input, size) == 0);
	}
	free(wire);
#endif
	return 0;
}
