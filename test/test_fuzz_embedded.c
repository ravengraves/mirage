/* Invalid caller inputs and alignment regressions for the embedded API. */
#include "mirage_embedded.h"
#include "test_fuzz_message.h"
#include "test_numericarray.h"
#include "test_decode_contracts.h"
#include <stdio.h>

static mirage_msg array_message(uint8_t *wire, int16_t length) {
	mirage_msg msg;
	mirage_init(&msg, wire, length);
	msg.length = length;
	assert(mirage_read_start(&msg) == 2);
	return msg;
}

static void test_numeric_null_destination(void) {
	for(int t = MIRAGE_ARRAY_I8; t <= MIRAGE_ARRAY_C64; ++t) {
		uint8_t wire[21] = {'8', ':', 0xC2, mirage_array_type_encoding[t], 0};
		int16_t bytes = mirage_array_type_size[t];
		mirage_msg msg = array_message(wire, 5 + bytes);
		mirage_array_type type = MIRAGE_ARRAY_U8;
		int16_t rank = 99;
		/* Nonempty rank-zero payload: the old byte copy dereferences NULL. */
		assert(mirage_read_numericarray(&msg, &type, &rank, NULL, 0,
						NULL, bytes) == MIRAGE_ERR_BADINPUT);
		assert(msg.cursor == 2 && rank == 99 && type == MIRAGE_ARRAY_U8);
	}
	/* Copy mode requires data even if the declared payload is empty. */
	uint8_t empty[] = {'8', ':', 0xC2, 0x10, 1, 0};
	mirage_msg msg = array_message(empty, sizeof(empty));
	mirage_array_type type;
	int16_t rank, dim;
	assert(mirage_read_numericarray(&msg, &type, &rank, &dim, 1,
					NULL, 0) == MIRAGE_ERR_BADINPUT);
	assert(msg.cursor == 2);
}

static void test_numeric_negative_capacities(void) {
	uint8_t wire[] = {'8', ':', 0xC2, 0x10, 2, 1, 1, 42};
	/* One dimension of storage, two on the wire: old -2 rank check writes
	 * past the heap allocation, independently of return-code assertions. */
	int16_t *dim = malloc(sizeof(*dim));
	assert(dim);
	const int16_t capacities[][2] = {
		{-2, 1}, {INT16_MIN, 1},
		{-2, MIRAGE_SKIP_DATA}, {INT16_MIN, MIRAGE_SKIP_DATA},
		{1, -2}, {1, INT16_MIN}, {-1, -2}, {-1, INT16_MIN},
	};
	for(size_t i = 0; i < sizeof(capacities) / sizeof(capacities[0]); ++i) {
		mirage_msg msg = array_message(wire, sizeof(wire));
		mirage_array_type type = MIRAGE_ARRAY_F64;
		int16_t rank = 99;
		uint8_t out = 0xA5;
		*dim = 99;
		assert(mirage_read_numericarray(&msg, &type, &rank, dim, capacities[i][0],
						&out, capacities[i][1]) == MIRAGE_ERR_BADINPUT);
		assert(msg.cursor == 2 && rank == 99 && *dim == 99);
		assert(type == MIRAGE_ARRAY_F64 && out == 0xA5);
	}
	free(dim);
}

/* Construct host bytes and little-endian wire bytes independently of Mirage.
 * Include complex components, signed zero and NaN bit patterns verbatim. */
