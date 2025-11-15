/**
 * @file mirage.h
 * @brief Minimal library for directly reading and writing WXF data buffers.
 *
 * The Mirage library provides a lightweight API for creating, writing to,
 * reading from, and destroying a binary message buffer that follows the
 * WXF (Wavefront Exchange Format) encoding rules.  All write functions
 * append data to the buffer, while all read functions consume data from
 * the buffer starting at the current cursor position.
 *
 * @note All functions return the number of bytes written or read on
 * success, or a negative value on error.  The error codes are defined
 * in the @ref mirage_error enum.
 *
 * @note For string-related functions, passing `length == -1` will cause
 * the function to use `strlen(value)` (for writes) or to skip copying
 * the data (for reads).  This is a convenience for C callers that
 * prefer to supply null-terminated strings.
 */

#ifndef MIRAGE_H
#define MIRAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------- */
/** @struct mirage_msg
 *  @brief Core message buffer.
 *
 *  The `mirage_msg` structure holds the current state of a binary
 *  buffer.  `length` is the number of valid bytes in the buffer,
 *  `cursor` is the current read/write head position, and `data`
 *  points to the underlying byte array.
 */
typedef struct {
	int64_t length; /**< Total valid bytes currently in buffer. */
	int64_t cursor; /**< Read/write head. */
	uint8_t *data;	/**< Byte buffer. */
} mirage_msg;

/* -------------------------------------------------------------------------- */
/** @enum mirage_element_type
 *  @brief Enumeration of element types in the WXF format.
 *
 *  Each value corresponds to the byte code used in the WXF
 *  encoding.  The names are chosen to match the symbolic
 *  representation used by the original implementation.
 *
 *  @value MIRAGE_FUNCTION      102 "f"  head / "function"
 *  @value MIRAGE_I8            67  "C"  signed 8-bit integer
 *  @value MIRAGE_I16           106 "j"  signed 16-bit integer
 *  @value MIRAGE_I32           105 "i"  signed 32-bit integer
 *  @value MIRAGE_I64           76  "L"  signed 64-bit integer
 *  @value MIRAGE_F64           114 "r"  IEEE double-precision real
 *  @value MIRAGE_STRING        83  "S"  UTF-8 string
 *  @value MIRAGE_BINSTRING     66  "B"  arbitrary binary data
 *  @value MIRAGE_SYMBOL        115 "s"  symbol
 *  @value MIRAGE_BIGINT        73  "I"  big integer (decimal string)
 *  @value MIRAGE_BIGREAL       82  "R"  big real
 *  @value _MIRAGE_PACKEDARRAY  193 "Á"  packed array (unused, read as numeric)
 *  @value MIRAGE_NUMERICARRAY  194 "Â"  numeric array (accepts 0xC1 legacy packed)
 *  @value MIRAGE_ASSOCIATION   65  "A"  association (key-value)
 *  @value _MIRAGE_RULE_DELAYED 58  ":"  :> (treated as rule -)
 *  @value MIRAGE_RULE          45  "-"  -> (rule)
 */
typedef enum {
	MIRAGE_FUNCTION,
	MIRAGE_I8,
	MIRAGE_I16,
	MIRAGE_I32,
	MIRAGE_I64,
	MIRAGE_F64,
	MIRAGE_STRING,
	MIRAGE_BINSTRING,
	MIRAGE_SYMBOL,
	MIRAGE_BIGINT,
	MIRAGE_BIGREAL,
	_MIRAGE_PACKEDARRAY,
	MIRAGE_NUMERICARRAY,
	MIRAGE_ASSOCIATION,
	_MIRAGE_RULE_DELAYED,
	MIRAGE_RULE
} mirage_element_type;

/** @enum mirage_array_type
 *  @brief Enumeration of numeric array element types.
 *
 *  These values describe the type of elements stored in a numeric
 *  array.  They map to the corresponding WXF array type codes.
 *
 *  @value MIRAGE_ARRAY_I8   Signed 8-bit integer array.
 *  @value MIRAGE_ARRAY_I16  Signed 16-bit integer array.
 *  @value MIRAGE_ARRAY_I32  Signed 32-bit integer array.
 *  @value MIRAGE_ARRAY_I64  Signed 64-bit integer array.
 *  @value MIRAGE_ARRAY_U8   Unsigned 8-bit integer array.
 *  @value MIRAGE_ARRAY_U16  Unsigned 16-bit integer array.
 *  @value MIRAGE_ARRAY_U32  Unsigned 32-bit integer array.
 *  @value MIRAGE_ARRAY_U64  Unsigned 64-bit integer array.
 *  @value MIRAGE_ARRAY_F32  32-bit IEEE float array.
 *  @value MIRAGE_ARRAY_F64  64-bit IEEE double array.
 *  @value MIRAGE_ARRAY_C32  Complex 32-bit float array.
 *  @value MIRAGE_ARRAY_C64  Complex 64-bit double array.
 */
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

