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
 * @note For string-related functions, use MIRAGE_STRLEN (-1) to automatically
 * determine string length with strlen(), or MIRAGE_SKIP_COPY (-1) to skip
 * copying data during reads.
 *
 * @note Message operations require a live mirage_msg with non-NULL data and
 *        0 <= cursor <= length <= max_length. Readers, writers, peek and reset
 *        reject invalid state with MIRAGE_ERR_BADINPUT without changing it;
 *        mirage_remaining returns 0. The caller must provide storage of at
 *        least max_length bytes and keep it alive; validation cannot verify
 *        allocation size, lifetime, or non-NULL pointer validity. Other buffer
 *        arguments must likewise cover their declared sizes, and typed pointer
 *        arguments must have the alignment required by their types.
 *
 * @section example_basic Basic Encoding/Decoding Example
 *
 * @code
 * // Create buffer
 * mirage_msg *msg = mirage_create(1024, NULL);
 *
 * // Write data
 * mirage_write_start(msg);
 * mirage_write_fn(msg, "Plus", MIRAGE_STRLEN, 2);
 * mirage_write_i64(msg, 1);
 * mirage_write_i64(msg, 2);
 *
 * // Get encoded data for transmission
 * int64_t length = msg->cursor;
 * uint8_t *data = msg->data;
 * // ... send data over network ...
 *
 * // Read back (e.g., on receiving end)
 * mirage_msg *recv = mirage_create(1024, NULL);
 * memcpy(recv->data, data, length);
 *
 * mirage_read_start(recv);
 * char name[256];
 * int64_t name_len, argc;
 * mirage_read_fn(recv, name, &name_len, 256, &argc);
 *
 * int64_t arg1, arg2;
 * mirage_read_i64(recv, &arg1);
 * mirage_read_i64(recv, &arg2);
 *
 * // Cleanup
 * mirage_destroy(&msg, NULL);
 * mirage_destroy(&recv, NULL);
 * @endcode
 *
 * @section example_error Error Handling Example
 *
 * @code
 * int64_t rc = mirage_write_i64(msg, 42);
 * if (rc == MIRAGE_ERR_BUFFERSIZE) {
 *     // Buffer too small
 *     fprintf(stderr, "Buffer too small\n");
 * } else if (rc == MIRAGE_ERR_BADINPUT) {
 *     // Invalid input (msg was NULL?)
 *     fprintf(stderr, "Invalid input\n");
 * } else if (rc < 0) {
 *     // Other error
 *     fprintf(stderr, "Error: %lld\n", (long long)rc);
 * } else {
 *     // Success: rc contains bytes written
 *     printf("Wrote %lld bytes\n", (long long)rc);
 * }
 * @endcode
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
	int64_t max_length; /**< Total bytes allocated. */
	int64_t length;	    /**< Total valid bytes currently in buffer. */
	int64_t cursor;	    /**< Read/write head. */
	uint8_t *data;	    /**< Byte buffer. */
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
/** @name Special Length Constants
 *  @{
 */

/** @brief Special length value: automatically determine string length using strlen().
 *
 *  Use this value for the length parameter in write functions to automatically
 *  calculate string length. Only valid for null-terminated strings.
 */
#define MIRAGE_STRLEN (-1)

/** @brief Special length value for read functions: skip copying data.
 *
 *  Use this value for max_length in read functions to advance the cursor
 *  without copying data into the output buffer. Useful for inspecting
 *  data structure or skipping unwanted elements.
 */
#define MIRAGE_SKIP_COPY (-1)

/** @brief Special length value for array reads: skip copying array data.
 *
 *  Use this value for max_bytes in mirage_read_numericarray to read
 *  array metadata without copying the element data. Dimensions are copied
 *  when max_rank is not -1.
 */
#define MIRAGE_SKIP_DATA (-1)

/** @brief Recommended max numeric array payload size for untrusted inputs.
 *
 *  7680x4320 * 4 bytes per pixel * 2 frames.
 */
