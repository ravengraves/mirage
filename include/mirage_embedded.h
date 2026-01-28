/**
 * @file mirage_embedded.h
 * @brief Embedded-optimized library for reading and writing WXF data buffers.
 *
 * This is a compact version of Mirage designed for embedded systems with
 * limited resources. Key differences from the standard mirage.h:
 *   - Uses int16_t for all length/size fields (max 32KB buffers)
 *   - Smaller memory footprint
 *   - No dynamic allocation required (bring your own buffer)
 *
 * @note All functions return the number of bytes written or read on
 * success, or a negative value on error.
 *
 * @note For string-related functions, use MIRAGE_STRLEN (-1) to automatically
 * determine string length, or MIRAGE_SKIP_COPY (-1) to skip copying data
 * during reads.
 */

#ifndef MIRAGE_EMBEDDED_H
#define MIRAGE_EMBEDDED_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/** @struct mirage_msg
 *  @brief Core message buffer for embedded systems.
 *
 *  Uses 16-bit integers for size fields, limiting buffers to 32KB max.
 */
typedef struct {
	int16_t max_length; /**< Total bytes allocated (max 32767). */
	int16_t length;	    /**< Total valid bytes currently in buffer. */
	int16_t cursor;	    /**< Read/write head position. */
	uint8_t *data;	    /**< Byte buffer (caller-provided). */
} mirage_msg;

/* -------------------------------------------------------------------------- */
/** @enum mirage_element_type
 *  @brief Enumeration of element types in the WXF format.
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
 */
typedef enum {
	MIRAGE_ERR_BADDATA = -1,
	MIRAGE_ERR_BUFFERSIZE = -2,
	MIRAGE_ERR_BADINPUT = -4
} mirage_error;

/* -------------------------------------------------------------------------- */
/** @name Special Length Constants
 *  @{
 */

/** @brief Special length value: automatically determine string length using strlen(). */
#define MIRAGE_STRLEN -1

/** @brief Special length value for read functions: skip copying data. */
#define MIRAGE_SKIP_COPY -1

/** @brief Special length value for array reads: skip copying array data. */
#define MIRAGE_SKIP_DATA -1

/** @} */

/* -------------------------------------------------------------------------- */
/** @name Setup / Teardown
 *  @{
 */

/** @brief Initialize a message buffer with caller-provided storage.
 *
 *  @param msg         Pointer to the mirage_msg structure to initialize.
 *  @param buffer      Pointer to the data buffer.
 *  @param buffer_size Size of the buffer in bytes (max 32767).
 */
void mirage_init(mirage_msg *msg, uint8_t *buffer, int16_t buffer_size);

/** @brief Reset the buffer for reuse. */
void mirage_reset(mirage_msg *msg);

/** @brief Get remaining buffer space in bytes. */
int16_t mirage_remaining(const mirage_msg *msg);

/** @} */

/* -------------------------------------------------------------------------- */
/** @name Write Functions
 *  @{
 */

/** @brief Reset the write cursor and start a new message.
 *  @return Number of bytes written or negative error code.
 */
int16_t mirage_write_start(mirage_msg *msg);

/** @brief Write a function element.
 *  @param name_length Length of name, or MIRAGE_STRLEN (-1) to use strlen(name).
 */
int16_t mirage_write_fn(mirage_msg *msg, const char *name, int16_t name_length, int16_t argc);

/** @brief Write a signed 8-bit integer. */
int16_t mirage_write_i8(mirage_msg *msg, int8_t value);

/** @brief Write a signed 16-bit integer. */
int16_t mirage_write_i16(mirage_msg *msg, int16_t value);

/** @brief Write a signed 32-bit integer. */
int16_t mirage_write_i32(mirage_msg *msg, int32_t value);

/** @brief Write a signed 64-bit integer. */
int16_t mirage_write_i64(mirage_msg *msg, int64_t value);

/** @brief Write a 64-bit IEEE double. */
int16_t mirage_write_f64(mirage_msg *msg, double value);

/** @brief Write a UTF-8 string.
 *  @param length Length in bytes, or MIRAGE_STRLEN (-1) to use strlen(value).
 */
int16_t mirage_write_string(mirage_msg *msg, const char *value, int16_t length);

/** @brief Write arbitrary binary data. */
int16_t mirage_write_bin(mirage_msg *msg, const uint8_t *data, int16_t length);

/** @brief Write a symbol.
 *  @param length Length in bytes, or MIRAGE_STRLEN (-1) to use strlen(name).
 */