/* -------------------------------------------------------------------------- */
/** @enum mirage_error
 *  @brief Return value codes.
 *
 *  All Mirage API functions return the number of bytes written or read
 *  on success, or one of these negative error codes on failure.
 *
 *  @value MIRAGE_ERR_BADDATA   -1  Bad data encountered during parsing.
 *  @value MIRAGE_ERR_BUFFERSIZE -2  Buffer too small to hold data.
 *  @value MIRAGE_ERR_BADINPUT   -4  Invalid input parameters.
 */
typedef enum {
	MIRAGE_ERR_BADDATA = -1,
	MIRAGE_ERR_BUFFERSIZE = -2,
	MIRAGE_ERR_BADINPUT = -4
} mirage_error;

/* -------------------------------------------------------------------------- */
/** @name Setup / Teardown
 *  @{
 */

/** @brief Create a new message buffer.
 *
 *  Allocates a @c mirage_msg structure and a data buffer of the given
 *  size.  The @p alloc callback is used to allocate the buffer.  If
 *  allocation fails, @c NULL is returned.
 *
 *  @param buffer_size Size of the data buffer in bytes.
 *  @param alloc      Allocation callback; should have the same
 *                    signature as @c malloc.
 *  @return Pointer to the new @c mirage_msg, or @c NULL on failure.
 */
mirage_msg *mirage_create(int64_t buffer_size, void *(*alloc)(size_t));

/** @brief Destroy a message buffer.
 *
 *  Frees the data buffer using @p dealloc and then frees the
 *  @c mirage_msg structure itself.
 *
 *  @param msg      Pointer to the @c mirage_msg pointer to destroy.
 *  @param dealloc  Deallocation callback; should have the same
 *                  signature as @c free.
 */
void mirage_destroy(mirage_msg **msg, void (*dealloc)(void *));

/** @} */

/* -------------------------------------------------------------------------- */
/** @name Write Functions
 *  @{
 */

/** @brief Reset the write cursor and start a new message.
 *
 *  This function clears the buffer and positions the cursor at the
 *  beginning.  It returns the number of bytes written (2)
 *  or a negative error code.
 *
 *  @param msg Pointer to the message buffer.
 *  @return Number of bytes written or negative error code.
 */
int64_t mirage_write_start(mirage_msg *msg);

/** @brief Write a function element.
 *
 *  Writes a function head followed by its name and argument count.
 *
 *  @param msg        Pointer to the message buffer.
 *  @param name       Null-terminated function name.
 *  @param name_length Length of the name in bytes; if -1, `strlen(name)` is used.
 *  @param argc       Number of arguments the function takes.
 *  @return Number of bytes written or negative error code.
 */
int64_t mirage_write_fn(mirage_msg *msg, char *name, int64_t name_length, int64_t argc);

/** @brief Write a signed 8-bit integer. */
int64_t mirage_write_i8(mirage_msg *msg, int8_t value);

/** @brief Write a signed 16-bit integer. */
int64_t mirage_write_i16(mirage_msg *msg, int16_t value);

/** @brief Write a signed 32-bit integer. */
int64_t mirage_write_i32(mirage_msg *msg, int32_t value);

/** @brief Write a signed 64-bit integer. */
int64_t mirage_write_i64(mirage_msg *msg, int64_t value);

/** @brief Write a 64-bit IEEE double. */
int64_t mirage_write_f64(mirage_msg *msg, double value);

/** @brief Write a UTF-8 string.
 *
 *  @param msg        Pointer to the message buffer.
 *  @param value      Null-terminated string.
 *  @param length     Length of the string in bytes; if -1, `strlen(value)` is used.
 *  @return Number of bytes written or negative error code.
 */
int64_t mirage_write_string(mirage_msg *msg, char *value, int64_t length);

/** @brief Write arbitrary binary data. */
int64_t mirage_write_bin(mirage_msg *msg, uint8_t *data, int64_t length);

/** @brief Write a symbol. */
int64_t mirage_write_sym(mirage_msg *msg, char *name, int64_t length);

/** @brief Write a big integer (decimal string). */
int64_t mirage_write_bigint(mirage_msg *msg, char *value, int64_t length);

/** @brief Write a big real (decimal string). */
int64_t mirage_write_bigreal(mirage_msg *msg, char *value, int64_t length);

/** @brief Write a numeric array.
 *
 *  @param msg         Pointer to the message buffer.
 *  @param array_type  Type of elements in the array.
 *  @param rank        Number of dimensions (0 for scalar)
 *  @param dims        Array of dimension sizes (length = @p rank).
 *  @param data        Pointer to the raw element data.
 *  @return Number of bytes written or negative error code.
 */
int64_t mirage_write_numericarray(mirage_msg *msg, mirage_array_type array_type,
				  int64_t rank, int64_t *dims, void *data);

/** @brief Write the start of an association (key-value map). */
int64_t mirage_write_association(mirage_msg *msg, int64_t length);

