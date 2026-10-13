/* Shared numeric-array capacity regressions for both C APIs.
 * Include after test_fuzz_message.h for the API's fuzz_size type. */
#ifndef TEST_NUMERICARRAY_H
#define TEST_NUMERICARRAY_H

static void test_numeric_roundtrip_modes(void) {
	for(int t = MIRAGE_ARRAY_I8; t <= MIRAGE_ARRAY_C64; ++t) {
		for(int shape = 0; shape < 3; ++shape) {
			fuzz_size rank = shape == 0 ? 0 : 2;
			fuzz_size dims[] = {shape == 2 ? 129 : 2, shape == 2 ? 0 : 3};
			fuzz_size count = shape == 0 ? 1 : (shape == 1 ? 6 : 0);
			fuzz_size bytes = count * mirage_array_type_size[t];
			uint8_t wire[128], native[96], out[96];
			for(size_t i = 0; i < sizeof(native); ++i) native[i] = (uint8_t) (i + 1);
			mirage_msg msg;
			mirage_init(&msg, wire, sizeof(wire));
			assert(mirage_write_start(&msg) == 2);
			assert(mirage_write_numericarray(&msg, (mirage_array_type) t,
				rank, rank ? dims : NULL, native) > 0);
			fuzz_size end = msg.cursor;
			/* Read both numeric and packed tags, with each output independently skipped. */
			for(int packed = 0; packed <= 1; ++packed) {
				wire[2] = packed ? 0xC1 : 0xC2;
				for(int mode = 0; mode < 4; ++mode) {
					int skip_dims = mode & 1, skip_data = mode & 2;
					for(int null_skipped = 0; null_skipped <= 1; ++null_skipped) {
						mirage_array_type type = (mirage_array_type) 99;
						fuzz_size out_rank = 99, out_dims[] = {99, 99, 99};
						memset(out, 0xA5, sizeof(out));
						msg.cursor = 2;
						assert(mirage_read_numericarray(&msg, &type, &out_rank,
							null_skipped && (skip_dims || rank == 0) ? NULL : out_dims,
							skip_dims ? -1 : rank, null_skipped && skip_data ? NULL : out,
							skip_data ? MIRAGE_SKIP_DATA : bytes) == end - 2);
						assert(msg.cursor == end && msg.length == end);
						assert(type == (mirage_array_type) t && out_rank == rank);
						for(int i = 0; i < 3; ++i) {
							assert(out_dims[i] == (!skip_dims && i < rank ? dims[i] : 99));
						}
						for(size_t i = 0; i < sizeof(out); ++i) {
							assert(out[i] == (!skip_data && i < (size_t) bytes ? native[i] : 0xA5));
						}
					}
				}
			}
		}
	}
}

/* Verify every error is atomic, including errors discovered after dimensions. */
static void check_numeric_rejected(uint8_t *wire, fuzz_size length,
		fuzz_size max_rank, fuzz_size max_bytes, unsigned null_outputs,
		fuzz_size error) {
	mirage_msg msg;
	mirage_init(&msg, wire, length);
	msg.length = length;
	msg.cursor = 2;
	mirage_array_type type = (mirage_array_type) 99;
	fuzz_size rank = 99, dims[] = {99, 99, 99};
	uint8_t out[16];
	memset(out, 0xA5, sizeof(out));
	assert(mirage_read_numericarray(&msg,
		null_outputs & 1 ? NULL : &type, null_outputs & 2 ? NULL : &rank,
		null_outputs & 4 ? NULL : dims, max_rank,
		null_outputs & 8 ? NULL : out, max_bytes) == error);
	assert(msg.cursor == 2 && msg.length == length);
	assert(type == (mirage_array_type) 99 && rank == 99);
	for(size_t i = 0; i < sizeof(dims) / sizeof(dims[0]); ++i) assert(dims[i] == 99);
	for(size_t i = 0; i < sizeof(out); ++i) assert(out[i] == 0xA5);
}

