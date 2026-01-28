// test_embedded.c - Test the embedded mirage implementation
// Uses stack-allocated buffer (no malloc) and int16_t lengths

#include "mirage_embedded.h"
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

int main(void) {
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