static void test_numeric_unaligned(int writing) {
	const struct { uint8_t encoding, width, components; } types[] = {
		{0x00, 1, 1}, {0x01, 2, 1}, {0x02, 4, 1}, {0x03, 8, 1},
		{0x10, 1, 1}, {0x11, 2, 1}, {0x12, 4, 1}, {0x13, 8, 1},
		{0x22, 4, 1}, {0x23, 8, 1}, {0x33, 4, 2}, {0x34, 8, 2},
	};
	const uint64_t patterns[] = {
		UINT64_C(0x0123456789ABCDEF), UINT64_C(0xFEDCBA9876543210),
		UINT64_C(0x8000000080000000), UINT64_C(0x7FF812347FC01234),
	};
	for(int t = MIRAGE_ARRAY_I8; t <= MIRAGE_ARRAY_C64; ++t) {
		for(int shape = 0; shape < 3; ++shape) {
			int16_t dims[] = {shape == 2 ? 0 : 2, 3};
			int16_t rank = shape == 0 ? 0 : 2;
			int16_t count = shape == 0 ? 1 : (shape == 1 ? 6 : 0);
			int16_t width = types[t].width;
			int16_t components = count * types[t].components;
			int16_t bytes = components * width;
			int16_t header = 5 + rank;
			uint8_t wire[103] = {'8', ':', 0xC2, types[t].encoding, (uint8_t) rank};
			uint8_t native[96];
			if(rank) {
				wire[5] = (uint8_t) dims[0];
				wire[6] = (uint8_t) dims[1];
			}
			for(int16_t i = 0; i < components; ++i) {
				uint64_t value = patterns[i % 4];
				uint8_t *p = native + i * width;
				switch(width) {
				case 1: *p = (uint8_t) value; break;
				case 2: { uint16_t v = (uint16_t) value; memcpy(p, &v, 2); break; }
				case 4: { uint32_t v = (uint32_t) value; memcpy(p, &v, 4); break; }
				case 8: memcpy(p, &value, 8); break;
				}
				for(int16_t j = 0; j < width; ++j) {
					wire[header + i * width + j] = (uint8_t) (value >> (8 * j));
				}
			}
			/* malloc is aligned; +1 deliberately misaligns all wider types.
			 * Exact allocation leaves an ASan redzone at the payload's end. */
			uint8_t *allocation = malloc((size_t) bytes + 1);
			uint8_t *message_storage = malloc((size_t) header + bytes + 1);
			assert(allocation && message_storage);
			allocation[0] = message_storage[0] = 0xA5;
			uint8_t *data = allocation + 1;
			mirage_msg msg;
			mirage_init(&msg, message_storage + 1, header + bytes);
			if(writing) {
				memcpy(data, native, bytes);
				assert(mirage_write_start(&msg) == 2);
				assert(mirage_write_numericarray(&msg, (mirage_array_type) t, rank,
					rank ? dims : NULL, data) == header + bytes - 2);
				assert(msg.cursor == header + bytes && msg.length == msg.cursor);
				assert(memcmp(msg.data, wire, msg.length) == 0);
				assert(memcmp(data, native, bytes) == 0);
			} else {
				/* Exercise both numeric and packed array tags. */
				for(int packed = 0; packed <= 1; ++packed) {
					wire[2] = packed ? 0xC1 : 0xC2;
					memcpy(msg.data, wire, header + bytes);
					memset(data, 0xA5, bytes);
					msg.length = header + bytes;
					assert(mirage_read_start(&msg) == 2);
					mirage_array_type type;
					int16_t out_rank, out_dims[2] = {-1, -1};
					assert(mirage_read_numericarray(&msg, &type, &out_rank,
						rank ? out_dims : NULL, rank, data, bytes) == header + bytes - 2);
					assert(msg.cursor == msg.length && type == (mirage_array_type) t);
					assert(out_rank == rank && memcmp(data, native, bytes) == 0);
					if(rank) assert(memcmp(out_dims, dims, sizeof(dims)) == 0);
				}
			}
			assert(allocation[0] == 0xA5 && message_storage[0] == 0xA5);
			free(message_storage);
			free(allocation);
		}
	}
}

static void test_numeric_unaligned_write(void) { test_numeric_unaligned(1); }
static void test_numeric_unaligned_read(void) { test_numeric_unaligned(0); }

int main(void) {
	test_numeric_null_destination();
	test_numeric_negative_capacities();
	test_numericarray();
	test_decode_contracts();
	test_numeric_unaligned_write();
	test_numeric_unaligned_read();
	test_message_null_data_read();
	test_message_null_data_write();
	test_invalid_message_states();
	test_message_init_and_boundaries();
	puts("Embedded fuzz-style caller input tests passed.");
	return 0;
}