/** @brief Write an association rule (key -> value). */
int64_t mirage_write_assoc_rule(mirage_msg *msg);

/** @} */

/* -------------------------------------------------------------------------- */
/** @name Read Functions
 *  @{
 */

/** @brief Reset the read cursor and advance the cursor past the messages start.
 *  @return start bytes or negative error code.
 */
int64_t mirage_read_start(mirage_msg *msg);

/** @brief Read a function element.
 *
 *  @param msg             Pointer to the message buffer.
 *  @param name            Buffer to receive the function name.
 *  @param name_length     Pointer to receive the length of the name.
 *  @param max_name_length Maximum buffer size for @p name.
 *  @param argc            Pointer to receive the argument count.
 *  @return Number of bytes read or negative error code.
 */
int64_t mirage_read_fn(mirage_msg *msg, char *name, int64_t *name_length,
		       int64_t max_name_length, int64_t *argc);

/** @brief Read a signed 8-bit integer. @return bytes or negative error code. */
int64_t mirage_read_i8(mirage_msg *msg, int8_t *value);

/** @brief Read a signed 16-bit integer. */
int64_t mirage_read_i16(mirage_msg *msg, int16_t *value);

/** @brief Read a signed 32-bit integer. */
int64_t mirage_read_i32(mirage_msg *msg, int32_t *value);

/** @brief Read a signed 64-bit integer. */
int64_t mirage_read_i64(mirage_msg *msg, int64_t *value);

/** @brief Read a 64-bit IEEE double. */
int64_t mirage_read_f64(mirage_msg *msg, double *value);

/** @brief Read a UTF-8 string.
 *
 *  If @p max_length is -1, the function does not copy the string data
 *  into @p value; it merely advances past the string message without copying.
 *
 *  @param msg        Pointer to the message buffer.
 *  @param value      Buffer to receive the string data.
 *  @param length     Pointer to receive the string length.
 *  @param max_length Maximum buffer size for @p value.
 *  @return Number of bytes read or negative error code.
 */
int64_t mirage_read_string(mirage_msg *msg, char *value, int64_t *length, int64_t max_length);

/** @brief Read arbitrary binary data. */
int64_t mirage_read_bin(mirage_msg *msg, uint8_t *data, int64_t *length, int64_t max_length);

/** @brief Read a symbol. */
int64_t mirage_read_sym(mirage_msg *msg, char *name, int64_t *name_length, int64_t max_length);

/** @brief Read a big integer (decimal string). */
int64_t mirage_read_bigint(mirage_msg *msg, char *value, int64_t *length, int64_t max_length);

/** @brief Read a big real (decimal string). */
int64_t mirage_read_bigreal(mirage_msg *msg, char *value, int64_t *length, int64_t max_length);

/** @brief Read a numeric array.
 *
 *  If @p max_bytes is -1, the function does not copy the string data
 *  into @p data; it merely advances past the string message without copying.
 *
 *  @param msg         Pointer to the message buffer.
 *  @param array_type  Pointer to receive the element type.
 *  @param rank        Pointer to receive the number of dimensions.
 *  @param dims        Buffer to receive the dimension sizes.
 *  @param max_rank    Maximum number of dimensions the caller can accept.
 *  @param data        Buffer to receive the raw element data.
 *  @param max_bytes   Maximum number of bytes the caller can accept.
 *  @return Number of bytes read or negative error code.
 */
int64_t mirage_read_numericarray(mirage_msg *msg, mirage_array_type *array_type,
				 int64_t *rank, int64_t *dims, int64_t max_rank, void *data, int64_t max_bytes);

/** @brief Read the start of an association. */
int64_t mirage_read_association(mirage_msg *msg, int64_t *length);

/** @brief Read an association rule (key -> value). */
int64_t mirage_read_assoc_rule(mirage_msg *msg);

/** @} */

/* -------------------------------------------------------------------------- */
/** @name Peek Utilities
 *  @{
 */

/** @brief Peek at the next element type without advancing the cursor.
 *
 *  @param msg Pointer to the message buffer.
 *  @param e   Pointer to receive the element type.
 *  @return Number of bytes consumed (typically the size of the type byte)
 *          or negative error code.
 */
int64_t mirage_peek_element_type(mirage_msg *msg, mirage_element_type *e);

/** @} */

/* -------------------------------------------------------------------------- */
/** @name Encoding Tables
 *  @{
 */

/** @var mirage_element_type_encoding
 *  @brief Mapping from @c mirage_element_type values to their WXF byte codes.
 */
extern const uint8_t mirage_element_type_encoding[16];

/** @var mirage_array_type_encoding
 *  @brief Mapping from @c mirage_array_type values to their WXF byte codes.
 */
extern const uint8_t mirage_array_type_encoding[12];

/** @var mirage_array_type_size
 *  @brief Size in bytes of each @c mirage_array_type element.
 */
extern const uint8_t mirage_array_type_size[12];

/** @} */

#ifdef __cplusplus
}
#endif

#endif // MIRAGE_H