int16_t mirage_write_sym(mirage_msg *msg, const char *name, int16_t length);

/** @brief Write a big integer (decimal string).
 *  @param length Length in bytes, or MIRAGE_STRLEN (-1) to use strlen(value).
 */
int16_t mirage_write_bigint(mirage_msg *msg, const char *value, int16_t length);

/** @brief Write a big real (decimal string).
 *  @param length Length in bytes, or MIRAGE_STRLEN (-1) to use strlen(value).
 */
int16_t mirage_write_bigreal(mirage_msg *msg, const char *value, int16_t length);

/** @brief Write a numeric array.
 *  @param rank Number of dimensions (0 for scalar).
 *  @param dims Array of dimension sizes.
 */
int16_t mirage_write_numericarray(mirage_msg *msg, mirage_array_type array_type,
				  int16_t rank, const int16_t *dims, const void *data);

/** @brief Write the start of an association (key-value map). */
int16_t mirage_write_association(mirage_msg *msg, int16_t length);

/** @brief Write an association rule (key -> value). */
int16_t mirage_write_assoc_rule(mirage_msg *msg);

/** @} */

/* -------------------------------------------------------------------------- */
/** @name Read Functions
 *  @{
 */

/** @brief Reset the read cursor and advance past the message start.
 *  @return Bytes consumed or negative error code.
 */
int16_t mirage_read_start(mirage_msg *msg);

/** @brief Read a function element. */
int16_t mirage_read_fn(mirage_msg *msg, char *name, int16_t *name_length,
		       int16_t max_name_length, int16_t *argc);

/** @brief Read a signed 8-bit integer. */
int16_t mirage_read_i8(mirage_msg *msg, int8_t *value);

/** @brief Read a signed 16-bit integer. */
int16_t mirage_read_i16(mirage_msg *msg, int16_t *value);

/** @brief Read a signed 32-bit integer. */
int16_t mirage_read_i32(mirage_msg *msg, int32_t *value);

/** @brief Read a signed 64-bit integer. */
int16_t mirage_read_i64(mirage_msg *msg, int64_t *value);

/** @brief Read a 64-bit IEEE double. */
int16_t mirage_read_f64(mirage_msg *msg, double *value);

/** @brief Read a UTF-8 string.
 *  @param max_length Max buffer size; if -1, skips without copying.
 */
int16_t mirage_read_string(mirage_msg *msg, char *value, int16_t *length, int16_t max_length);

/** @brief Read arbitrary binary data. */
int16_t mirage_read_bin(mirage_msg *msg, uint8_t *data, int16_t *length, int16_t max_length);

/** @brief Read a symbol. */
int16_t mirage_read_sym(mirage_msg *msg, char *name, int16_t *name_length, int16_t max_length);

/** @brief Read a big integer (decimal string). */
int16_t mirage_read_bigint(mirage_msg *msg, char *value, int16_t *length, int16_t max_length);

/** @brief Read a big real (decimal string). */
int16_t mirage_read_bigreal(mirage_msg *msg, char *value, int16_t *length, int16_t max_length);

/** @brief Read a numeric array.
 *  @param max_rank Maximum dimensions the caller can accept.
 *  @param max_bytes Maximum data bytes; if -1, skips without copying.
 */
int16_t mirage_read_numericarray(mirage_msg *msg, mirage_array_type *array_type,
				 int16_t *rank, int16_t *dims, int16_t max_rank,
				 void *data, int16_t max_bytes);

/** @brief Read the start of an association. */
int16_t mirage_read_association(mirage_msg *msg, int16_t *length);

/** @brief Read an association rule. */
int16_t mirage_read_assoc_rule(mirage_msg *msg);

/** @} */

/* -------------------------------------------------------------------------- */
/** @name Peek Utilities
 *  @{
 */

/** @brief Peek at the next element type without advancing the cursor.
 *  @return 1 (bytes inspected) on success, or negative error code.
 */
int16_t mirage_peek_element_type(mirage_msg *msg, mirage_element_type *e);

/** @} */

/* -------------------------------------------------------------------------- */
/** @name Encoding Tables
 *  @{
 */

extern const uint8_t mirage_element_type_encoding[16];
extern const uint8_t mirage_array_type_encoding[12];
extern const uint8_t mirage_array_type_size[12];

/** @} */

#ifdef __cplusplus
}
#endif

#endif // MIRAGE_EMBEDDED_H
