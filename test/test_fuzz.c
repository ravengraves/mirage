// test_fuzz.c - invalid input coverage for Mirage decoding
#include "mirage.h"
#include "mirage_style.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_fuzz_message.h"
#include "test_numericarray.h"
#include "test_decode_contracts.h"

typedef struct {
	u8 *data;
	size_t len;
	size_t cap;
} wbuf;

static void buf_init(wbuf *buf, u8 *storage, size_t cap) {
	buf->data = storage;
	buf->len = 0;
	buf->cap = cap;
}

static void buf_push(wbuf *buf, u8 v) {
	if(buf->len >= buf->cap) {
		fprintf(stderr, "buffer overflow\n");
		exit(1);
	}
	buf->data[buf->len++] = v;
}

static void buf_write_bytes(wbuf *buf, const void *src, size_t len) {
	if(buf->len + len > buf->cap) {
		fprintf(stderr, "buffer overflow\n");
		exit(1);
	}
	memcpy(buf->data + buf->len, src, len);
	buf->len += len;
}

static void buf_write_varu(wbuf *buf, u64 v) {
	while(1) {
		u8 byte = (u8) (v & 0x7F);
		v >>= 7;
		if(v) {
			byte |= 0x80;
		}
		buf_push(buf, byte);
		if(!v) {
			break;
		}
	}
}

static mirage_msg *msg_from_bytes(const u8 *data, size_t len) {
	size_t cap = len > 2 ? len : 2;
	mirage_msg *msg = mirage_create((i64) cap, malloc);
	if(!msg) {
		fprintf(stderr, "mirage_create failed\n");
		exit(1);
	}
	memcpy(msg->data, data, len);
	msg->length = (i64) len;
	msg->cursor = 0;
	return msg;
}

static void require_error(const char *label, i64 rc) {
	if(rc >= 0) {
		fprintf(stderr, "%s: expected error, got %lld\n", label, (long long) rc);
		exit(1);
	}
}

static void require_ok(const char *label, i64 rc) {
	if(rc < 0) {
		fprintf(stderr, "%s: unexpected error %lld\n", label, (long long) rc);
		exit(1);
	}
}

static void expect_start_error(const char *label, const u8 *data, size_t len) {
	mirage_msg *msg = msg_from_bytes(data, len);
	i64 rc = mirage_read_start(msg);
	require_error(label, rc);
	mirage_destroy(&msg, free);
}

static void expect_string_error(const char *label, const u8 *data, size_t len) {
	mirage_msg *msg = msg_from_bytes(data, len);
	i64 rc = mirage_read_start(msg);
	require_ok(label, rc);
	char out[32];
	i64 out_len = 0;
	rc = mirage_read_string(msg, out, &out_len, (i64) sizeof(out));
	require_error(label, rc);
	mirage_destroy(&msg, free);
}

static void expect_bin_error(const char *label, const u8 *data, size_t len, i64 max) {
	mirage_msg *msg = msg_from_bytes(data, len);
	i64 rc = mirage_read_start(msg);
	require_ok(label, rc);
	u8 out[32];
	i64 out_len = 0;
	rc = mirage_read_bin(msg, out, &out_len, max);
	require_error(label, rc);
	mirage_destroy(&msg, free);
}

static void expect_function_error(const char *label, const u8 *data, size_t len, i64 nmax) {
	mirage_msg *msg = msg_from_bytes(data, len);
	i64 rc = mirage_read_start(msg);
	require_ok(label, rc);
	char name[32];
	i64 name_len = 0;
	i64 argc = 0;
	rc = mirage_read_fn(msg, name, &name_len, nmax, &argc);
	require_error(label, rc);
	mirage_destroy(&msg, free);
}

static void expect_assoc_error(const char *label, const u8 *data, size_t len) {
	mirage_msg *msg = msg_from_bytes(data, len);
	i64 rc = mirage_read_start(msg);
	require_ok(label, rc);
	i64 alen = 0;
	rc = mirage_read_association(msg, &alen);
	require_error(label, rc);
	mirage_destroy(&msg, free);
}

static void expect_rule_error(const char *label, const u8 *data, size_t len) {
	mirage_msg *msg = msg_from_bytes(data, len);
	i64 rc = mirage_read_start(msg);
	require_ok(label, rc);
	rc = mirage_read_assoc_rule(msg);
	require_error(label, rc);
	mirage_destroy(&msg, free);
}

static void expect_numeric_error(const char *label, const u8 *data, size_t len,
				 i64 max_rank, i64 max_bytes) {
	mirage_msg *msg = msg_from_bytes(data, len);
	i64 rc = mirage_read_start(msg);
	require_ok(label, rc);
	mirage_array_type t = MIRAGE_ARRAY_U8;
	i64 rank = 0;
	i64 dims[8];
	u8 out[128];
	rc = mirage_read_numericarray(msg, &t, &rank, dims, max_rank, out, max_bytes);
	require_error(label, rc);
	mirage_destroy(&msg, free);
}

