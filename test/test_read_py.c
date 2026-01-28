// test_read_py.c - Write WXF data from C for Python to verify
// Writes various types that Python will read and validate
#include "mirage.h"
#include "mirage_style.h"
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

int main(int argc, char *argv[]) {
	if(argc != 2) {
		fprintf(stderr, "Usage: %s <output_file>\n", argv[0]);
		return 1;
	}

	// Create message buffer
	mirage_msg *msg = mirage_create(8192, malloc);
	if(!msg) {
		perror("mirage_create");
		return 1;
	}

	CHECK(mirage_write_start(msg));

	// Write a List with 20 elements (same structure as Python test)
	CHECK(mirage_write_fn(msg, "List", -1, 20));

	// 1. i8 - 42
	CHECK(mirage_write_i8(msg, 42));

	// 2. i16 - -1000
	CHECK(mirage_write_i16(msg, -1000));

	// 3. i32 - 100000
	CHECK(mirage_write_i32(msg, 100000));

	// 4. i64 - max i64
	CHECK(mirage_write_i64(msg, 9223372036854775807LL));

	// 5. f64 - pi
	CHECK(mirage_write_f64(msg, 3.141592653589793));

	// 6. string - "Hello from C!"
	CHECK(mirage_write_string(msg, "Hello from C!", -1));

	// 7. binary data
	u8 binary[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0xFF};
	CHECK(mirage_write_bin(msg, binary, 6));

	// 8. List [100, 200, 300]
	CHECK(mirage_write_fn(msg, "List", -1, 3));
	CHECK(mirage_write_i64(msg, 100));
	CHECK(mirage_write_i64(msg, 200));
	CHECK(mirage_write_i64(msg, 300));

	// 9. Custom function Multiply[5, 7]
	CHECK(mirage_write_fn(msg, "Multiply", -1, 2));
	CHECK(mirage_write_i64(msg, 5));
	CHECK(mirage_write_i64(msg, 7));

	// 10. Association {"a": 10, "b": 20}
	CHECK(mirage_write_association(msg, 2));
	CHECK(mirage_write_assoc_rule(msg));
	CHECK(mirage_write_string(msg, "a", -1));
	CHECK(mirage_write_i64(msg, 10));
	CHECK(mirage_write_assoc_rule(msg));
	CHECK(mirage_write_string(msg, "b", -1));
	CHECK(mirage_write_i64(msg, 20));

	// 11. Symbol "TestSymbol"
	CHECK(mirage_write_sym(msg, "TestSymbol", -1));

	// 12. Symbol "Null" (Python None)
	CHECK(mirage_write_sym(msg, "Null", -1));

	// 13. Symbol "True" (Python True)
	CHECK(mirage_write_sym(msg, "True", -1));

	// 14. Symbol "False" (Python False)
	CHECK(mirage_write_sym(msg, "False", -1));

	// 15. Nested structure
	CHECK(mirage_write_association(msg, 2));
	CHECK(mirage_write_assoc_rule(msg));
	CHECK(mirage_write_string(msg, "inner_list", -1));
	CHECK(mirage_write_fn(msg, "List", -1, 2));
	CHECK(mirage_write_i64(msg, 1));
	CHECK(mirage_write_i64(msg, 2));
	CHECK(mirage_write_assoc_rule(msg));
	CHECK(mirage_write_string(msg, "inner_fn", -1));
	CHECK(mirage_write_fn(msg, "Add", -1, 2));
	CHECK(mirage_write_f64(msg, 1.5));
	CHECK(mirage_write_f64(msg, 2.5));

	// 16. NumericArray i32 [1, 2, 3, 4, 5]
	i32 arr_i32[] = {1, 2, 3, 4, 5};
	i64 dims_1d[] = {5};
	CHECK(mirage_write_numericarray(msg, MIRAGE_ARRAY_I32, 1, dims_1d,
					arr_i32));

	// 17. NumericArray f64 2D [[10, 20], [30, 40]]
	f64 arr_f64[] = {10.0, 20.0, 30.0, 40.0};
	i64 dims_2d[] = {2, 2};
	CHECK(mirage_write_numericarray(msg, MIRAGE_ARRAY_F64, 2, dims_2d,
					arr_f64));

	// 18. BigInt
	CHECK(mirage_write_bigint(msg, "-9999999999999999999999999999999", -1));

	// 19. Empty List
	CHECK(mirage_write_fn(msg, "List", -1, 0));

	// 20. Empty Association
	CHECK(mirage_write_association(msg, 0));

	// Get buffer and write to file
	i64 len = msg->length;
	u8 *buf = msg->data;

	FILE *f = fopen(argv[1], "wb");
	if(!f) {
		perror("fopen");
		return 1;
	}
	if(fwrite(buf, 1, len, f) != (size_t) len) {
		fprintf(stderr, "Failed to write file\n");
		return 1;
	}
	fclose(f);

	mirage_destroy(&msg, free);
	printf("Wrote %lld bytes to %s\n", (long long) len, argv[1]);
	return 0;
}
