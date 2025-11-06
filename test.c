//  test.c – small demo that writes/reads a WXF function.
#include "mirage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

int main(void) {
	// ------ 1. Allocate message buffer (1024 bytes) ----------------------
	mirage_msg *msg = mirage_create(1024, malloc);
	if(!msg) {
		perror("mirage_create");
		return 1;
	}

	// ------ 2. Write message data  ---------------------------------------
	// "List" with 5 args:
	// 1. int8  7
	// 2. string "hello"
	// 3. int32 array {1,2,3}
	// 4. f32 array 72.0 (scalar)
	// 5. f64 42.0
	i64 err = mirage_write_start(msg); // Write message start
	err |= mirage_write_fn(msg, "List",
			       sizeof("List")-1, 5); // function header
	err |= mirage_write_i8(msg, 7);		   // First argument
	err |= mirage_write_string(msg, "Hello World!",
				   sizeof("Hello World!")-1); // Second argument

	// Third argument – numeric array (3 i32 values)
	i32 arr[3] = {1, 2, 3};
	i64 dim[1] = {3};
	err |= mirage_write_numericarray(msg,
					 MIRAGE_ARRAY_I32, // array type
					 1,		   // rank (1‑D)
					 dim,		   // dimensions
					 arr);		   // data pointer

	// Fourth argument – scalar (rank 0 array) with single float
	f32 scalar = 72.0f;
	err |= mirage_write_numericarray(msg,
					 MIRAGE_ARRAY_F32, // array type
					 0,		   // rank (1‑D), 0 is scalar
					 0,		   // dimensions, none
					 &scalar);	   // data pointer

	// Fifth argument - f64
	err |= mirage_write_f64(msg, 42.0);

	if(err < 0) {
		fprintf(stderr, "error writing data\n");
		return -err;
	}

	// ------ 3. Read the packet back.  ------------------------------------
	err |= mirage_read_start(msg); //rewind cursor
	if(err < 0) {
		fprintf(stderr, "mirage_read_start\n");
		return -err;
	}

	// Read function header
	char name[32];
	i64 nlen, argc;
	err |= mirage_read_fn(msg, name, &nlen, 32, &argc);

	// Read each argument in the same order
	i8 val8;
	err |= mirage_read_i8(msg, &val8);

	char buf[64];
	i64 blen;
	err |= mirage_read_string(msg, buf, &blen, sizeof(buf) - 1);

	mirage_array_type atype;
	i64 rank, dims[4];
	i32 out[10];
	err |= mirage_read_numericarray(msg, &atype, &rank, dims, 4, out,
					sizeof(out));

	mirage_array_type atype2;
	i64 rank2;
	f32 out2;
	err |= mirage_read_numericarray(msg, &atype2, &rank2, 0, 0, &out2,
					sizeof(out2));

	f64 out3;
	err |= mirage_read_f64(msg, &out3);

	if(err < 0) {
		fprintf(stderr, "error reading message data\n");
		return -err;
	}

	name[nlen] = '\0';
	printf("Function \"%s\" has %lld args\n", name, (long long) argc);
	buf[blen] = '\0';
	printf("  arg0 i8   = %d\n", (int) val8);
	printf("  arg1 str  = \"%s\"\n", buf);
	printf("  arg2 array = {%d, %d, %d}\n", out[0], out[1], out[2]);
	printf("  arg3 scalar = %f\n", out2);
	printf("  arg4 double = %f\n", out3);

	// Validate data
	assert(nlen == 4);
	assert(name[0] == 'L' && name[1] == 'i' && name[3] == 't');
	assert(blen == 12);
	assert(buf[0] == 'H' && buf[6] == 'W' && buf[11] == '!');
	assert(val8 == 7);
	assert(rank == 1 && dims[0] == 3 && atype == MIRAGE_ARRAY_I32 && out[2] == 3);
	assert(rank2 == 0 && atype2 == MIRAGE_ARRAY_F32 && out2 == 72.0f);
	assert(out3 == 42.0);

	// ------ 1. Test free -------------------------------------------------
	mirage_destroy(&msg, free);
	return 0;
}
