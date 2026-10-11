/* test_all.c - comprehensive Mirage test (with Association) */
#include "mirage.h"
#include "mirage_style.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

	REJECT_WRITE(mirage_write_bin(&msg, data, INT64_MAX), MIRAGE_ERR_BUFFERSIZE);
	REJECT_WRITE(mirage_write_string(&msg, "x", INT64_MAX), MIRAGE_ERR_BUFFERSIZE);
	REJECT_WRITE(mirage_write_sym(&msg, "x", INT64_MAX), MIRAGE_ERR_BUFFERSIZE);
	REJECT_WRITE(mirage_write_bigint(&msg, "1", INT64_MAX), MIRAGE_ERR_BUFFERSIZE);
	REJECT_WRITE(mirage_write_bigreal(&msg, "1", INT64_MAX), MIRAGE_ERR_BUFFERSIZE);
	REJECT_WRITE(mirage_write_fn(&msg, "f", INT64_MAX, 0), MIRAGE_ERR_BUFFERSIZE);

	int64_t dims[40] = {INT64_MAX};
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
		dims[i] = INT64_MAX;
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

	/* Reject a full logical buffer without overflowing cursor + write size. */
	msg.max_length = msg.length = msg.cursor = INT64_MAX;
	assert(mirage_write_i8(&msg, 1) == MIRAGE_ERR_BUFFERSIZE);
	assert(mirage_write_i16(&msg, 1) == MIRAGE_ERR_BUFFERSIZE);
	assert(mirage_write_i32(&msg, 1) == MIRAGE_ERR_BUFFERSIZE);
	assert(mirage_write_i64(&msg, 1) == MIRAGE_ERR_BUFFERSIZE);
	assert(mirage_write_f64(&msg, 1) == MIRAGE_ERR_BUFFERSIZE);
	assert(mirage_write_assoc_rule(&msg) == MIRAGE_ERR_BUFFERSIZE);
	assert(msg.cursor == INT64_MAX);

#undef REJECT_WRITE
}

