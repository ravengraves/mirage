// test_embedded.c - Test the embedded mirage implementation
// Uses stack-allocated buffer (no malloc) and int16_t lengths

#include "mirage_embedded.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define BUFFER_SIZE 4096

#define CHECK(expr)                                                 \
	do {                                                        \
		if((expr) < 0) {                                    \
			printf("FAIL line %d: %s = %d\n", __LINE__, \
			       #expr, (int) (expr));                \
			return 1;                                   \
		}                                                   \
	} while(0)

#define ASSERT_EQ(a, b)                                                 \
	do {                                                            \
		if((a) != (b)) {                                        \
			printf("FAIL line %d: %s=%d != %s=%d\n",        \
			       __LINE__, #a, (int) (a), #b, (int) (b)); \
			return 1;                                       \
		}                                                       \
	} while(0)

/* Failed writes must leave both the buffer and cursor untouched. */
static void test_writer_bounds(void) {
	uint8_t buffer[32], before[32];
	const uint8_t data[] = {1, 2, 3, 4};
	mirage_msg msg;
	memset(buffer, 0xA5, sizeof(buffer));
	mirage_init(&msg, buffer, sizeof(buffer));
	assert(mirage_write_start(&msg) == 2);
	memcpy(before, buffer, sizeof(buffer));

#define REJECT_WRITE(call, error) do { \
	assert((call) == (error)); \
	assert(msg.cursor == 2 && msg.length == 2); \
	assert(memcmp(buffer, before, sizeof(buffer)) == 0); \
} while(0)

	REJECT_WRITE(mirage_write_bin(&msg, data, INT16_MAX), MIRAGE_ERR_BUFFERSIZE);
	REJECT_WRITE(mirage_write_string(&msg, "x", INT16_MAX), MIRAGE_ERR_BUFFERSIZE);
	REJECT_WRITE(mirage_write_sym(&msg, "x", INT16_MAX), MIRAGE_ERR_BUFFERSIZE);
	REJECT_WRITE(mirage_write_bigint(&msg, "1", INT16_MAX), MIRAGE_ERR_BUFFERSIZE);
	REJECT_WRITE(mirage_write_bigreal(&msg, "1", INT16_MAX), MIRAGE_ERR_BUFFERSIZE);
	REJECT_WRITE(mirage_write_fn(&msg, "f", INT16_MAX, 0), MIRAGE_ERR_BUFFERSIZE);

	int16_t dims[40] = {INT16_MAX};
	REJECT_WRITE(mirage_write_numericarray(&msg, MIRAGE_ARRAY_U8, 1, dims, data),
		     MIRAGE_ERR_BUFFERSIZE);
	REJECT_WRITE(mirage_write_numericarray(&msg, MIRAGE_ARRAY_U64, 1, dims, data),
		     MIRAGE_ERR_BADINPUT);
	REJECT_WRITE(mirage_write_numericarray(&msg, (mirage_array_type) 12, 0, NULL, data),
		     MIRAGE_ERR_BADINPUT);
	REJECT_WRITE(mirage_write_numericarray(&msg, (mirage_array_type) -1, 0, NULL, data),
		     MIRAGE_ERR_BADINPUT);

	/* A zero-sized array can still have an oversized dimension header. */
	dims[0] = 0;
	for(size_t i = 1; i < sizeof(dims) / sizeof(dims[0]); ++i) {
		dims[i] = INT16_MAX;
	}
	REJECT_WRITE(mirage_write_numericarray(&msg, MIRAGE_ARRAY_U8, 40, dims, data),
		     MIRAGE_ERR_BUFFERSIZE);

	/* One byte short, then an exact fit at a nonzero cursor. */
	msg.max_length = 7;
	REJECT_WRITE(mirage_write_bin(&msg, data, 4), MIRAGE_ERR_BUFFERSIZE);
	msg.max_length = 8;
	assert(mirage_write_bin(&msg, data, 4) == 6);
	assert(msg.cursor == 8 && msg.length == 8);
	assert(memcmp(buffer + 4, data, 4) == 0 && buffer[8] == 0xA5);

	assert(mirage_write_start(&msg) == 2);
	memset(buffer + 2, 0xA5, sizeof(buffer) - 2);
	memcpy(before, buffer, sizeof(buffer));
	msg.max_length = 6;
	REJECT_WRITE(mirage_write_fn(&msg, "f", 1, 0), MIRAGE_ERR_BUFFERSIZE);
	msg.max_length = 7;
	assert(mirage_write_fn(&msg, "f", 1, 0) == 5);
	assert(msg.cursor == 7 && msg.length == 7);
	assert(memcmp(buffer + 2, "f\0s\1f", 5) == 0 && buffer[7] == 0xA5);

	assert(mirage_write_start(&msg) == 2);
	memset(buffer + 2, 0xA5, sizeof(buffer) - 2);
	memcpy(before, buffer, sizeof(buffer));
	dims[0] = 4;
	msg.max_length = 9;
	REJECT_WRITE(mirage_write_numericarray(&msg, MIRAGE_ARRAY_U8, 1, dims, data),
		     MIRAGE_ERR_BUFFERSIZE);
	msg.max_length = 10;
	assert(mirage_write_numericarray(&msg, MIRAGE_ARRAY_U8, 1, dims, data) == 8);
	assert(msg.cursor == 10 && msg.length == 10);
	assert(memcmp(buffer + 6, data, 4) == 0 && buffer[10] == 0xA5);

#undef REJECT_WRITE
}

static void test_strlen_bounds(void) {
	static char text[(size_t) INT16_MAX + 2];
	uint8_t buffer[32], before[32];
	mirage_msg msg;
	int16_t (*const writers[])(mirage_msg *, const char *, int16_t) = {
		mirage_write_string, mirage_write_sym,
		mirage_write_bigint, mirage_write_bigreal
	};
	memset(buffer, 0xA5, sizeof(buffer));
	mirage_init(&msg, buffer, sizeof(buffer));
	assert(mirage_write_start(&msg) == 2);
	memcpy(before, buffer, sizeof(buffer));
	memset(text, 'x', sizeof(text));
	text[INT16_MAX] = '\0';

	/* 32767 is representable, but does not fit this message buffer. */
	for(size_t i = 0; i < sizeof(writers) / sizeof(
		[0]); ++i) {
		assert(writers[i](&msg, text, MIRAGE_STRLEN) == MIRAGE_ERR_BUFFERSIZE);
	}
	assert(mirage_write_fn(&msg, text, MIRAGE_STRLEN, 0) == MIRAGE_ERR_BUFFERSIZE);
	assert(msg.cursor == 2 && msg.length == 2);
	assert(memcmp(buffer, before, sizeof(buffer)) == 0);

	/* 32768 must stop the scan before using the wrapped negative index. */
	text[INT16_MAX] = 'x';
	text[(size_t) INT16_MAX + 1] = '\0';
	for(size_t i = 0; i < sizeof(writers) / sizeof(writers[0]); ++i) {
		assert(writers[i](&msg, text, MIRAGE_STRLEN) == MIRAGE_ERR_BADINPUT);
	}
	assert(mirage_write_fn(&msg, text, MIRAGE_STRLEN, 0) == MIRAGE_ERR_BADINPUT);
	assert(msg.cursor == 2 && msg.length == 2);
	assert(memcmp(buffer, before, sizeof(buffer)) == 0);

	assert(mirage_write_string(&msg, "", MIRAGE_STRLEN) == 2);
	assert(mirage_write_string(&msg, "ok", MIRAGE_STRLEN) == 4);
}

int main(void) {
	test_writer_bounds();
	test_strlen_bounds();
	// Stack-allocated buffer - no malloc needed
	uint8_t buffer[BUFFER_SIZE];
	mirage_msg msg;

	printf("Testing embedded mirage (int16_t lengths, no malloc)\n");
	printf("Buffer size: %d bytes\n\n", BUFFER_SIZE);

	// Initialize message with our buffer
	mirage_init(&msg, buffer, BUFFER_SIZE);

	// ---- Write test data ----
	printf("Writing test data...\n");

	CHECK(mirage_write_start(&msg));

	// Write a List with 10 elements
	CHECK(mirage_write_fn(&msg, "List", -1, 10));

	// 1. i8
	CHECK(mirage_write_i8(&msg, 42));

	// 2. i16
	CHECK(mirage_write_i16(&msg, -1000));

	// 3. i32
	CHECK(mirage_write_i32(&msg, 100000));

	// 4. i64
	CHECK(mirage_write_i64(&msg, 9223372036854775807LL));

	// 5. f64
	CHECK(mirage_write_f64(&msg, 3.14159265358979));

	// 6. string
	CHECK(mirage_write_string(&msg, "Hello embedded!", -1));

	// 7. symbol
	CHECK(mirage_write_sym(&msg, "TestSymbol", -1));

	// 8. binary data
	uint8_t binary[] = {0xDE, 0xAD, 0xBE, 0xEF};
	CHECK(mirage_write_bin(&msg, binary, 4));

	// 9. nested list [1, 2, 3]
	CHECK(mirage_write_fn(&msg, "List", -1, 3));
	CHECK(mirage_write_i16(&msg, 1));
	CHECK(mirage_write_i16(&msg, 2));
	CHECK(mirage_write_i16(&msg, 3));

	// 10. association {"x": 10, "y": 20}
	CHECK(mirage_write_association(&msg, 2));
	CHECK(mirage_write_assoc_rule(&msg));
	CHECK(mirage_write_string(&msg, "x", -1));
	CHECK(mirage_write_i16(&msg, 10));
	CHECK(mirage_write_assoc_rule(&msg));
	CHECK(mirage_write_string(&msg, "y", -1));
	CHECK(mirage_write_i16(&msg, 20));

	printf("  Wrote %d bytes\n\n", msg.length);

	// ---- Read test data back ----
	printf("Reading test data back...\n");

	CHECK(mirage_read_start(&msg));

	// Read List header
	char name[32];
	int16_t nlen, argc;
	CHECK(mirage_read_fn(&msg, name, &nlen, 32, &argc));
	name[nlen] = '\0';
	ASSERT_EQ(argc, 10);
	printf("  Function: %s[%d args]\n", name, argc);

	// 1. i8
	int8_t val_i8;
	CHECK(mirage_read_i8(&msg, &val_i8));
	ASSERT_EQ(val_i8, 42);
	printf("  1. i8: %d ✓\n", val_i8);

	// 2. i16
	int16_t val_i16;
	CHECK(mirage_read_i16(&msg, &val_i16));
	ASSERT_EQ(val_i16, -1000);
	printf("  2. i16: %d ✓\n", val_i16);

	// 3. i32
	int32_t val_i32;
	CHECK(mirage_read_i32(&msg, &val_i32));
	ASSERT_EQ(val_i32, 100000);
	printf("  3. i32: %d ✓\n", val_i32);

	// 4. i64
	int64_t val_i64;
	CHECK(mirage_read_i64(&msg, &val_i64));
	ASSERT_EQ(val_i64, 9223372036854775807LL);
	printf("  4. i64: %lld ✓\n", (long long) val_i64);

	// 5. f64
	double val_f64;
	CHECK(mirage_read_f64(&msg, &val_f64));
	if(val_f64 < 3.14 || val_f64 > 3.15) {
		printf("FAIL: f64 = %f\n", val_f64);
		return 1;
	}
	printf("  5. f64: %.14f ✓\n", val_f64);

	// 6. string
	char str[64];
	int16_t slen;
	CHECK(mirage_read_string(&msg, str, &slen, 64));
	str[slen] = '\0';
	printf("  6. string: \"%s\" ✓\n", str);

	// 7. symbol
	char sym[32];
	int16_t symlen;
	CHECK(mirage_read_sym(&msg, sym, &symlen, 32));
	sym[symlen] = '\0';
	printf("  7. symbol: %s ✓\n", sym);

	// 8. binary
	uint8_t bin[8];
	int16_t binlen;
	CHECK(mirage_read_bin(&msg, bin, &binlen, 8));
	ASSERT_EQ(binlen, 4);
	ASSERT_EQ(bin[0], 0xDE);
	ASSERT_EQ(bin[3], 0xEF);
	printf("  8. binary: [%d bytes] ✓\n", binlen);

	// 9. nested list
	CHECK(mirage_read_fn(&msg, name, &nlen, 32, &argc));
	ASSERT_EQ(argc, 3);
	int16_t list_vals[3];
	CHECK(mirage_read_i16(&msg, &list_vals[0]));
	CHECK(mirage_read_i16(&msg, &list_vals[1]));
	CHECK(mirage_read_i16(&msg, &list_vals[2]));
	ASSERT_EQ(list_vals[0], 1);
	ASSERT_EQ(list_vals[1], 2);
	ASSERT_EQ(list_vals[2], 3);
	printf("  9. nested list: [%d, %d, %d] ✓\n",
	       list_vals[0], list_vals[1], list_vals[2]);

	// 10. association
	int16_t assoc_len;
	CHECK(mirage_read_association(&msg, &assoc_len));
	ASSERT_EQ(assoc_len, 2);
	CHECK(mirage_read_assoc_rule(&msg));
	CHECK(mirage_read_string(&msg, str, &slen, 64));
	CHECK(mirage_read_i16(&msg, &val_i16));
	CHECK(mirage_read_assoc_rule(&msg));
	CHECK(mirage_read_string(&msg, str, &slen, 64));
	CHECK(mirage_read_i16(&msg, &val_i16));
	printf("  10. association: {2 entries} ✓\n");

	// ---- Test peek ----
	printf("\nTesting peek...\n");
	mirage_reset(&msg);
	mirage_write_start(&msg);
	mirage_write_i8(&msg, 1);
	mirage_write_string(&msg, "test", -1);
	mirage_write_f64(&msg, 1.0);

	mirage_read_start(&msg);

	mirage_element_type etype;
	CHECK(mirage_peek_element_type(&msg, &etype));
	ASSERT_EQ(etype, MIRAGE_I8);
	printf("  Peek i8: ✓\n");

	mirage_read_i8(&msg, &val_i8);
	CHECK(mirage_peek_element_type(&msg, &etype));
	ASSERT_EQ(etype, MIRAGE_STRING);
	printf("  Peek string: ✓\n");

	mirage_read_string(&msg, str, &slen, 64);
	CHECK(mirage_peek_element_type(&msg, &etype));
	ASSERT_EQ(etype, MIRAGE_F64);
	printf("  Peek f64: ✓\n");

	// ---- Test numeric array ----
	printf("\nTesting numeric array...\n");
	mirage_reset(&msg);
	mirage_write_start(&msg);

	int32_t arr_data[] = {10, 20, 30, 40};
	int16_t dims[] = {4};
	CHECK(mirage_write_numericarray(&msg, MIRAGE_ARRAY_I32, 1, dims, arr_data));

	mirage_read_start(&msg);

	mirage_array_type atype;
	int16_t rank;
	int16_t out_dims[4];
	int32_t out_data[4];
	CHECK(mirage_read_numericarray(&msg, &atype, &rank, out_dims, 4, out_data, 16));
	ASSERT_EQ(atype, MIRAGE_ARRAY_I32);
	ASSERT_EQ(rank, 1);
	ASSERT_EQ(out_dims[0], 4);
	ASSERT_EQ(out_data[0], 10);
	ASSERT_EQ(out_data[3], 40);
	printf("  Array i32[4]: [%d, %d, %d, %d] ✓\n",
	       out_data[0], out_data[1], out_data[2], out_data[3]);

	printf("\n========================================\n");
	printf("All embedded tests passed!\n");
	printf("========================================\n");

	return 0;
}
