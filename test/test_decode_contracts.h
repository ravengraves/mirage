/* Shared C/embedded parser contracts. Include after test_numericarray.h. */
#ifndef TEST_DECODE_CONTRACTS_H
#define TEST_DECODE_CONTRACTS_H

static mirage_msg contract_message(uint8_t *wire, fuzz_size length) {
	mirage_msg msg;
	mirage_init(&msg, wire, length);
	msg.length = length;
	msg.cursor = 2;
	return msg;
}

static fuzz_size read_sized(mirage_msg *msg, int kind, char *out,
		fuzz_size *length, fuzz_size capacity, fuzz_size *argc) {
	switch(kind) {
	case 0: return mirage_read_string(msg, out, length, capacity);
	case 1: return mirage_read_bin(msg, (uint8_t *) out, length, capacity);
	case 2: return mirage_read_sym(msg, out, length, capacity);
	case 3: return mirage_read_bigint(msg, out, length, capacity);
	case 4: return mirage_read_bigreal(msg, out, length, capacity);
	default: return mirage_read_fn(msg, out, length, capacity, argc);
	}
}

static void test_sized_readers(void) {
	const uint8_t tags[] = {'S', 'B', 's', 'I', 'R', 'f'};
	const fuzz_size capacities[] = {FUZZ_SIZE_MIN, -2, -1, 0, 1, 2, 8};
	for(int kind = 0; kind < 6; ++kind) {
		for(int empty = 0; empty <= 1; ++empty) {
			uint8_t wire[16] = {'8', ':', tags[kind]};
			fuzz_size header = 3;
			if(kind == 5) { wire[header++] = 2; wire[header++] = 's'; }
			wire[header++] = empty ? 0 : 2;
			wire[header] = 'a'; wire[header + 1] = 'b';
			fuzz_size length = header + (empty ? 0 : 2);
			for(size_t i = 0; i < sizeof(capacities) / sizeof(capacities[0]); ++i) {
				for(int null_output = 0; null_output <= 1; ++null_output) {
					mirage_msg msg = contract_message(wire, length);
					char out[8]; memset(out, 0xA5, sizeof(out));
					fuzz_size n = 99, argc = 99, cap = capacities[i];
					fuzz_size expected = cap < -1 || (cap > 0 && null_output) ? MIRAGE_ERR_BADINPUT
						: cap >= 0 && cap < (empty ? 0 : 2) ? MIRAGE_ERR_BUFFERSIZE : length - 2;
					assert(read_sized(&msg, kind, null_output ? NULL : out, &n, cap, &argc) == expected);
					assert(msg.length == length && msg.cursor == (expected < 0 ? 2 : length));
					assert(n == (expected < 0 ? 99 : empty ? 0 : 2));
					assert(argc == (expected >= 0 && kind == 5 ? 2 : 99));
					for(size_t j = 0; j < sizeof(out); ++j) {
						assert((uint8_t) out[j] == (expected >= 0 && cap > 0 && !empty && j < 2 ? 'a' + j : 0xA5));
					}
				}
			}
			/* Truncation at every field, missing output length, and wrong tags. */
			for(fuzz_size cut = 2; cut < length; ++cut) {
				mirage_msg msg = contract_message(wire, cut);
				char out[8]; memset(out, 0xA5, sizeof(out));
				fuzz_size n = 99, argc = 99;
				assert(read_sized(&msg, kind, out, &n, sizeof(out), &argc) == MIRAGE_ERR_BUFFERSIZE);
				assert(msg.cursor == 2 && n == 99 && argc == 99);
				for(size_t j = 0; j < sizeof(out); ++j) assert((uint8_t) out[j] == 0xA5);
			}
			mirage_msg msg = contract_message(wire, length);
			fuzz_size n = 99, argc = 99;
			assert(read_sized(&msg, kind, NULL, NULL, -1, &argc) == MIRAGE_ERR_BADINPUT);
			assert(msg.cursor == 2 && argc == 99);
			assert(read_sized(&msg, kind, NULL, &n, -1, NULL) == length - 2);
			assert(n == (empty ? 0 : 2));
			wire[2] = 'Z'; msg.cursor = 2; n = 99;
			assert(read_sized(&msg, kind, NULL, &n, -1, &argc) == MIRAGE_ERR_BADDATA);
			assert(msg.cursor == 2 && n == 99 && argc == 99);
		}
	}
	uint8_t wrong_head[] = {'8', ':', 'f', 2, 'S', 0};
	mirage_msg msg = contract_message(wrong_head, sizeof(wrong_head));
	fuzz_size length = 99, argc = 99;
	assert(mirage_read_fn(&msg, NULL, &length, -1, &argc) == MIRAGE_ERR_BADDATA);
	assert(msg.cursor == 2 && length == 99 && argc == 99);
}

/* Route the same invalid varint through every metadata field. */
static void check_varu_error(const uint8_t *encoded, fuzz_size bytes, fuzz_size error) {
	for(int field = 0; field < 10; ++field) {
		uint8_t wire[32] = {'8', ':'};
		fuzz_size prefix = 2;
		if(field < 5) wire[prefix++] = (const uint8_t[]) {'S', 'B', 's', 'I', 'R'}[field];
		else if(field < 7) {
			wire[prefix++] = 'f';
			if(field == 6) { wire[prefix++] = 1; wire[prefix++] = 's'; }
		} else if(field == 7) wire[prefix++] = 'A';
		else {
			wire[prefix++] = 0xC2; wire[prefix++] = 0x10;
			if(field == 9) { wire[prefix++] = 2; wire[prefix++] = 0; }
		}
		memcpy(wire + prefix, encoded, bytes);
		mirage_msg msg = contract_message(wire, prefix + bytes);
		fuzz_size length = 99, argc = 99, rank = 99, dims[] = {99, 99};
		mirage_array_type type = (mirage_array_type) 99;
		fuzz_size rc;
		if(field < 7) rc = read_sized(&msg, field < 5 ? field : 5, NULL, &length, -1, &argc);
		else if(field == 7) rc = mirage_read_association(&msg, &length);
		else rc = mirage_read_numericarray(&msg, &type, &rank, dims, 2, NULL, -1);
		assert(rc == error && msg.cursor == 2);
		assert(length == 99 && argc == 99 && rank == 99 && type == (mirage_array_type) 99);
		assert(dims[0] == 99 && dims[1] == 99);
	}
}

