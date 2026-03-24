// test_write_py.c - Read and verify WXF data written by Python
// Reads test_write.py output and validates all types
#include "mirage.h"
#include "mirage_style.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                        \
	do {                                                               \
		if((expr) < 0) {                                           \
			fprintf(stderr, "FAIL at line %d: %s\n", __LINE__, \
				#expr);                                    \
			return 1;                                          \
		}                                                          \
	} while(0)

#define ASSERT_EQ(a, b)                                                \
	do {                                                           \
		if((a) != (b)) {                                       \
			fprintf(stderr, "FAIL at line %d: %s != %s\n", \
				__LINE__, #a, #b);                     \
			return 1;                                      \
		}                                                      \
	} while(0)

#define ASSERT_STR_EQ(a, b, len)                                              \
	do {                                                                  \
		if(strncmp(a, b, len) != 0) {                                 \
			fprintf(stderr, "FAIL at line %d: string mismatch\n", \
				__LINE__);                                    \
			return 1;                                             \
		}                                                             \
	} while(0)

int main(int argc, char *argv[]) {
	int rc = 1;
	FILE *f = NULL;
	unsigned char *data = NULL;
	mirage_msg *msg = NULL;

	if(argc != 2) {
		fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
		return 1;
	}

	// Read file
	f = fopen(argv[1], "rb");
	if(!f) {
		perror("fopen");
		goto cleanup;
	}
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if(size <= 0) {
		fprintf(stderr, "Invalid input size: %ld\n", size);
		goto cleanup;
	}

	data = malloc(size);
	if(!data) {
		fprintf(stderr, "malloc failed for %ld bytes\n", size);
		goto cleanup;
	}
	if(fread(data, 1, size, f) != (size_t) size) {
		fprintf(stderr, "Failed to read file\n");
		goto cleanup;
	}
	if(fclose(f) != 0) {
		perror("fclose");
		f = NULL;
		goto cleanup;
	}
	f = NULL;

	// Create mirage message from data
	msg = mirage_create(size, malloc);
	if(!msg) {
		fprintf(stderr, "mirage_create failed for %ld bytes\n", size);
		goto cleanup;
	}
	memcpy(msg->data, data, size);
	msg->length = size;
	free(data);
	data = NULL;

	// Read the WXF data
	CHECK(mirage_read_start(msg));

	// Outer structure is a List with 20 elements
	char name[64];
	i64 nlen, argc_out;
	CHECK(mirage_read_fn(msg, name, &nlen, sizeof(name), &argc_out));
	name[nlen] = '\0';
	ASSERT_STR_EQ(name, "List", 4);
	ASSERT_EQ(argc_out, 20);
	printf("Reading List with %lld elements\n", (long long) argc_out);

	// 1. i8 - 42
	i8 val_i8;
	CHECK(mirage_read_i8(msg, &val_i8));
	ASSERT_EQ(val_i8, 42);
	printf("  1. i8: %d ✓\n", val_i8);

	// 2. i16 - -1000
	i16 val_i16;
	CHECK(mirage_read_i16(msg, &val_i16));
	ASSERT_EQ(val_i16, -1000);
	printf("  2. i16: %d ✓\n", val_i16);

	// 3. i32 - 100000
	i32 val_i32;
	CHECK(mirage_read_i32(msg, &val_i32));
	ASSERT_EQ(val_i32, 100000);
	printf("  3. i32: %d ✓\n", val_i32);

	// 4. i64 - max i64
	i64 val_i64;
	CHECK(mirage_read_i64(msg, &val_i64));
	ASSERT_EQ(val_i64, 9223372036854775807LL);
	printf("  4. i64: %lld ✓\n", (long long) val_i64);

	// 5. f64 - pi
	f64 val_f64;
	CHECK(mirage_read_f64(msg, &val_f64));
	if(fabs(val_f64 - 3.141592653589793) > 1e-15) {
		fprintf(stderr, "FAIL: f64 mismatch\n");
		return 1;
	}
	printf("  5. f64: %.15f ✓\n", val_f64);

	// 6. string - "Hello, Mirage!"
	char str[64];
	i64 slen;
	CHECK(mirage_read_string(msg, str, &slen, sizeof(str)));
	str[slen] = '\0';
	ASSERT_STR_EQ(str, "Hello, Mirage!", 14);
	printf("  6. string: \"%s\" ✓\n", str);

	// 7. bytes - binary data
	u8 bin[16];
	i64 blen;
	CHECK(mirage_read_bin(msg, bin, &blen, sizeof(bin)));
	ASSERT_EQ(blen, 7);
	ASSERT_EQ(bin[0], 0x00);
	ASSERT_EQ(bin[1], 0x01);
	ASSERT_EQ(bin[4], 0xff);
	ASSERT_EQ(bin[6], 0xfd);
	printf("  7. bytes: [%d bytes] ✓\n", (int) blen);

	// 8. list [1, 2, 3] -> List function
	CHECK(mirage_read_fn(msg, name, &nlen, sizeof(name), &argc_out));
	name[nlen] = '\0';
	ASSERT_STR_EQ(name, "List", 4);
	ASSERT_EQ(argc_out, 3);
	i64 list_vals[3];
	CHECK(mirage_read_i64(msg, &list_vals[0]));
	CHECK(mirage_read_i64(msg, &list_vals[1]));
	CHECK(mirage_read_i64(msg, &list_vals[2]));
	ASSERT_EQ(list_vals[0], 1);
	ASSERT_EQ(list_vals[1], 2);
	ASSERT_EQ(list_vals[2], 3);
	printf("  8. list: [%lld, %lld, %lld] ✓\n", (long long) list_vals[0],
	       (long long) list_vals[1], (long long) list_vals[2]);

	// 9. tuple ('Plus', 10, 20) -> Plus function
	CHECK(mirage_read_fn(msg, name, &nlen, sizeof(name), &argc_out));
	name[nlen] = '\0';
	ASSERT_STR_EQ(name, "Plus", 4);
	ASSERT_EQ(argc_out, 2);
	i64 plus_args[2];
	CHECK(mirage_read_i64(msg, &plus_args[0]));
	CHECK(mirage_read_i64(msg, &plus_args[1]));
	ASSERT_EQ(plus_args[0], 10);
	ASSERT_EQ(plus_args[1], 20);
	printf("  9. function Plus[%lld, %lld] ✓\n", (long long) plus_args[0],
	       (long long) plus_args[1]);

	// 10. dict {"x": 1.5, "y": 2.5} -> Association
	i64 assoc_len;
	CHECK(mirage_read_association(msg, &assoc_len));
	ASSERT_EQ(assoc_len, 2);
	// Read x -> 1.5
	CHECK(mirage_read_assoc_rule(msg));
	CHECK(mirage_read_string(msg, str, &slen, sizeof(str)));
	f64 dict_val;
	CHECK(mirage_read_f64(msg, &dict_val));
	// Read y -> 2.5
	CHECK(mirage_read_assoc_rule(msg));
	CHECK(mirage_read_string(msg, str, &slen, sizeof(str)));
	CHECK(mirage_read_f64(msg, &dict_val));
	printf("  10. dict: {2 entries} ✓\n");

	// 11. symbol "Global`mySymbol"
	char sym[64];
	i64 symlen;
	CHECK(mirage_read_sym(msg, sym, &symlen, sizeof(sym)));
	sym[symlen] = '\0';
	ASSERT_STR_EQ(sym, "Global`mySymbol", 15);
	printf("  11. symbol: %s ✓\n", sym);

	// 12. None -> Symbol "Null"
	CHECK(mirage_read_sym(msg, sym, &symlen, sizeof(sym)));
	sym[symlen] = '\0';
	ASSERT_STR_EQ(sym, "Null", 4);
	printf("  12. None: Null ✓\n");

	// 13. True -> i8 1
	CHECK(mirage_read_i8(msg, &val_i8));
	ASSERT_EQ(val_i8, 1);
	printf("  13. True: %d ✓\n", val_i8);

	// 14. False -> i8 0
	CHECK(mirage_read_i8(msg, &val_i8));
	ASSERT_EQ(val_i8, 0);
	printf("  14. False: %d ✓\n", val_i8);

	// 15. nested dict
	CHECK(mirage_read_association(msg, &assoc_len));
	ASSERT_EQ(assoc_len, 3);
	// "name" -> "test"
	CHECK(mirage_read_assoc_rule(msg));
	CHECK(mirage_read_string(msg, str, &slen, sizeof(str)));
	ASSERT_STR_EQ(str, "name", 4);
	CHECK(mirage_read_string(msg, str, &slen, sizeof(str)));
	ASSERT_STR_EQ(str, "test", 4);
	// "values" -> [1, 2, 3]
	CHECK(mirage_read_assoc_rule(msg));
	CHECK(mirage_read_string(msg, str, &slen, sizeof(str)));
	ASSERT_STR_EQ(str, "values", 6);
	CHECK(mirage_read_fn(msg, name, &nlen, sizeof(name), &argc_out));
	ASSERT_EQ(argc_out, 3);
	CHECK(mirage_read_i64(msg, &val_i64));
	CHECK(mirage_read_i64(msg, &val_i64));
	CHECK(mirage_read_i64(msg, &val_i64));
	// "nested" -> Fn["arg1", "arg2"]
	CHECK(mirage_read_assoc_rule(msg));
	CHECK(mirage_read_string(msg, str, &slen, sizeof(str)));
	ASSERT_STR_EQ(str, "nested", 6);
	CHECK(mirage_read_fn(msg, name, &nlen, sizeof(name), &argc_out));
	name[nlen] = '\0';
	ASSERT_STR_EQ(name, "Fn", 2);
	ASSERT_EQ(argc_out, 2);
	CHECK(mirage_read_string(msg, str, &slen, sizeof(str)));
	CHECK(mirage_read_string(msg, str, &slen, sizeof(str)));
	printf("  15. nested dict: ✓\n");

	// 16. numpy int64 array [10, 20, 30, 40]
	mirage_array_type atype;
	i64 rank, dims[4];
	i64 arr_i64[4];
	CHECK(mirage_read_numericarray(msg, &atype, &rank, dims, 4, arr_i64,
				       sizeof(arr_i64)));
	ASSERT_EQ(atype, MIRAGE_ARRAY_I64);
	ASSERT_EQ(rank, 1);
	ASSERT_EQ(dims[0], 4);
	ASSERT_EQ(arr_i64[0], 10);
	ASSERT_EQ(arr_i64[3], 40);
	printf("  16. numpy int64 array: [%lld, %lld, %lld, %lld] ✓\n",
	       (long long) arr_i64[0], (long long) arr_i64[1],
	       (long long) arr_i64[2], (long long) arr_i64[3]);

	// 17. numpy float64 2D array [[1,2],[3,4]]
	f64 arr_f64[4];
	CHECK(mirage_read_numericarray(msg, &atype, &rank, dims, 4, arr_f64,
				       sizeof(arr_f64)));
	ASSERT_EQ(atype, MIRAGE_ARRAY_F64);
	ASSERT_EQ(rank, 2);
	ASSERT_EQ(dims[0], 2);
	ASSERT_EQ(dims[1], 2);
	ASSERT_EQ(arr_f64[0], 1.0);
	ASSERT_EQ(arr_f64[3], 4.0);
	printf("  17. numpy float64 2D array: [[%.1f,%.1f],[%.1f,%.1f]] ✓\n",
	       arr_f64[0], arr_f64[1], arr_f64[2], arr_f64[3]);

	// 18. BigInt - 10^30
	char bigint[64];
	i64 biglen;
	CHECK(mirage_read_bigint(msg, bigint, &biglen, sizeof(bigint)));
	bigint[biglen] = '\0';
	ASSERT_STR_EQ(bigint, "1000000000000000000000000000000", 31);
	printf("  18. BigInt: %s ✓\n", bigint);

	// 19. Empty list []
	CHECK(mirage_read_fn(msg, name, &nlen, sizeof(name), &argc_out));
	name[nlen] = '\0';
	ASSERT_STR_EQ(name, "List", 4);
	ASSERT_EQ(argc_out, 0);
	printf("  19. Empty list: [] ✓\n");

	// 20. Empty dict {}
	CHECK(mirage_read_association(msg, &assoc_len));
	ASSERT_EQ(assoc_len, 0);
	printf("  20. Empty dict: {} ✓\n");

	printf("\nAll Python->C tests passed!\n");
	rc = 0;

cleanup:
	if(f) {
		int close_rc = fclose(f);
		(void) close_rc;
	}
	free(data);
	if(msg) {
		mirage_destroy(&msg, free);
	}
	return rc;
}