static void test_numeric_capacities(void) {
	uint8_t wire[] = {'8', ':', 0xC2, 0x10, 2, 1, 1, 42};
	const fuzz_size invalid[][2] = {
		{-2, 1}, {FUZZ_SIZE_MIN, 1}, {-2, -1}, {FUZZ_SIZE_MIN, -1},
		{2, -2}, {2, FUZZ_SIZE_MIN}, {-1, -2}, {-1, FUZZ_SIZE_MIN},
	};
	for(size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
		check_numeric_rejected(wire, sizeof(wire), invalid[i][0], invalid[i][1],
			0, MIRAGE_ERR_BADINPUT);
	}
	const fuzz_size short_capacity[][2] = {{0, 1}, {1, 1}, {0, -1}, {1, -1}, {2, 0}, {-1, 0}};
	for(size_t i = 0; i < sizeof(short_capacity) / sizeof(short_capacity[0]); ++i) {
		check_numeric_rejected(wire, sizeof(wire), short_capacity[i][0], short_capacity[i][1],
			0, MIRAGE_ERR_BUFFERSIZE);
	}
	for(int mode = 0; mode < 4; ++mode) {
		fuzz_size max_rank = mode & 1 ? -1 : 2;
		fuzz_size max_bytes = mode & 2 ? -1 : 1;
		check_numeric_rejected(wire, sizeof(wire), max_rank, max_bytes, 1, MIRAGE_ERR_BADINPUT);
		check_numeric_rejected(wire, sizeof(wire), max_rank, max_bytes, 2, MIRAGE_ERR_BADINPUT);
	}
	check_numeric_rejected(wire, sizeof(wire), 2, 1, 4, MIRAGE_ERR_BADINPUT);
	check_numeric_rejected(wire, sizeof(wire), 2, -1, 4, MIRAGE_ERR_BADINPUT);
	check_numeric_rejected(wire, sizeof(wire), 2, 1, 8, MIRAGE_ERR_BADINPUT);
	check_numeric_rejected(wire, sizeof(wire), -1, 1, 8, MIRAGE_ERR_BADINPUT);
	check_numeric_rejected(wire, sizeof(wire), 0, -1, 12, MIRAGE_ERR_BUFFERSIZE);
	uint8_t scalar[] = {'8', ':', 0xC2, 0x10, 0, 42};
	check_numeric_rejected(scalar, sizeof(scalar), 1, -1, 4, MIRAGE_ERR_BADINPUT);
	check_numeric_rejected(scalar, sizeof(scalar), 0, 0, 4, MIRAGE_ERR_BUFFERSIZE);
	uint8_t empty[] = {'8', ':', 0xC2, 0x10, 1, 0};
	check_numeric_rejected(empty, sizeof(empty), 1, 0, 8, MIRAGE_ERR_BADINPUT);
	check_numeric_rejected(empty, sizeof(empty), -1, 0, 8, MIRAGE_ERR_BADINPUT);

	/* Large legal capacities must not change which outputs are copied. */
	mirage_msg msg;
	mirage_init(&msg, wire, sizeof(wire));
	msg.length = sizeof(wire);
	msg.cursor = 2;
	mirage_array_type type;
	fuzz_size rank, dims[2];
	uint8_t out;
	assert(mirage_read_numericarray(&msg, &type, &rank, dims, FUZZ_SIZE_MAX,
		&out, FUZZ_SIZE_MAX) == sizeof(wire) - 2);
	assert(type == MIRAGE_ARRAY_U8 && rank == 2 && dims[0] == 1 && dims[1] == 1 && out == 42);
}

static fuzz_size numeric_append_varu(uint8_t *wire, fuzz_size offset, uint64_t value) {
	do {
		uint8_t byte = (uint8_t) (value & 0x7F);
		value >>= 7;
		wire[offset++] = byte | (value ? 0x80 : 0);
	} while(value);
	return offset;
}

static void test_numeric_malformed_modes(void) {
	for(int mode = 0; mode < 4; ++mode) {
		fuzz_size max_rank = mode & 1 ? -1 : 2;
		fuzz_size max_bytes = mode & 2 ? -1 : 16;
		uint8_t wire[32] = {'8', ':', 0xC2, 0x10, 2, 2, 3, 1, 2, 3, 4, 5, 6};
		/* Every truncated prefix, including dimensions and the last payload byte. */
		for(fuzz_size len = 2; len < 13; ++len) {
			check_numeric_rejected(wire, len, max_rank, max_bytes, 0, MIRAGE_ERR_BUFFERSIZE);
		}
		wire[6] = 0x80; /* Unterminated dimension varint. */
		check_numeric_rejected(wire, 7, max_rank, max_bytes, 0, MIRAGE_ERR_BUFFERSIZE);
		wire[4] = 0x80; /* Unterminated rank varint. */
		check_numeric_rejected(wire, 5, max_rank, max_bytes, 0, MIRAGE_ERR_BUFFERSIZE);
		wire[4] = 0;
		check_numeric_rejected(wire, 5, max_rank, max_bytes, 0, MIRAGE_ERR_BUFFERSIZE);
		wire[2] = 'S';
		check_numeric_rejected(wire, 6, max_rank, max_bytes, 0, MIRAGE_ERR_BADDATA);
		wire[2] = 0xC2;
		wire[3] = 0xFF;
		check_numeric_rejected(wire, 6, max_rank, max_bytes, 0, MIRAGE_ERR_BADDATA);
		wire[3] = 0x10;
		wire[4] = 2;
		fuzz_size len = numeric_append_varu(wire, 5, (uint64_t) FUZZ_SIZE_MAX / 2 + 1);
		wire[len++] = 2; /* Element-count overflow. */
		check_numeric_rejected(wire, len, max_rank, max_bytes, 0, MIRAGE_ERR_BADDATA);
		wire[3] = 0x11;
		wire[4] = 1; /* Payload-byte overflow for U16. */
		check_numeric_rejected(wire, len - 1, max_rank, max_bytes, 0, MIRAGE_ERR_BADDATA);
		wire[3] = 0x10;
		len = numeric_append_varu(wire, 4, (uint64_t) FUZZ_SIZE_MAX + 1);
		check_numeric_rejected(wire, len, max_rank, max_bytes, 0, MIRAGE_ERR_BADDATA);
		wire[4] = 2;
		wire[5] = 0; /* Even an empty array must validate all dimensions. */
		len = numeric_append_varu(wire, 6, (uint64_t) FUZZ_SIZE_MAX + 1);
		check_numeric_rejected(wire, len, max_rank, max_bytes, 0, MIRAGE_ERR_BADDATA);
	}
}