static void test_varu_boundaries(void) {
	const fuzz_size values[] = {0, 127, 128, 16383, 16384, FUZZ_SIZE_MAX - 1, FUZZ_SIZE_MAX};
	for(size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		uint8_t wire[64] = {'8', ':', 'A'}, expected[16] = {'8', ':', 'A'};
		fuzz_size end = numeric_append_varu(expected, 3, values[i]);
		mirage_msg msg;
		mirage_init(&msg, wire, sizeof(wire));
		assert(mirage_write_start(&msg) == 2);
		assert(mirage_write_association(&msg, values[i]) == end - 2);
		assert(msg.cursor == end && msg.length == end && memcmp(wire, expected, end) == 0);
		msg.cursor = 2;
		fuzz_size count = 99;
		assert(mirage_read_association(&msg, &count) == end - 2 && count == values[i]);
		assert(msg.cursor == end);
		assert(mirage_write_start(&msg) == 2);
		fuzz_size written = mirage_write_fn(&msg, "x", 1, values[i]);
		assert(written > 0 && written == msg.cursor - 2);
		msg.cursor = 2; count = 99;
		fuzz_size length = 99;
		assert(mirage_read_fn(&msg, NULL, &length, -1, &count) == written);
		assert(count == values[i] && length == 1);
	}
	uint8_t encoded[16];
	fuzz_size max_bytes = numeric_append_varu(encoded, 0, FUZZ_SIZE_MAX);
	for(fuzz_size cut = 0; cut < max_bytes; ++cut) check_varu_error(encoded, cut, MIRAGE_ERR_BUFFERSIZE);
	fuzz_size bytes = numeric_append_varu(encoded, 0, (uint64_t) FUZZ_SIZE_MAX + 1);
	check_varu_error(encoded, bytes, MIRAGE_ERR_BADDATA);
	memset(encoded, 0x80, sizeof(encoded));
	check_varu_error(encoded, max_bytes, MIRAGE_ERR_BADDATA);
	encoded[max_bytes] = 0;
	check_varu_error(encoded, max_bytes + 1, MIRAGE_ERR_BADDATA);
	/* Nonminimal encodings that fit the supported width still work. */
	uint8_t wire[16] = {'8', ':', 'A'};
	memset(wire + 3, 0x80, max_bytes);
	wire[2 + max_bytes] = 0;
	mirage_msg msg = contract_message(wire, 3 + max_bytes);
	fuzz_size count = 99;
	assert(mirage_read_association(&msg, &count) == 1 + max_bytes && count == 0);
	assert(msg.cursor == msg.length);
}

static void test_message_contracts(void) {
	uint8_t wire[16] = {'7', ':'};
	mirage_msg msg = contract_message(wire, sizeof(wire));
	msg.cursor = 7;
	assert(mirage_read_start(&msg) == MIRAGE_ERR_BADDATA && msg.cursor == 7);
	msg.length = msg.cursor = 1;
	assert(mirage_read_start(&msg) == MIRAGE_ERR_BUFFERSIZE && msg.cursor == 1);
	wire[0] = '8'; msg.length = sizeof(wire); msg.cursor = 7;
	assert(mirage_read_start(&msg) == 2 && msg.cursor == 2);
	uint8_t before[16]; memcpy(before, wire, sizeof(wire));
	fuzz_size length = msg.length;
	assert(mirage_write_association(&msg, -1) == MIRAGE_ERR_BADINPUT);
	assert(mirage_write_association(&msg, FUZZ_SIZE_MIN) == MIRAGE_ERR_BADINPUT);
	assert(msg.cursor == 2 && msg.length == length && memcmp(wire, before, sizeof(wire)) == 0);
	const uint8_t tags[] = {0xC1, 0xC2, ':', '-'};
	for(size_t i = 0; i < sizeof(tags); ++i) {
		wire[2] = tags[i];
		mirage_element_type type = MIRAGE_FUNCTION;
		assert(mirage_peek_element_type(&msg, &type) == 1 && msg.cursor == 2);
		assert(type == (i < 2 ? MIRAGE_NUMERICARRAY : MIRAGE_RULE));
	}
}

#ifndef MIRAGE_EMBEDDED_H
static int contract_allocations;
static void *contract_allocator(size_t bytes) {
	++contract_allocations;
	void *p = malloc(bytes);
	if(p) memset(p, 'C', bytes);
	return p;
}

static void test_create_header(void) {
	assert(mirage_create(-1, contract_allocator) == NULL);
	assert(mirage_create(0, contract_allocator) == NULL);
	assert(mirage_create(1, contract_allocator) == NULL);
	assert(contract_allocations == 0);
	mirage_msg *msg = mirage_create(2, contract_allocator);
	assert(msg && contract_allocations == 1);
	assert(msg->length == 2 && msg->cursor == 2 && memcmp(msg->data, "8:", 2) == 0);
	mirage_destroy(&msg, free);
}
#endif

static void test_decode_contracts(void) {
	test_sized_readers();
	test_varu_boundaries();
	test_message_contracts();
#ifndef MIRAGE_EMBEDDED_H
	test_create_header();
#endif
}

#endif