#define MIRAGE_MAX_TENSOR_BYTES (7680LL * 4320LL * 4LL * 2LL)

/** @} */

/* -------------------------------------------------------------------------- */
/** @name Setup / Teardown
 *  @{
 */

/** @brief Initialize a message buffer with caller-provided storage.
 *
 *  Does not allocate memory or write a message header; call
 *  mirage_write_start() before encoding a new message.
 *
 *  @param msg         Pointer to the mirage_msg structure to initialize.
 *  @param buffer      Pointer to the data buffer.
 *  @param buffer_size Size of the buffer in bytes (must be > 0).
 *  @note A NULL buffer or nonpositive size clears msg to an invalid, empty
 *        state. A NULL msg is ignored. Reinitialize with valid storage to use it.
 */
void mirage_init(mirage_msg *msg, uint8_t *buffer, int64_t buffer_size);

/** @brief Create a new message buffer.
 *
 *  Allocates a mirage_msg structure and a data buffer of the given
 *  size using the provided allocation callback.
 *
 *  @param buffer_size Size of the data buffer in bytes (must be >= 2 for the header).
 *  @param alloc      Allocation callback with malloc-compatible signature:
 *                    - Must return aligned memory suitable for any type
 *                    - Must return NULL on failure
 *                    - If NULL, uses system malloc
 *                    - Need not be thread-safe
 *
 *  @return Pointer to new mirage_msg, or NULL on failure (invalid size or allocation failure).
 *
 *  @note The deallocator passed to mirage_destroy() must match this allocator
 *        (e.g., custom free for custom malloc).
 *
 *  Example with custom allocator:
 *  @code
 *      void *my_alloc(size_t n) { return aligned_alloc(64, n); }
 *      void my_free(void *p) { free(p); }
 *
 *      mirage_msg *m = mirage_create(4096, my_alloc);
 *      // ... use message ...
 *      mirage_destroy(&m, my_free);
 *  @endcode
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

/** @brief Reset cursor to beginning without writing header.
 *
 *  Useful for reusing a buffer for multiple messages without
 *  calling mirage_write_start() which writes the "8:" header.
 *
 *  @param msg Pointer to the message buffer.
 *  @return 0 on success, or negative error code.
 */
int64_t mirage_reset(mirage_msg *msg);

/** @brief Get remaining buffer space in bytes.
 *
 *  Useful for checking if there's enough space before writing.
 *
 *  @param msg Pointer to the message buffer.
 *  @return Number of bytes available for writing, or 0 if msg is NULL.
 */
int64_t mirage_remaining(const mirage_msg *msg);

/** @brief Write a function element.
 *
 *  Writes a function head followed by its name and argument count.
 *
 *  @param msg        Pointer to the message buffer.
 *  @param name       Null-terminated function name.
 *  @param name_length Length of the name in bytes, or MIRAGE_STRLEN (-1) to use strlen(name).
 *  @param argc       Number of arguments the function takes.
 *  @return Number of bytes written or negative error code.
 */
int64_t mirage_write_fn(mirage_msg *msg, const char *name, int64_t name_length, int64_t argc);

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
 *  @param value      Null-terminated string (must be valid UTF-8).
 *  @param length     Length in bytes, or MIRAGE_STRLEN (-1) to use strlen(value).
 *  @return Number of bytes written or negative error code.
 */
int64_t mirage_write_string(mirage_msg *msg, const char *value, int64_t length);

/** @brief Write arbitrary binary data. */
int64_t mirage_write_bin(mirage_msg *msg, const uint8_t *data, int64_t length);

/** @brief Write a symbol. */
int64_t mirage_write_sym(mirage_msg *msg, const char *name, int64_t length);

/** @brief Write a big integer (decimal string). */
int64_t mirage_write_bigint(mirage_msg *msg, const char *value, int64_t length);

/** @brief Write a big real (decimal string). */
int64_t mirage_write_bigreal(mirage_msg *msg, const char *value, int64_t length);