static void expect_numeric_error_skip(const char *label, const u8 *data, size_t len,
				      i64 max_rank) {
	mirage_msg *msg = msg_from_bytes(data, len);
	i64 rc = mirage_read_start(msg);
	require_ok(label, rc);
	mirage_array_type t = MIRAGE_ARRAY_U8;
	i64 rank = 0;
	i64 dims[8];
	rc = mirage_read_numericarray(msg, &t, &rank, dims, max_rank,
				      NULL, MIRAGE_SKIP_DATA);
	require_error(label, rc);
	mirage_destroy(&msg, free);
}

static void expect_numeric_error_null_data(const char *label, const u8 *data, size_t len,
					   i64 max_rank, i64 max_bytes) {
	mirage_msg *msg = msg_from_bytes(data, len);
	i64 rc = mirage_read_start(msg);
	require_ok(label, rc);
	mirage_array_type t = MIRAGE_ARRAY_U8;
	i64 rank = 0;
	i64 dims[8];
	rc = mirage_read_numericarray(msg, &t, &rank, dims, max_rank, NULL, max_bytes);
	require_error(label, rc);
	mirage_destroy(&msg, free);
}

static void expect_numeric_error_null_dims(const char *label, const u8 *data, size_t len,
					   i64 max_rank, i64 max_bytes) {
	mirage_msg *msg = msg_from_bytes(data, len);
	i64 rc = mirage_read_start(msg);
	require_ok(label, rc);
	mirage_array_type t = MIRAGE_ARRAY_U8;
	i64 rank = 0;
	u8 out[32];
	rc = mirage_read_numericarray(msg, &t, &rank, NULL, max_rank, out, max_bytes);
	require_error(label, rc);
	mirage_destroy(&msg, free);
}

static void expect_numeric_ok_skip(const char *label, const u8 *data, size_t len,
				   i64 max_rank) {
	mirage_msg *msg = msg_from_bytes(data, len);
	i64 rc = mirage_read_start(msg);
	require_ok(label, rc);
	mirage_array_type t = MIRAGE_ARRAY_U8;
	i64 rank = 0;
	i64 dims[8];
	rc = mirage_read_numericarray(msg, &t, &rank, dims, max_rank,
				      NULL, MIRAGE_SKIP_DATA);
	require_ok(label, rc);
	if(msg->cursor <= 2) {
		fprintf(stderr, "%s: cursor did not advance\n", label);
		exit(1);
	}
	mirage_destroy(&msg, free);
}

static void build_string_packet(wbuf *buf, const char *text) {
	buf_push(buf, '8');
	buf_push(buf, ':');
	buf_push(buf, 'S');
	buf_write_varu(buf, (u64) strlen(text));
	buf_write_bytes(buf, text, strlen(text));
}

static void build_function_packet(wbuf *buf, const char *name, u64 argc) {
	buf_push(buf, '8');
	buf_push(buf, ':');
	buf_push(buf, 'f');
	buf_write_varu(buf, argc);
	buf_push(buf, 's');
	buf_write_varu(buf, (u64) strlen(name));
	buf_write_bytes(buf, name, strlen(name));
}

static void build_association_packet(wbuf *buf, u64 len) {
	buf_push(buf, '8');
	buf_push(buf, ':');
	buf_push(buf, 'A');
	buf_write_varu(buf, len);
}

static void build_numeric_packet(wbuf *buf, u8 tag, u8 enc, u64 rank,
				 const u64 *dims, size_t dims_len,
				 const u8 *data, size_t data_len) {
	buf_push(buf, '8');
	buf_push(buf, ':');
	buf_push(buf, tag);
	buf_push(buf, enc);
	buf_write_varu(buf, rank);
	for(size_t i = 0; i < dims_len; ++i) {
		buf_write_varu(buf, dims[i]);
	}
	if(data && data_len) {
		buf_write_bytes(buf, data, data_len);
	}
}

