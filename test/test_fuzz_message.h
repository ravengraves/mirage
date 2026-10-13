/* Shared caller-state regressions for the standard and embedded fuzz tests. */
#ifndef TEST_FUZZ_MESSAGE_H
#define TEST_FUZZ_MESSAGE_H

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#ifdef MIRAGE_EMBEDDED_H
typedef int16_t fuzz_size;
#define FUZZ_SIZE_MIN INT16_MIN
#define FUZZ_SIZE_MAX INT16_MAX
#else
typedef int64_t fuzz_size;
#define FUZZ_SIZE_MIN INT64_MIN
#define FUZZ_SIZE_MAX INT64_MAX
#endif

static void test_message_null_data_read(void) {
	uint8_t storage[] = {'8', ':', 'C', 42};
	mirage_msg msg;
	mirage_init(&msg, storage, sizeof(storage));
	msg.length = sizeof(storage);
	msg.cursor = 2;
	msg.data = NULL;
	int8_t value = 0;
	/* Old validators allow a NULL read (UBSan/ASan). */
	assert(mirage_read_i8(&msg, &value) == MIRAGE_ERR_BADINPUT);
	assert(value == 0 && msg.cursor == 2 && msg.length == sizeof(storage));
}

static void test_message_null_data_write(void) {
	uint8_t storage[16];
	mirage_msg msg;
	mirage_init(&msg, storage, sizeof(storage));
	msg.data = NULL;
	/* Old validators allow a NULL write (UBSan/ASan). */
	assert(mirage_write_i64(&msg, 42) == MIRAGE_ERR_BADINPUT);
	assert(msg.cursor == 0 && msg.length == 0);
}

static void assert_same_message(const mirage_msg *a, const mirage_msg *b) {
	assert(a->data == b->data && a->cursor == b->cursor);
	assert(a->length == b->length && a->max_length == b->max_length);
}

static void check_invalid_message(const mirage_msg *state, uint8_t *storage) {
	uint8_t before[16], out[16] = {0};
	memcpy(before, storage, sizeof(before));
	mirage_msg msg;
	mirage_msg *m = state ? &msg : NULL;
	int8_t i8_value = 0;
	int16_t i16_value = 0;
	int32_t i32_value = 0;
	int64_t i64_value = 0;
	double f64_value = 0;
	fuzz_size length = 0, argc = 0, rank = 0, dim = 1;
	mirage_array_type type = MIRAGE_ARRAY_U8;
	mirage_element_type element = MIRAGE_FUNCTION;

#define REJECT_STATE(call) do { \
	if(state) msg = *state; \
	assert((call) == MIRAGE_ERR_BADINPUT); \
	if(state) assert_same_message(&msg, state); \
	assert(memcmp(storage, before, sizeof(before)) == 0); \
} while(0)

	REJECT_STATE(mirage_read_i8(m, &i8_value));
	REJECT_STATE(mirage_read_i16(m, &i16_value));
	REJECT_STATE(mirage_read_i32(m, &i32_value));
	REJECT_STATE(mirage_read_i64(m, &i64_value));
	REJECT_STATE(mirage_read_f64(m, &f64_value));
	REJECT_STATE(mirage_read_start(m));
	REJECT_STATE(mirage_read_fn(m, (char *) out, &length, sizeof(out), &argc));
	REJECT_STATE(mirage_read_string(m, (char *) out, &length, sizeof(out)));
	REJECT_STATE(mirage_read_bin(m, out, &length, sizeof(out)));
	REJECT_STATE(mirage_read_sym(m, (char *) out, &length, sizeof(out)));
	REJECT_STATE(mirage_read_bigint(m, (char *) out, &length, sizeof(out)));
	REJECT_STATE(mirage_read_bigreal(m, (char *) out, &length, sizeof(out)));
	REJECT_STATE(mirage_read_numericarray(m, &type, &rank, &dim, 1, out, sizeof(out)));
	REJECT_STATE(mirage_read_numericarray(m, &type, &rank, &dim, 1, NULL, MIRAGE_SKIP_DATA));
	REJECT_STATE(mirage_read_association(m, &length));
	REJECT_STATE(mirage_read_assoc_rule(m));
	REJECT_STATE(mirage_peek_element_type(m, &element));
	REJECT_STATE(mirage_write_start(m));
	REJECT_STATE(mirage_write_fn(m, "f", MIRAGE_STRLEN, 0));
	REJECT_STATE(mirage_write_i8(m, 1));
	REJECT_STATE(mirage_write_i16(m, 1));
	REJECT_STATE(mirage_write_i32(m, 1));
	REJECT_STATE(mirage_write_i64(m, 1));
	REJECT_STATE(mirage_write_f64(m, 1));
	REJECT_STATE(mirage_write_string(m, "s", MIRAGE_STRLEN));
	REJECT_STATE(mirage_write_bin(m, out, 1));
	REJECT_STATE(mirage_write_sym(m, "s", MIRAGE_STRLEN));
	REJECT_STATE(mirage_write_bigint(m, "1", MIRAGE_STRLEN));
	REJECT_STATE(mirage_write_bigreal(m, "1", MIRAGE_STRLEN));
	REJECT_STATE(mirage_write_numericarray(m, MIRAGE_ARRAY_U8, 1, &dim, out));
	REJECT_STATE(mirage_write_association(m, 1));
	REJECT_STATE(mirage_write_assoc_rule(m));
