// mirage - minimal library for directly reading and writing WXF data buffers.
#ifndef MIRAGE_H
#define MIRAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef float f32;
typedef double f64;

// ---- Core message buffer ----------------------------------------------------
typedef struct {
	i64 length; // total valid bytes currently in buffer
	i64 cursor; // read/write head
	i8 *data;   // byte buffer
} mirage_msg;

// ---- Element & array type enums ---------------------------------------------
typedef enum {
	MIRAGE_FUNCTION,      // 102 "f" head / "function"
	MIRAGE_I8,	      // 67 "C" signed 8-bit integer
	MIRAGE_I16,	      // 106 "j" signed 16-bit integer
	MIRAGE_I32,	      // 105 "i" signed 32-bit integer
	MIRAGE_I64,	      // 76 "L" signed 64-bit integer
	MIRAGE_F64,	      // 114 "r" IEEE double-precision real
	MIRAGE_STRING,	      // 83 "S" utf-8 string
	MIRAGE_BINSTRING,     // 66 "B" arbitrary binary data
	MIRAGE_SYMBOL,	      // 115 "s" symbol
	MIRAGE_BIGINT,	      // 73 "I" big integer (decimal string)
	MIRAGE_BIGREAL,	      // 82 "R" big real
	_MIRAGE_PACKEDARRAY,  // 193 "Á" packed array (unused, read as numeric)
	MIRAGE_NUMERICARRAY,  // 194 "Â" numeric array (accepts 0xC1 legacy packed)
	MIRAGE_ASSOCIATION,   // 65 "A" association (key-value)
	_MIRAGE_RULE_DELAYED, // 58 ":" :> (: treated as rule -)
	MIRAGE_RULE,	      // 45 "-" ->
} mirage_element_type;

typedef enum {
	MIRAGE_ARRAY_I8,
	MIRAGE_ARRAY_I16,
	MIRAGE_ARRAY_I32,
	MIRAGE_ARRAY_I64,
	MIRAGE_ARRAY_U8,
	MIRAGE_ARRAY_U16,
	MIRAGE_ARRAY_U32,
	MIRAGE_ARRAY_U64,
	MIRAGE_ARRAY_F32,
	MIRAGE_ARRAY_F64,
	MIRAGE_ARRAY_C32,
	MIRAGE_ARRAY_C64
} mirage_array_type;

// return values
typedef enum {
	MIRAGE_ERR_BADDATA = -1,
	MIRAGE_ERR_BUFFERSIZE = -2,
	MIRAGE_ERR_BADINPUT = -4,
} mirage_error;

// ---- Setup/teardown ---------------------------------------------------------
mirage_msg *mirage_create(i64 buffer_size, void *(*alloc)(size_t));
void mirage_destroy(mirage_msg **msg, void (*dealloc)(void *));

// ---- Write ------------------------------------------------------------------
i64 mirage_write_start(mirage_msg *msg);
i64 mirage_write_fn(mirage_msg *msg, char *name, i64 name_length, i64 argc);
i64 mirage_write_i8(mirage_msg *msg, i8 value);
i64 mirage_write_i16(mirage_msg *msg, i16 value);
i64 mirage_write_i32(mirage_msg *msg, i32 value);
i64 mirage_write_i64(mirage_msg *msg, i64 value);
i64 mirage_write_f64(mirage_msg *msg, f64 value);
i64 mirage_write_string(mirage_msg *msg, char *value, i64 length);
i64 mirage_write_bin(mirage_msg *msg, i8 *data, i64 length);
i64 mirage_write_sym(mirage_msg *msg, char *name, i64 length);
i64 mirage_write_bigint(mirage_msg *msg, char *value, i64 length);
i64 mirage_write_bigreal(mirage_msg *msg, char *value, i64 length);
i64 mirage_write_numericarray(mirage_msg *msg, mirage_array_type array_type,
			      i64 rank, i64 *dims, void *data);
i64 mirage_write_association(mirage_msg *msg, i64 length);
i64 mirage_write_assoc_rule(mirage_msg *msg);

// ---- Read -------------------------------------------------------------------
// passing length = -1 will cause read functions to not copy string/buffer data
i64 mirage_read_start(mirage_msg *msg);
i64 mirage_read_fn(mirage_msg *msg, char *name, i64 *name_length,
		   i64 max_name_length, i64 *argc);
i64 mirage_read_i8(mirage_msg *msg, i8 *value);
i64 mirage_read_i16(mirage_msg *msg, i16 *value);
i64 mirage_read_i32(mirage_msg *msg, i32 *value);
i64 mirage_read_i64(mirage_msg *msg, i64 *value);
i64 mirage_read_f64(mirage_msg *msg, f64 *value);
i64 mirage_read_string(mirage_msg *msg, char *value, i64 *length, i64 max_length);
i64 mirage_read_bin(mirage_msg *msg, i8 *data, i64 *length, i64 max_length);
i64 mirage_read_sym(mirage_msg *msg, char *name, i64 *name_length, i64 max_length);
i64 mirage_read_bigint(mirage_msg *msg, char *value, i64 *length, i64 max_length);
i64 mirage_read_bigreal(mirage_msg *msg, char *value, i64 *length, i64 max_length);
i64 mirage_read_numericarray(mirage_msg *msg, mirage_array_type *array_type,
			     i64 *rank, i64 *dims, i64 max_rank, void *data, i64 max_bytes);
i64 mirage_read_association(mirage_msg *msg, i64 *length);
i64 mirage_read_assoc_rule(mirage_msg *msg);

// ---- Peek utilities (do not advance cursor) ---------------------------------
i64 mirage_peek_element_type(mirage_msg *msg, mirage_element_type *e);

// ---- Tables (encoding) ------------------------------------------------------
extern const u8 mirage_element_type_encoding[16];
extern const u8 mirage_array_type_encoding[12];
extern const u8 mirage_array_type_size[12];

#ifdef __cplusplus
}
#endif

#endif // MIRAGE_H
