/**
 * @file mirage_wasm.c
 * @brief WASM-specific wrapper for Mirage with minimal runtime.
 *
 * Uses the embedded implementation which is more WASM32-friendly
 * (uses int16_t lengths instead of int64_t).
 */

#include <stddef.h>
#include <stdint.h>

/* Simple bump allocator with static heap */
#define HEAP_SIZE (1024 * 1024) /* 1MB heap */

static uint8_t heap[HEAP_SIZE];
static size_t heap_used = 0;

void *calloc(size_t nmemb, size_t size) {
	size_t n = nmemb * size;
	n = (n + 15) & ~(size_t) 15; /* 16-byte alignment */
	if(heap_used + n > HEAP_SIZE) {
		return (void *) 0;
	}
	void *p = &heap[heap_used];
	heap_used += n;
	/* Zero memory */
	uint8_t *ptr = (uint8_t *) p;
	for(size_t i = 0; i < n; i++) {
		ptr[i] = 0;
	}
	return p;
}

void *malloc(size_t size);
__attribute__((visibility("default"))) void *malloc(size_t size) {
	return calloc(1, size);
}

void free(void *ptr) {
	(void) ptr; /* No-op for bump allocator */
}

/* memset - needed by embedded implementation */
void *memset(void *s, int c, size_t n) {
	uint8_t *p = (uint8_t *) s;
	while(n--) {
		*p++ = (uint8_t) c;
	}
	return s;
}

/* Include the embedded implementation (no stdlib dependency) */
#include "mirage_embedded.c"

/* Export marker for WASM */
#define WASM_EXPORT __attribute__((visibility("default")))

/* Reset heap for reuse - call between encode/decode operations */
WASM_EXPORT void wasm_reset(void) {
	heap_used = 0;
}

/* Get heap usage (for debugging) */
WASM_EXPORT uint32_t wasm_heap_used(void) {
	return (uint32_t) heap_used;
}

/* =========================================================================
 * Message management
 * ========================================================================= */

/* Create a message with buffer from heap */
WASM_EXPORT mirage_msg *wasm_create(int32_t size) {
	mirage_msg *msg = (mirage_msg *) malloc(sizeof(mirage_msg));
	if(!msg) {
		return (mirage_msg *) 0;
	}
	uint8_t *buf = (uint8_t *) malloc((size_t) size);
	if(!buf) {
		return (mirage_msg *) 0;
	}
	mirage_init(msg, buf, (int16_t) (size > 32767 ? 32767 : size));
	return msg;
}

/* Destroy is no-op with bump allocator */
WASM_EXPORT void wasm_destroy(mirage_msg *msg) {
	(void) msg;
}

WASM_EXPORT int32_t wasm_write_start(mirage_msg *msg) {
	return (int32_t) mirage_write_start(msg);
}

WASM_EXPORT int32_t wasm_read_start(mirage_msg *msg) {
	return (int32_t) mirage_read_start(msg);
}

/* =========================================================================
 * Write functions
 * ========================================================================= */

WASM_EXPORT int32_t wasm_write_i8(mirage_msg *msg, int8_t value) {
	return (int32_t) mirage_write_i8(msg, value);
}

WASM_EXPORT int32_t wasm_write_i16(mirage_msg *msg, int16_t value) {
	return (int32_t) mirage_write_i16(msg, value);
}

WASM_EXPORT int32_t wasm_write_i32(mirage_msg *msg, int32_t value) {
	return (int32_t) mirage_write_i32(msg, value);
}

WASM_EXPORT int32_t wasm_write_i64_lo(mirage_msg *msg, int32_t lo, int32_t hi) {
	int64_t value = ((int64_t) (uint32_t) hi << 32) | (uint32_t) lo;
	return (int32_t) mirage_write_i64(msg, value);
}

WASM_EXPORT int32_t wasm_write_f64(mirage_msg *msg, double value) {
	return (int32_t) mirage_write_f64(msg, value);
}

WASM_EXPORT int32_t wasm_write_string(mirage_msg *msg, char *str, int32_t len) {
	return (int32_t) mirage_write_string(msg, str, (int16_t) len);
}

WASM_EXPORT int32_t wasm_write_bin(mirage_msg *msg, uint8_t *data, int32_t len) {
	return (int32_t) mirage_write_bin(msg, data, (int16_t) len);
}

WASM_EXPORT int32_t wasm_write_sym(mirage_msg *msg, char *name, int32_t len) {
	return (int32_t) mirage_write_sym(msg, name, (int16_t) len);
}

WASM_EXPORT int32_t wasm_write_bigint(mirage_msg *msg, char *str, int32_t len) {
	return (int32_t) mirage_write_bigint(msg, str, (int16_t) len);
}

WASM_EXPORT int32_t wasm_write_fn(mirage_msg *msg, char *name, int32_t name_len, int32_t argc) {
	return (int32_t) mirage_write_fn(msg, name, (int16_t) name_len, (int16_t) argc);
}

WASM_EXPORT int32_t wasm_write_association(mirage_msg *msg, int32_t len) {
	return (int32_t) mirage_write_association(msg, (int16_t) len);
}

WASM_EXPORT int32_t wasm_write_assoc_rule(mirage_msg *msg) {
	return (int32_t) mirage_write_assoc_rule(msg);
}