#ifdef MIRAGE_EMBEDDED_H
	if(state) msg = *state;
	mirage_reset(m);
	if(state) assert_same_message(&msg, state);
#else
	REJECT_STATE(mirage_reset(m));
#endif
	assert(mirage_remaining(m) == 0);
	assert(memcmp(storage, before, sizeof(before)) == 0);
	assert(i8_value == 0 && i16_value == 0 && i32_value == 0 && i64_value == 0);
	assert(f64_value == 0 && length == 0 && argc == 0 && rank == 0 && dim == 1);
	assert(type == MIRAGE_ARRAY_U8 && element == MIRAGE_FUNCTION);
	for(size_t i = 0; i < sizeof(out); ++i) assert(out[i] == 0);
#undef REJECT_STATE
}

static void test_invalid_message_states(void) {
	/* Exact allocation exposes negative-cursor accesses under ASan. */
	uint8_t *storage = malloc(16);
	assert(storage);
	memset(storage, 0xA5, 16);
	check_invalid_message(NULL, storage);
	mirage_msg invalid[] = {
		{16, 4, 2, NULL},
		{0, 0, 0, NULL},
		{16, 4, -1, storage},
		{16, 4, FUZZ_SIZE_MIN, storage},
		{-1, 0, 0, storage},
		{FUZZ_SIZE_MIN, 0, 0, storage},
		{16, -1, 0, storage},
		{16, FUZZ_SIZE_MIN, 0, storage},
		{16, 17, 0, storage},
		{16, FUZZ_SIZE_MAX, 0, storage},
		{16, 4, 5, storage},
		{16, 4, 17, storage},
		{16, 4, FUZZ_SIZE_MAX, storage},
	};
	for(size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
		check_invalid_message(&invalid[i], storage);
	}
	free(storage);
}

static void test_message_init_and_boundaries(void) {
	uint8_t storage[16];
	mirage_msg msg;
	const fuzz_size sizes[] = {0, -1, FUZZ_SIZE_MIN};
	for(size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
		mirage_init(&msg, storage, sizeof(storage));
		mirage_init(&msg, storage, sizes[i]);
		assert(!msg.data && !msg.max_length && !msg.length && !msg.cursor);
		assert(mirage_write_start(&msg) == MIRAGE_ERR_BADINPUT);
	}
	mirage_init(&msg, NULL, sizeof(storage));
	assert(!msg.data && !msg.max_length && !msg.length && !msg.cursor);
	mirage_init(NULL, storage, sizeof(storage));

	mirage_init(&msg, storage, 2);
	assert(mirage_write_start(&msg) == 2);
	assert(mirage_remaining(&msg) == 0);
	assert(mirage_write_i8(&msg, 1) == MIRAGE_ERR_BUFFERSIZE);
	int8_t value;
	assert(mirage_read_i8(&msg, &value) == MIRAGE_ERR_BUFFERSIZE);
	assert(mirage_read_start(&msg) == 2);
	mirage_reset(&msg);
	assert(msg.length == 0 && msg.cursor == 0);
	assert(mirage_remaining(&msg) == 2);
	/* Non-NULL storage with zero capacity is valid, but cannot hold a header. */
	msg.max_length = 0;
	assert(mirage_write_start(&msg) == MIRAGE_ERR_BUFFERSIZE);
	assert(mirage_read_start(&msg) == MIRAGE_ERR_BUFFERSIZE);
}

#endif