int main(void) {
	test_writer_bounds();
	/* ------------------------------------------------------------------ */
	/* 1. Allocate a message buffer (4096 bytes)                           */
	/* ------------------------------------------------------------------ */
	mirage_msg *msg = mirage_create(4096, malloc);
	if(!msg) {
		perror("mirage_create");
		return 1;
	}

	/* ------------------------------------------------------------------ */
	/* 2. Write the message                                               */
	/* ------------------------------------------------------------------ */
	i64 err = 0;
	err |= mirage_write_start(msg);

	/* Function header - 15 arguments */
	char *func_name = "AllTypes";
	err |= mirage_write_fn(msg, func_name, (i64) strlen(func_name), 15);

	/* Argument 0 - i8 */
	err |= mirage_write_i8(msg, 7);

	/* Argument 1 - i16 */
	err |= mirage_write_i16(msg, 12345);

	/* Argument 2 - i32 */
	err |= mirage_write_i32(msg, -123456);

	/* Argument 3 - i64 */
	err |= mirage_write_i64(msg, 987654321012345678LL);

	/* Argument 4 - f64 */
	err |= mirage_write_f64(msg, 3.141592653589793);

	/* Argument 5 - string */
	char *str_val = "Hello WXF";
	err |= mirage_write_string(msg, str_val, (i64) strlen(str_val));

	/* Argument 6 - binary data */
	u8 bin_data[4] = {1, 2, 3, 4};
	err |= mirage_write_bin(msg, bin_data, sizeof(bin_data));

	/* Argument 7 - symbol */
	char *sym_val = "mySymbol";
	err |= mirage_write_sym(msg, sym_val, (i64) strlen(sym_val));

	/* Argument 8 - bigint */
	char *bigint_val = "123456789012345678901234567890";
	err |= mirage_write_bigint(msg, bigint_val, (i64) strlen(bigint_val));

	/* Argument 9 - bigreal */
	char *bigreal_val = "1.2345678901234567890123456789E+100";
	err |= mirage_write_bigreal(msg, bigreal_val, (i64) strlen(bigreal_val));

	/* Argument 10 - numeric array (i32) */
	i32 arr_i32[3] = {1, 2, 3};
	i64 dims_i32[1] = {3};
	err |= mirage_write_numericarray(msg, MIRAGE_ARRAY_I32,
					 1, dims_i32, arr_i32);

	/* Argument 11 - numeric scalar (f32) */
	f32 scalar_f32 = 72.0f;
	err |= mirage_write_numericarray(msg, MIRAGE_ARRAY_F32,
					 0, NULL, &scalar_f32);

	/* Argument 12 - numeric array (u8) */
	u8 arr_u8[4] = {10, 20, 30, 40};
	i64 dims_u8[1] = {4};
	err |= mirage_write_numericarray(msg, MIRAGE_ARRAY_U8,
					 1, dims_u8, arr_u8);

	/* Argument 13 - numeric array (f32) */
	f32 arr_f32[2] = {1.5f, 2.5f};
	i64 dims_f32[1] = {2};
	err |= mirage_write_numericarray(msg, MIRAGE_ARRAY_F32,
					 1, dims_f32, arr_f32);

	/* Argument 14 - association (2 key/value pairs) */
	err |= mirage_write_association(msg, 2); /* 2 pairs */

	/* Pair 1: key "foo" (string) -> value i32 42 */
	err |= mirage_write_assoc_rule(msg);
	err |= mirage_write_string(msg, "foo", 3);
	err |= mirage_write_i32(msg, 42);

	/* Pair 2: key "bar" (symbol) -> value f64 3.14 */
	err |= mirage_write_assoc_rule(msg);
	err |= mirage_write_sym(msg, "bar", 3);
	err |= mirage_write_f64(msg, 3.14);

	if(err < 0) {
		fprintf(stderr, "error writing data\n");
		mirage_destroy(&msg, free);
		return 1;
	}

	/* ------------------------------------------------------------------ */
	/* 3. Read the packet back                                           */
	/* ------------------------------------------------------------------ */
	err = 0;
	err |= mirage_read_start(msg);
	if(err < 0) {
		fprintf(stderr, "mirage_read_start failed: %lld\n", (long long) err);
		mirage_destroy(&msg, free);
		return 1;
	}

	/* Function header */
	char name[32];
	i64 nlen, argc;
	err |= mirage_read_fn(msg, name, &nlen, sizeof(name), &argc);
	assert(nlen == (i64) strlen(func_name));
	assert(argc == 15);
	name[nlen] = '\0';
	printf("Function \"%s\" has %lld args\n", name, (long long) argc);

	/* Argument 0 - i8 */
	i8 val8;
	err |= mirage_read_i8(msg, &val8);
	assert(val8 == 7);

	/* Argument 1 - i16 */
	i16 val16;
	err |= mirage_read_i16(msg, &val16);
	assert(val16 == 12345);

	/* Argument 2 - i32 */
	i32 val32;
	err |= mirage_read_i32(msg, &val32);
	assert(val32 == -123456);

	/* Argument 3 - i64 */
	i64 val64;
	err |= mirage_read_i64(msg, &val64);
	assert(val64 == 987654321012345678LL);

	/* Argument 4 - f64 */
	f64 valf64;
	err |= mirage_read_f64(msg, &valf64);
	assert(valf64 == 3.141592653589793);

	/* Argument 5 - string */
	char buf[64];
	i64 blen;
	err |= mirage_read_string(msg, buf, &blen, sizeof(buf));
	assert(blen == (i64) strlen(str_val));
	buf[blen] = '\0';
	assert(strcmp(buf, str_val) == 0);

	/* Argument 6 - binary data */
	u8 bin_read[8];
	i64 bin_len;
	err |= mirage_read_bin(msg, bin_read, &bin_len, sizeof(bin_read));
	assert(bin_len == (i64) sizeof(bin_data));
	assert(memcmp(bin_read, bin_data, bin_len) == 0);

	/* Argument 7 - symbol */
	char sym_buf[32];
	i64 sym_len;
	err |= mirage_read_sym(msg, sym_buf, &sym_len, sizeof(sym_buf));
	assert(sym_len == (i64) strlen(sym_val));
	sym_buf[sym_len] = '\0';
	assert(strcmp(sym_buf, sym_val) == 0);

	/* Argument 8 - bigint */
	char bigint_buf[64];
	i64 bigint_len;
	err |= mirage_read_bigint(msg, bigint_buf, &bigint_len, sizeof(bigint_buf));
	assert(bigint_len == (i64) strlen(bigint_val));
	bigint_buf[bigint_len] = '\0';
	assert(strcmp(bigint_buf, bigint_val) == 0);

	/* Argument 9 - bigreal */
	char bigreal_buf[128];
	i64 bigreal_len;
	err |= mirage_read_bigreal(msg, bigreal_buf, &bigreal_len, sizeof(bigreal_buf));
	assert(bigreal_len == (i64) strlen(bigreal_val));
	bigreal_buf[bigreal_len] = '\0';
	assert(strcmp(bigreal_buf, bigreal_val) == 0);

	/* Argument 10 - numeric array (i32) */
	mirage_array_type atype;
	i64 rank, dims[4];
	i32 out_i32[10];
	err |= mirage_read_numericarray(msg, &atype, &rank, dims, 4,
					out_i32, sizeof(out_i32));
	assert(atype == MIRAGE_ARRAY_I32);
	assert(rank == 1 && dims[0] == 3);
	assert(out_i32[0] == 1 && out_i32[1] == 2 && out_i32[2] == 3);

	/* Argument 11 - numeric scalar (f32) */
	f32 out_f32;
	err |= mirage_read_numericarray(msg, &atype, &rank, NULL, 0, &out_f32, sizeof(out_f32));
	assert(atype == MIRAGE_ARRAY_F32);
	assert(rank == 0 && out_f32 == 72.0f);

	/* Argument 12 - numeric array (u8) */
	u8 out_u8[10];
	err |= mirage_read_numericarray(msg, &atype, &rank, dims, 4,
					out_u8, sizeof(out_u8));
	assert(atype == MIRAGE_ARRAY_U8);
	assert(rank == 1 && dims[0] == 4);
	assert(out_u8[0] == 10 && out_u8[1] == 20 && out_u8[2] == 30 && out_u8[3] == 40);

	/* Argument 13 - numeric array (f32) */
	f32 out_f32_arr[10];
	err |= mirage_read_numericarray(msg, &atype, &rank, dims, 4,
					out_f32_arr, sizeof(out_f32_arr));
	assert(atype == MIRAGE_ARRAY_F32);
	assert(rank == 1 && dims[0] == 2);
	assert(out_f32_arr[0] == 1.5f && out_f32_arr[1] == 2.5f);

	/* Argument 14 - association */
	i64 assoc_len;
	err |= mirage_read_association(msg, &assoc_len);
	assert(assoc_len == 2);

	/* Pair 1 */
	mirage_element_type key_type;
	char key_buf1[32];
	i64 key_len1;
	i32 val_i32;
	err |= mirage_read_assoc_rule(msg); /* consumes the "->" head */
	err |= mirage_peek_element_type(msg, &key_type);
	assert(key_type == MIRAGE_STRING);
	err |= mirage_read_string(msg, key_buf1, &key_len1, sizeof(key_buf1));
	assert(key_len1 == 3 && strncmp(key_buf1, "foo", 3) == 0);
	err |= mirage_peek_element_type(msg, &key_type);
	assert(key_type == MIRAGE_I32);
	err |= mirage_read_i32(msg, &val_i32);
	assert(val_i32 == 42);

	/* Pair 2 */
	char key_buf2[32];
	i64 key_len2;
	f64 val_f64_assoc;
	err |= mirage_read_assoc_rule(msg);
	err |= mirage_peek_element_type(msg, &key_type);
	assert(key_type == MIRAGE_SYMBOL);
	err |= mirage_read_sym(msg, key_buf2, &key_len2, sizeof(key_buf2));
	assert(key_len2 == 3 && strncmp(key_buf2, "bar", 3) == 0);
	err |= mirage_peek_element_type(msg, &key_type);
	assert(key_type == MIRAGE_F64);
	err |= mirage_read_f64(msg, &val_f64_assoc);
	assert(val_f64_assoc == 3.14);

	if(err < 0) {
		fprintf(stderr, "error reading message data\n");
		mirage_destroy(&msg, free);
		return 1;
	}


	/* ------------------------------------------------------------------ */
	/* 4. Clean up                                                        */
	/* ------------------------------------------------------------------ */
	mirage_destroy(&msg, free);
	printf("All tests passed.\n");
	return 0;
}