WASM_EXPORT int32_t wasm_write_numericarray(mirage_msg *msg, int32_t atype, int32_t rank,
					    int16_t *dims, void *data) {
	return (int32_t) mirage_write_numericarray(msg, (mirage_array_type) atype,
						   (int16_t) rank, dims, data);
}

/* =========================================================================
 * Read functions
 * ========================================================================= */

WASM_EXPORT int32_t wasm_peek_element_type(mirage_msg *msg) {
	mirage_element_type etype;
	int16_t ret = mirage_peek_element_type(msg, &etype);
	if(ret < 0) {
		return (int32_t) ret;
	}
	return (int32_t) etype;
}

WASM_EXPORT int32_t wasm_read_i8(mirage_msg *msg, int8_t *out) {
	return (int32_t) mirage_read_i8(msg, out);
}

WASM_EXPORT int32_t wasm_read_i16(mirage_msg *msg, int16_t *out) {
	return (int32_t) mirage_read_i16(msg, out);
}

WASM_EXPORT int32_t wasm_read_i32(mirage_msg *msg, int32_t *out) {
	return (int32_t) mirage_read_i32(msg, out);
}

WASM_EXPORT int32_t wasm_read_i64(mirage_msg *msg, int32_t *lo_out, int32_t *hi_out) {
	int64_t value;
	int16_t ret = mirage_read_i64(msg, &value);
	if(ret >= 0) {
		*lo_out = (int32_t) (uint32_t) value;
		*hi_out = (int32_t) (uint32_t) (value >> 32);
	}
	return (int32_t) ret;
}

WASM_EXPORT int32_t wasm_read_f64(mirage_msg *msg, double *out) {
	return (int32_t) mirage_read_f64(msg, out);
}

WASM_EXPORT int32_t wasm_read_string(mirage_msg *msg, char *buf, int32_t *len_out, int32_t buf_size) {
	int16_t len = 0;
	int16_t ret = mirage_read_string(msg, buf, &len, (int16_t) buf_size);
	*len_out = (int32_t) len;
	return (int32_t) ret;
}

WASM_EXPORT int32_t wasm_read_bin(mirage_msg *msg, uint8_t *buf, int32_t *len_out, int32_t buf_size) {
	int16_t len = 0;
	int16_t ret = mirage_read_bin(msg, buf, &len, (int16_t) buf_size);
	*len_out = (int32_t) len;
	return (int32_t) ret;
}

WASM_EXPORT int32_t wasm_read_sym(mirage_msg *msg, char *buf, int32_t *len_out, int32_t buf_size) {
	int16_t len = 0;
	int16_t ret = mirage_read_sym(msg, buf, &len, (int16_t) buf_size);
	*len_out = (int32_t) len;
	return (int32_t) ret;
}

WASM_EXPORT int32_t wasm_read_bigint(mirage_msg *msg, char *buf, int32_t *len_out, int32_t buf_size) {
	int16_t len = 0;
	int16_t ret = mirage_read_bigint(msg, buf, &len, (int16_t) buf_size);
	*len_out = (int32_t) len;
	return (int32_t) ret;
}

WASM_EXPORT int32_t wasm_read_fn(mirage_msg *msg, char *name_buf, int32_t *name_len_out,
				 int32_t name_buf_size, int32_t *argc_out) {
	int16_t name_len = 0, argc = 0;
	int16_t ret = mirage_read_fn(msg, name_buf, &name_len, (int16_t) name_buf_size, &argc);
	*name_len_out = (int32_t) name_len;
	*argc_out = (int32_t) argc;
	return (int32_t) ret;
}

WASM_EXPORT int32_t wasm_read_association(mirage_msg *msg, int32_t *len_out) {
	int16_t len = 0;
	int16_t ret = mirage_read_association(msg, &len);
	*len_out = (int32_t) len;
	return (int32_t) ret;
}

WASM_EXPORT int32_t wasm_read_assoc_rule(mirage_msg *msg) {
	return (int32_t) mirage_read_assoc_rule(msg);
}

WASM_EXPORT int32_t wasm_read_numericarray(mirage_msg *msg, int32_t *atype_out, int32_t *rank_out,
					   int16_t *dims_out, int32_t dims_size,
					   void *data_out, int32_t data_size) {
	if(!atype_out || !rank_out || dims_size < -1 || dims_size > INT16_MAX
		|| data_size < MIRAGE_SKIP_DATA || data_size > INT16_MAX) {
		return MIRAGE_ERR_BADINPUT;
	}
	mirage_array_type atype;
	int16_t rank = 0;
	int16_t ret = mirage_read_numericarray(msg, &atype, &rank, dims_out,
					       (int16_t) dims_size, data_out, (int16_t) data_size);
	if(ret >= 0) {
		*atype_out = (int32_t) atype;
		*rank_out = (int32_t) rank;
	}
	return (int32_t) ret;
}

/* =========================================================================
 * Message struct accessors
 * ========================================================================= */

WASM_EXPORT int32_t wasm_msg_length(mirage_msg *msg) {
	return (int32_t) msg->length;
}

WASM_EXPORT int32_t wasm_msg_cursor(mirage_msg *msg) {
	return (int32_t) msg->cursor;
}

WASM_EXPORT uint8_t *wasm_msg_data(mirage_msg *msg) {
	return msg->data;
}

WASM_EXPORT void wasm_msg_set_length(mirage_msg *msg, int32_t len) {
	msg->length = (int16_t) len;
}