int main(void) {
	test_numericarray();
	test_decode_contracts();
	test_message_null_data_read();
	test_message_null_data_write();
	test_invalid_message_states();
	test_message_init_and_boundaries();
	u8 storage[256];
	wbuf buf;

	buf_init(&buf, storage, sizeof(storage));
	buf_push(&buf, '8');
	expect_start_error("short_header", buf.data, buf.len);

	buf_init(&buf, storage, sizeof(storage));
	buf_write_bytes(&buf, "7:hello", 7);
	expect_start_error("invalid_header", buf.data, buf.len);

	buf_init(&buf, storage, sizeof(storage));
	buf_write_bytes(&buf, "8:Z", 3);
	mirage_msg *msg = msg_from_bytes(buf.data, buf.len);
	i64 rc = mirage_read_start(msg);
	require_ok("unknown_tag_start", rc);
	mirage_element_type et = MIRAGE_FUNCTION;
	rc = mirage_peek_element_type(msg, &et);
	require_error("unknown_tag", rc);
	mirage_destroy(&msg, free);

	buf_init(&buf, storage, sizeof(storage));
	build_string_packet(&buf, "hello");
	buf.len -= 1; // truncate payload
	expect_string_error("truncated_string", buf.data, buf.len);

	buf_init(&buf, storage, sizeof(storage));
	buf_push(&buf, '8');
	buf_push(&buf, ':');
	buf_push(&buf, 'S');
	buf_write_varu(&buf, 10);
	buf_write_bytes(&buf, "hi", 2);
	expect_string_error("string_len_too_long", buf.data, buf.len);

	buf_init(&buf, storage, sizeof(storage));
	buf_push(&buf, '8');
	buf_push(&buf, ':');
	buf_push(&buf, 'S');
	buf_push(&buf, 0x80); // unterminated varu
	expect_string_error("bad_varu", buf.data, buf.len);

	buf_init(&buf, storage, sizeof(storage));
	build_string_packet(&buf, "hello");
	expect_bin_error("bin_tag_mismatch", buf.data, buf.len, 32);

	buf_init(&buf, storage, sizeof(storage));
	build_function_packet(&buf, "List", 2);
	buf.len -= 1;
	expect_function_error("fn_truncated_name", buf.data, buf.len, 32);

	buf_init(&buf, storage, sizeof(storage));
	buf_push(&buf, '8');
	buf_push(&buf, ':');
	buf_push(&buf, 'f');
	expect_function_error("fn_truncated_argc", buf.data, buf.len, 32);

	buf_init(&buf, storage, sizeof(storage));
	buf_push(&buf, '8');
	buf_push(&buf, ':');
	buf_push(&buf, 'f');
	buf_write_varu(&buf, 1);
	buf_push(&buf, 'S');
	expect_function_error("fn_bad_name_tag", buf.data, buf.len, 32);

	buf_init(&buf, storage, sizeof(storage));
	buf_push(&buf, '8');
	buf_push(&buf, ':');
	buf_push(&buf, 'f');
	buf_write_varu(&buf, 1);
	buf_push(&buf, 's');
	buf_push(&buf, 0x80);
	expect_function_error("fn_bad_name_varu", buf.data, buf.len, 32);

	buf_init(&buf, storage, sizeof(storage));
	build_function_packet(&buf, "LongName", 1);
	expect_function_error("fn_name_too_long", buf.data, buf.len, 4);

	buf_init(&buf, storage, sizeof(storage));
	build_association_packet(&buf, 3);
	buf.len -= 1;
	expect_assoc_error("assoc_truncated_varu", buf.data, buf.len);

	buf_init(&buf, storage, sizeof(storage));
	buf_write_bytes(&buf, "8:+", 3);
	expect_rule_error("assoc_rule_bad_tag", buf.data, buf.len);

	u8 enc_u8 = mirage_array_type_encoding[MIRAGE_ARRAY_U8];
	u8 enc_u16 = mirage_array_type_encoding[MIRAGE_ARRAY_U16];
	u8 enc_c64 = mirage_array_type_encoding[MIRAGE_ARRAY_C64];
	u8 valid_numeric[64];
	size_t valid_numeric_len = 0;

	buf_init(&buf, storage, sizeof(storage));
	build_string_packet(&buf, "x");
	expect_numeric_error("numeric_bad_tag", buf.data, buf.len, 4, 64);

	buf_init(&buf, storage, sizeof(storage));
	build_numeric_packet(&buf, 0xC2, 0xFF, 0, NULL, 0, NULL, 0);
	expect_numeric_error("invalid_numeric_encoding", buf.data, buf.len, 4, 64);

	buf_init(&buf, storage, sizeof(storage));
	buf_write_bytes(&buf, "8:\xC2", 3);
	expect_numeric_error("truncated_numeric_header", buf.data, buf.len, 4, 64);

	buf_init(&buf, storage, sizeof(storage));
	buf_push(&buf, '8');
	buf_push(&buf, ':');
	buf_push(&buf, 0xC2);
	buf_push(&buf, enc_u8);
	buf_push(&buf, 0x80);
	expect_numeric_error("numeric_rank_truncated", buf.data, buf.len, 4, 64);

	buf_init(&buf, storage, sizeof(storage));
	build_numeric_packet(&buf, 0xC2, enc_u8, (u64) 1, NULL, 0, NULL, 0);
	expect_numeric_error("numeric_dims_truncated", buf.data, buf.len, 4, 64);

	buf_init(&buf, storage, sizeof(storage));
	buf_push(&buf, '8');
	buf_push(&buf, ':');
	buf_push(&buf, 0xC2);
	buf_push(&buf, enc_u8);
	buf_write_varu(&buf, 1);
	buf_push(&buf, 0x80);
	expect_numeric_error("numeric_dims_varu_truncated", buf.data, buf.len, 4, 64);

	buf_init(&buf, storage, sizeof(storage));
	{
		u64 dims[1] = {(u64) 1 << 63};
		build_numeric_packet(&buf, 0xC2, enc_u8, 1, dims, 1, NULL, 0);
	}
	expect_numeric_error("numeric_dims_overflow", buf.data, buf.len, 4, 64);

	buf_init(&buf, storage, sizeof(storage));
	{
		u64 dims[2] = {(u64) 1 << 62, 4};
		build_numeric_packet(&buf, 0xC2, enc_u8, 2, dims, 2, NULL, 0);
	}
	expect_numeric_error("numeric_elem_overflow", buf.data, buf.len, 4, 64);

	buf_init(&buf, storage, sizeof(storage));
	{
		u64 dims[1] = {(u64) 1 << 62};
		build_numeric_packet(&buf, 0xC2, enc_c64, 1, dims, 1, NULL, 0);
	}
	expect_numeric_error("numeric_bytes_overflow", buf.data, buf.len, 4, 64);

	buf_init(&buf, storage, sizeof(storage));
	{
		u64 dims[1] = {2};
		u8 data[2] = {1, 2};
		build_numeric_packet(&buf, 0xC2, enc_u16, 1, dims, 1, data, sizeof(data));
	}
	expect_numeric_error("numeric_data_truncated", buf.data, buf.len, 4, 64);

	buf_init(&buf, storage, sizeof(storage));
	{
		u64 dims[1] = {2};
		u8 data[2] = {1, 2};
		build_numeric_packet(&buf, 0xC2, enc_u16, 1, dims, 1, data, sizeof(data));
	}
	expect_numeric_error_skip("numeric_skip_truncated", buf.data, buf.len, 4);

	buf_init(&buf, storage, sizeof(storage));
	{
		u64 dims[1] = {2};
		u8 data[4] = {1, 2, 3, 4};
		build_numeric_packet(&buf, 0xC2, enc_u16, 1, dims, 1, data, sizeof(data));
	}
	expect_numeric_error("numeric_max_bytes", buf.data, buf.len, 4, 2);

	buf_init(&buf, valid_numeric, sizeof(valid_numeric));
	{
		u64 dims[1] = {2};
		u8 data[2] = {1, 2};
		build_numeric_packet(&buf, 0xC2, enc_u8, 1, dims, 1, data, sizeof(data));
		valid_numeric_len = buf.len;
	}

	buf_init(&buf, storage, sizeof(storage));
	{
		u64 dims[6] = {1, 1, 1, 1, 1, 1};
		u8 data[1] = {1};
		build_numeric_packet(&buf, 0xC2, enc_u8, 6, dims, 6, data, sizeof(data));
	}
	expect_numeric_error("numeric_max_rank", buf.data, buf.len, 4, 64);

	buf_init(&buf, storage, sizeof(storage));
	{
		u64 dims[1] = {2};
		u8 data[2] = {1, 2};
		build_numeric_packet(&buf, 0xC2, enc_u8, 1, dims, 1, data, sizeof(data));
	}
	expect_numeric_ok_skip("numeric_skip_data", buf.data, buf.len, 4);

	buf_init(&buf, storage, sizeof(storage));
	build_numeric_packet(&buf, 0xC2, enc_u8, 0, NULL, 0, NULL, 0);
	expect_numeric_error("numeric_scalar_missing_data", buf.data, buf.len, 0, 0);

	buf_init(&buf, storage, sizeof(storage));
	build_numeric_packet(&buf, 0xC2, enc_u8, 1, NULL, 0, NULL, 0);
	expect_numeric_error("numeric_dims_missing", buf.data, buf.len, 1, 64);

	expect_numeric_error("numeric_zero_max_bytes", valid_numeric, valid_numeric_len,
			     1, 0);
	expect_numeric_error_null_data("numeric_null_data", valid_numeric, valid_numeric_len,
				       1, 64);
	expect_numeric_error_null_dims("numeric_null_dims", valid_numeric, valid_numeric_len,
				       1, 64);
	expect_numeric_error("numeric_negative_max_rank", valid_numeric, valid_numeric_len, -2, 64);
	expect_numeric_error("numeric_negative_max_bytes", valid_numeric, valid_numeric_len, 1, -2);

	printf("Fuzz-style invalid decode tests passed.\n");
	return 0;
}