/** @brief Write a numeric array.
 *
 *  @param msg         Pointer to the message buffer.
 *  @param array_type  Type of elements in the array.
 *  @param rank        Number of dimensions (0 for scalar)
 *  @param dims        Array of dimension sizes (length = @p rank).
 *                     Any zero dimension makes the payload empty, regardless of order.
 *  @param data        Pointer to the raw element data.
 *  @return Number of bytes written or negative error code.
 */
int64_t mirage_write_numericarray(mirage_msg *msg, mirage_array_type array_type,
				  int64_t rank, const int64_t *dims, const void *data);

/** @brief Write the start of an association (key-value map). */
int64_t mirage_write_association(mirage_msg *msg, int64_t length);

/** @brief Write an association rule (key -> value). */
int64_t mirage_write_assoc_rule(mirage_msg *msg);

/** @} */

/* -------------------------------------------------------------------------- */
/** @name Read Functions
 *  Readers leave the entry cursor and all outputs unchanged on error.
 *  Invalid arguments return MIRAGE_ERR_BADINPUT; insufficient capacity or
 *  truncated input returns MIRAGE_ERR_BUFFERSIZE; malformed encodings and wrong
 *  tags return MIRAGE_ERR_BADDATA. Varints support 0 through INT64_MAX in at most
 *  nine bytes; nonminimal encodings within that width are accepted.
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
 *  @param max_name_length Capacity in bytes, or MIRAGE_SKIP_COPY (-1) to skip the name.
 *  @param argc            Optional pointer to receive the argument count.
 *  @return Number of bytes read or negative error code.
 *  @note name_length is required. name is required for positive capacity;
 *        with zero capacity, only an empty name fits and name may be NULL.
 *        Skipping still validates and consumes the entire name.
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
 *  @param msg        Pointer to the message buffer.
 *  @param value      Buffer to receive string (may be NULL if max_length is MIRAGE_SKIP_COPY).
 *  @param length     Pointer to receive actual string length.
 *  @param max_length Buffer size, or MIRAGE_SKIP_COPY (-1) to skip copying.
 *  @return Number of bytes read from message buffer or negative error code.
 *
 *  @note When max_length is MIRAGE_SKIP_COPY, only the cursor advances and
 *        length is populated; no data is copied to value.
 *        Zero is an actual capacity: only an empty value fits. value is required
 *        for positive capacity; length is always required. Capacities below -1
 *        are invalid. These rules also apply to binary, symbol, bigint and
 *        bigreal readers. Returned text is not NUL-terminated.
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
 *  @param msg         Pointer to the message buffer.
 *  @param array_type  Pointer to receive the element type.
 *  @param rank        Pointer to receive the number of dimensions.
 *  @param dims        Buffer to receive dimension sizes; required when max_rank is positive.
 *  @param max_rank    Dimension capacity, or -1 to skip copying dimensions.
 *  @param data        Buffer to receive raw element data; required unless max_bytes is
 *                     MIRAGE_SKIP_DATA, even for an empty array.
 *  @param max_bytes   Maximum buffer size, or MIRAGE_SKIP_DATA (-1) to skip copying array elements.
 *  @return Number of bytes read or negative error code.
 *
 *  @note The two skip sentinels are independent. Skipped buffers may be NULL and
 *        are not modified. With max_rank == 0, dims may be NULL for a scalar.
 *        Type and rank are always required and returned on success. The full
 *        encoded array is validated and consumed even when outputs are skipped.
 *        Capacities below -1 return MIRAGE_ERR_BADINPUT; insufficient dimension
 *        or payload capacity returns MIRAGE_ERR_BUFFERSIZE. On error, the cursor
 *        and all output buffers are unchanged.
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
 *  @return 1 (bytes inspected) on success, or negative error code.
 *  @note Packed-array tags return MIRAGE_NUMERICARRAY and delayed-rule tags
 *        return MIRAGE_RULE. On error, the output is unchanged.
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