static void test_numeric_empty_products(void) {
	/* Each position of zero must suppress even an earlier overflowing product. */
	const fuzz_size shapes[][3] = {
		{0, FUZZ_SIZE_MAX, FUZZ_SIZE_MAX},
		{FUZZ_SIZE_MAX, 0, FUZZ_SIZE_MAX},
		{FUZZ_SIZE_MAX, FUZZ_SIZE_MAX, 0},
	};
	for(int t = MIRAGE_ARRAY_I8; t <= MIRAGE_ARRAY_C64; ++t) {
		for(size_t shape = 0; shape < sizeof(shapes) / sizeof(shapes[0]); ++shape) {
			uint8_t wire[64], expected[64] = {'8', ':', 0xC2, mirage_array_type_encoding[t], 3};
			fuzz_size end = 5;
			for(int i = 0; i < 3; ++i) end = numeric_append_varu(expected, end, shapes[shape][i]);
			mirage_msg msg;
			mirage_init(&msg, wire, sizeof(wire));
			assert(mirage_write_start(&msg) == 2);
			uint8_t data = 0xA5;
			assert(mirage_write_numericarray(&msg, (mirage_array_type) t, 3, shapes[shape], &data) == end - 2);
			assert(msg.cursor == end && msg.length == end && memcmp(wire, expected, end) == 0);
			for(int mode = 0; mode < 4; ++mode) {
				mirage_array_type type = (mirage_array_type) 99;
				fuzz_size rank = 99, dims[] = {99, 99, 99};
				msg.cursor = 2;
				assert(mirage_read_numericarray(&msg, &type, &rank, dims, mode & 1 ? -1 : 3,
					&data, mode & 2 ? -1 : 0) == end - 2);
				assert(type == (mirage_array_type) t && rank == 3 && data == 0xA5 && msg.cursor == end);
				for(int i = 0; i < 3; ++i) assert(dims[i] == (mode & 1 ? 99 : shapes[shape][i]));
			}
		}
	}
	/* A zero must never hide a negative dimension or malformed later varint. */
	uint8_t wire[64], before[64];
	memset(wire, 0xA5, sizeof(wire));
	mirage_msg msg;
	mirage_init(&msg, wire, sizeof(wire));
	assert(mirage_write_start(&msg) == 2);
	memcpy(before, wire, sizeof(wire));
	fuzz_size dims[] = {0, -1};
	assert(mirage_write_numericarray(&msg, MIRAGE_ARRAY_U8, 2, dims, before) == MIRAGE_ERR_BADINPUT);
	assert(msg.cursor == 2 && msg.length == 2 && memcmp(wire, before, sizeof(wire)) == 0);
	uint8_t malformed[32] = {'8', ':', 0xC2, 0x10, 3};
	fuzz_size end = numeric_append_varu(malformed, 5, FUZZ_SIZE_MAX);
	end = numeric_append_varu(malformed, end, FUZZ_SIZE_MAX);
	malformed[end++] = 0x80;
	for(int mode = 0; mode < 4; ++mode) {
		check_numeric_rejected(malformed, end, mode & 1 ? -1 : 3, mode & 2 ? -1 : 16,
			0, MIRAGE_ERR_BUFFERSIZE);
	}
}

static void test_numericarray(void) {
	test_numeric_roundtrip_modes();
	test_numeric_capacities();
	test_numeric_malformed_modes();
	test_numeric_empty_products();
}

#endif
