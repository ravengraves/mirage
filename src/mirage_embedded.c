// mirage_embedded.c - Embedded-optimized WXF implementation
// Uses int16_t for all length fields (max 32KB buffers)
// Big-endian safe via explicit byte manipulation

#include "mirage_embedded.h"

// Type aliases for brevity
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

// ---- Encoding tables --------------------------------------------------------
const u8 mirage_element_type_encoding[16] = {
		'f', 'C', 'j', 'i', 'L', 'r', 'S', 'B',
		's', 'I', 'R', 0xC1, 0xC2, 'A', ':', '-'};
const u8 mirage_array_type_encoding[12] = {
		0x00, 0x01, 0x02, 0x03, 0x10, 0x11,
		0x12, 0x13, 0x22, 0x23, 0x33, 0x34};
const u8 mirage_array_type_size[12] = {
		1, 2, 4, 8, 1, 2, 4, 8, 4, 8, 8, 16};

// ---- Small helpers ----------------------------------------------------------

// Portable checked multiply for i16 values
// Returns 1 if overflow, 0 if safe (result stored in *out)
static inline int _checked_mul_i16(i16 a, i16 b, i16 *out) {
#if defined(__GNUC__) || defined(__clang__)
	return __builtin_mul_overflow(a, b, out);
#else
	if(a == 0 || b == 0) {
		*out = 0;
		return 0;
	}
	if(a > 0 && b > 0 && a > INT16_MAX / b) {
		return 1;
	}
	if(a > 0 && b < 0 && b < INT16_MIN / a) {
		return 1;
	}
	if(a < 0 && b > 0 && a < INT16_MIN / b) {
		return 1;
	}
	if(a < 0 && b < 0 && a < INT16_MAX / b) {
		return 1;
	}
	*out = a * b;
	return 0;
#endif
}

static inline void _memcpy(u8 *dest, const u8 *src, u16 n) {
	for(u16 i = 0; i < n; i++) {
		dest[i] = src[i];
	}
}

static inline i16 _strlen(const char *str) {
	u16 i = 0;
	while(*str++ != 0) {
		i = i + 1;
		if(i > INT16_MAX) {
			return -1; // Length exceeds 32768
		}
	}
	return (i16) i;
}

// Public entry points validate state before buffer access or cursor arithmetic.
static inline int _validate_msg(const mirage_msg *m) {
	// This ordering also implies nonnegative length and max_length.
	return m && m->data && m->cursor >= 0
		&& m->cursor <= m->length && m->length <= m->max_length;
}

static inline int _need(mirage_msg *m, i16 n) {
	return (n <= m->max_length - m->cursor);
}

static inline i16 _varu_size(u16 v) {
	i16 n = 0;
	do {
		v >>= 7;
		n++;
	} while(v > 0);
	return n;
}

static inline void _write_varu(mirage_msg *m, u16 v) {
	do {
		m->data[m->cursor] = (u8) ((v & 0x7F) | 0x80);
		v >>= 7;
		m->cursor++;
	} while(v > 0);
	m->data[m->cursor - 1] &= 0x7F;
}

static inline i16 _read_varu(mirage_msg *m, u16 *out) {
	i16 s = m->cursor;
	u32 acc = 0;
	// Lengths fit the signed API range; reject overflow before shifting.
	for(int sh = 0; sh < 15; sh += 7) {
		if(m->cursor >= m->length) {
			m->cursor = s;
			return MIRAGE_ERR_BUFFERSIZE;
		}
		u8 b = (u8) m->data[m->cursor++];
		u32 part = b & 0x7F;
		if(part > ((u32) INT16_MAX >> sh)) {
			m->cursor = s;
			return MIRAGE_ERR_BADDATA;
		}
		acc |= part << sh;
		if(b < 0x80) {
			*out = (u16) acc;
			return m->cursor - s;
		}
	}
	m->cursor = s;
	return MIRAGE_ERR_BADDATA;
}

// Little-endian write helpers
static inline void _w8(mirage_msg *m, u8 v) {
	m->data[m->cursor++] = v;
}

static inline void _w16le(mirage_msg *m, u16 v) {
	m->data[m->cursor + 0] = (u8) (v >> 0);
	m->data[m->cursor + 1] = (u8) (v >> 8);
	m->cursor += 2;
}

static inline void _w32le(mirage_msg *m, u32 v) {
	m->data[m->cursor + 0] = (u8) (v >> 0);
	m->data[m->cursor + 1] = (u8) (v >> 8);
	m->data[m->cursor + 2] = (u8) (v >> 16);
	m->data[m->cursor + 3] = (u8) (v >> 24);
	m->cursor += 4;
}

static inline void _w64le(mirage_msg *m, u64 v) {
	m->data[m->cursor + 0] = (u8) (v >> 0);
	m->data[m->cursor + 1] = (u8) (v >> 8);
	m->data[m->cursor + 2] = (u8) (v >> 16);
	m->data[m->cursor + 3] = (u8) (v >> 24);
	m->data[m->cursor + 4] = (u8) (v >> 32);
	m->data[m->cursor + 5] = (u8) (v >> 40);
	m->data[m->cursor + 6] = (u8) (v >> 48);
	m->data[m->cursor + 7] = (u8) (v >> 56);
	m->cursor += 8;
}

// Little-endian read helpers
static inline void _r16le(mirage_msg *m, u16 *v) {
	if(v) {
		*v = ((u16) (u8) m->data[m->cursor + 0] << 0)
				| ((u16) (u8) m->data[m->cursor + 1] << 8);
	}
	m->cursor += 2;
}

static inline void _r32le(mirage_msg *m, u32 *v) {
	if(v) {
		*v = ((u32) (u8) m->data[m->cursor + 0] << 0)
				| ((u32) (u8) m->data[m->cursor + 1] << 8)
				| ((u32) (u8) m->data[m->cursor + 2] << 16)
				| ((u32) (u8) m->data[m->cursor + 3] << 24);
	}
	m->cursor += 4;
}

static inline void _r64le(mirage_msg *m, u64 *v) {
	if(v) {
		*v = ((u64) (u8) m->data[m->cursor + 0] << 0)
				| ((u64) (u8) m->data[m->cursor + 1] << 8)
				| ((u64) (u8) m->data[m->cursor + 2] << 16)
				| ((u64) (u8) m->data[m->cursor + 3] << 24)
				| ((u64) (u8) m->data[m->cursor + 4] << 32)
				| ((u64) (u8) m->data[m->cursor + 5] << 40)
				| ((u64) (u8) m->data[m->cursor + 6] << 48)
				| ((u64) (u8) m->data[m->cursor + 7] << 56);
	}
	m->cursor += 8;
}

// ---- Init/Reset -------------------------------------------------------------

void mirage_init(mirage_msg *msg, uint8_t *buffer, int16_t buffer_size) {
	if(!msg) {
		return;
	}
	if(!buffer || buffer_size <= 0) {
		*msg = (mirage_msg) {0};
		return;
	}
	msg->max_length = buffer_size;
	msg->length = 0;
	msg->cursor = 0;
	msg->data = buffer;
}

void mirage_reset(mirage_msg *msg) {
	if(!_validate_msg(msg)) {
		return;
	}
	msg->length = 0;
	msg->cursor = 0;
}

i16 mirage_remaining(const mirage_msg *msg) {
	if(!_validate_msg(msg)) {
		return 0;
	}
	return msg->max_length - msg->cursor;
}

// ---- Write ------------------------------------------------------------------

// Call only after checking space for the complete element.
static inline void _write_buf_unchecked(mirage_msg *m, const i8 *buf, i16 len, i8 tag) {
	_w8(m, (u8) tag);
	_write_varu(m, (u16) len);
	_memcpy(m->data + m->cursor, (const u8 *) buf, (u16) len);
	m->cursor += len;
	m->length = m->cursor;
}

static i16 _write_buf(mirage_msg *m, const i8 *buf, i16 len, i8 tag) {
	if(len < 0) {
		return MIRAGE_ERR_BADINPUT;
	}
	i16 header = 1 + _varu_size((u16) len);
	i16 remaining = m->max_length - m->cursor;
	// Subtract the small header before comparing the potentially large payload.
	if(header > remaining || len > remaining - header) {
		return MIRAGE_ERR_BUFFERSIZE;
	}
	_write_buf_unchecked(m, buf, len, tag);
	return header + len;
}

i16 mirage_write_start(mirage_msg *m) {
	if(!_validate_msg(m)) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(m->max_length < 2) {
		return MIRAGE_ERR_BUFFERSIZE;
	}
	m->data[0] = '8';
	m->data[1] = ':';
	m->cursor = 2;
	m->length = 2;
	return 2;
}

i16 mirage_write_fn(mirage_msg *m, const char *name, i16 nlen, i16 argc) {
	if(!_validate_msg(m) || !name) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(argc < 0 || nlen < MIRAGE_STRLEN) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(nlen == MIRAGE_STRLEN) {
		nlen = _strlen(name);
		if(nlen < 0) {
			return MIRAGE_ERR_BADINPUT;
		}
	}
	i16 s = m->cursor;
	i16 header = 1 + _varu_size((u16) argc) + 1 + _varu_size((u16) nlen);
	i16 remaining = m->max_length - m->cursor;
	if(header > remaining || nlen > remaining - header) {
		return MIRAGE_ERR_BUFFERSIZE;
	}
	_w8(m, 'f');
	_write_varu(m, (u16) argc);
	_write_buf_unchecked(m, (const i8 *) name, nlen, 's');
	m->length = m->cursor;
	return m->cursor - s;
}

i16 mirage_write_i8(mirage_msg *m, i8 v) {
	if(!_validate_msg(m)) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(!_need(m, 2)) {
		return MIRAGE_ERR_BUFFERSIZE;
	}
	_w8(m, 'C');
	_w8(m, (u8) v);
	m->length = m->cursor;
	return 2;
}

i16 mirage_write_i16(mirage_msg *m, i16 v) {
	if(!_validate_msg(m)) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(!_need(m, 3)) {
		return MIRAGE_ERR_BUFFERSIZE;
	}
	_w8(m, 'j');
	_w16le(m, (u16) v);
	m->length = m->cursor;
	return 3;
}

i16 mirage_write_i32(mirage_msg *m, i32 v) {
	if(!_validate_msg(m)) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(!_need(m, 5)) {
		return MIRAGE_ERR_BUFFERSIZE;
	}
	_w8(m, 'i');
	_w32le(m, (u32) v);
	m->length = m->cursor;
	return 5;
}

i16 mirage_write_i64(mirage_msg *m, i64 v) {
	if(!_validate_msg(m)) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(!_need(m, 9)) {
		return MIRAGE_ERR_BUFFERSIZE;
	}
	_w8(m, 'L');
	_w64le(m, (u64) v);
	m->length = m->cursor;
	return 9;
}

i16 mirage_write_f64(mirage_msg *m, f64 v) {
	if(!_validate_msg(m)) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(!_need(m, 9)) {
		return MIRAGE_ERR_BUFFERSIZE;
	}
	_w8(m, 'r');
	union {
		f64 d;
		u64 i;
	} u;
	u.d = v;
	_w64le(m, u.i);
	m->length = m->cursor;
	return 9;
}

i16 mirage_write_string(mirage_msg *m, const char *v, i16 l) {
	if(!_validate_msg(m) || !v) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(l == MIRAGE_STRLEN) {
		l = _strlen(v);
	}
	return _write_buf(m, (const i8 *) v, l, 'S');
}

i16 mirage_write_bin(mirage_msg *m, const u8 *d, i16 l) {
	if(!_validate_msg(m) || !d) {
		return MIRAGE_ERR_BADINPUT;
	}
	return _write_buf(m, (const i8 *) d, l, 'B');
}

i16 mirage_write_sym(mirage_msg *m, const char *n, i16 l) {
	if(!_validate_msg(m) || !n) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(l == MIRAGE_STRLEN) {
		l = _strlen(n);
	}
	return _write_buf(m, (const i8 *) n, l, 's');
}

i16 mirage_write_bigint(mirage_msg *m, const char *v, i16 l) {
	if(!_validate_msg(m) || !v) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(l == MIRAGE_STRLEN) {
		l = _strlen(v);
	}
	return _write_buf(m, (const i8 *) v, l, 'I');
}

i16 mirage_write_bigreal(mirage_msg *m, const char *v, i16 l) {
	if(!_validate_msg(m) || !v) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(l == MIRAGE_STRLEN) {
		l = _strlen(v);
	}
	return _write_buf(m, (const i8 *) v, l, 'R');
}

i16 mirage_write_numericarray(mirage_msg *m, mirage_array_type t, i16 rank,
			      const i16 *dims, const void *data) {
	if(!_validate_msg(m) || !data) {
		return MIRAGE_ERR_BADINPUT;
	}
	if((unsigned) t >= 12) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(rank < 0) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(rank > 0 && !dims) {
		return MIRAGE_ERR_BADINPUT;
	}

	i16 remaining = m->max_length - m->cursor;
	i16 header = 2 + _varu_size((u16) rank);
	if(header > remaining) {
		return MIRAGE_ERR_BUFFERSIZE;
	}
	remaining -= header;
	i16 elems = 1;
	int overflow = 0;
	for(i16 i = 0; i < rank; i++) {
		if(dims[i] < 0) {
			return MIRAGE_ERR_BADINPUT;
		}
		if(dims[i] == 0) {
			elems = 0;
			overflow = 0;
		} else if(!overflow) {
			i16 new_elems;
			overflow = _checked_mul_i16(elems, dims[i], &new_elems);
			if(!overflow) elems = new_elems;
		}
		// Debit each dimension so even a large rank cannot overflow the header.
		i16 dim_bytes = _varu_size((u16) dims[i]);
		if(dim_bytes > remaining) {
			return MIRAGE_ERR_BUFFERSIZE;
		}
		remaining -= dim_bytes;
	}

	i16 bytes;
	if(overflow || _checked_mul_i16(elems, (i16) mirage_array_type_size[t], &bytes)) {
		return MIRAGE_ERR_BADINPUT;
	}

	if(bytes > remaining) {
		return MIRAGE_ERR_BUFFERSIZE;
	}

	i16 start = m->cursor;
	_w8(m, 0xC2);
	_w8(m, mirage_array_type_encoding[t]);
	_write_varu(m, (u16) rank);
	for(i16 i = 0; i < rank; i++) {
		_write_varu(m, (u16) dims[i]);
	}

	// Copy through aligned locals, then write each component as little-endian.
	switch(t) {
	case MIRAGE_ARRAY_I8:
	case MIRAGE_ARRAY_U8:
		_memcpy(m->data + m->cursor, (const u8 *) data, (u16) bytes);
		m->cursor += bytes;
		break;
	case MIRAGE_ARRAY_I16:
	case MIRAGE_ARRAY_U16: {
		const u8 *p = (const u8 *) data;
		for(i16 i = 0; i < elems; i++) {
			u16 value;
			_memcpy((u8 *) &value, p + i * sizeof(value), sizeof(value));
			_w16le(m, value);
		}
		break;
	}
	case MIRAGE_ARRAY_C32:
		elems *= 2;
		/* fallthrough */
	case MIRAGE_ARRAY_I32:
	case MIRAGE_ARRAY_U32:
	case MIRAGE_ARRAY_F32: {
		const u8 *p = (const u8 *) data;
		for(i16 i = 0; i < elems; i++) {
			u32 value;
			_memcpy((u8 *) &value, p + i * sizeof(value), sizeof(value));
			_w32le(m, value);
		}
		break;
	}
	case MIRAGE_ARRAY_C64:
		elems *= 2;
		/* fallthrough */
	case MIRAGE_ARRAY_I64:
	case MIRAGE_ARRAY_U64:
	case MIRAGE_ARRAY_F64: {
		const u8 *p = (const u8 *) data;
		for(i16 i = 0; i < elems; i++) {
			u64 value;
			_memcpy((u8 *) &value, p + i * sizeof(value), sizeof(value));
			_w64le(m, value);
		}
		break;
	}
	default:
		return MIRAGE_ERR_BADINPUT;
	}

	m->length = m->cursor;
	return m->cursor - start;
}

i16 mirage_write_association(mirage_msg *m, i16 len) {
	if(!_validate_msg(m) || len < 0) {
		return MIRAGE_ERR_BADINPUT;
	}
	i16 need = 1 + _varu_size((u16) len);
	if(!_need(m, need)) {
		return MIRAGE_ERR_BUFFERSIZE;
	}
	_w8(m, 'A');
	_write_varu(m, (u16) len);
	m->length = m->cursor;
	return need;
}

i16 mirage_write_assoc_rule(mirage_msg *m) {
	if(!_validate_msg(m)) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(!_need(m, 1)) {
		return MIRAGE_ERR_BUFFERSIZE;
	}
	_w8(m, '-');
	m->length = m->cursor;
	return 1;
}

// ---- Read -------------------------------------------------------------------

static i16 _read_buf(mirage_msg *m, i8 *dst, i16 *len, i16 max, i8 tag) {
	if(max < -1 || (max > 0 && !dst)) {
		return MIRAGE_ERR_BADINPUT;
	}
	i16 s = m->cursor;
	if(m->cursor >= m->length) {
		return -2;
	}
	if(m->data[m->cursor] != (u8) tag) {
		return -1;
	}
	m->cursor++;

	u16 L = 0;
	i16 rc_L = _read_varu(m, &L);
	if(rc_L < 0) {
		m->cursor = s;
		return rc_L;
	}
	if((max >= 0 && L > (u16) max) || L > (u16) (m->length - m->cursor)) {
		m->cursor = s;
		return -2;
	}
	if(max > 0) {
		_memcpy((u8 *) dst, m->data + m->cursor, L);
	}
	m->cursor += (i16) L;
	*len = (i16) L;
	return m->cursor - s;
}

i16 mirage_read_start(mirage_msg *m) {
	if(!_validate_msg(m)) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(m->length < 2) {
		return -2;
	}
	if(m->data[0] == '8' && m->data[1] == ':') {
		m->cursor = 2;
		return 2;
	}
	return -1;
}

i16 mirage_read_fn(mirage_msg *m, char *name, i16 *nlen, i16 nmax, i16 *argc) {
	if(!_validate_msg(m) || !nlen || nmax < -1 || (nmax > 0 && !name)) {
		return MIRAGE_ERR_BADINPUT;
	}
	i16 s = m->cursor;
	if(m->cursor >= m->length) {
		return -2;
	}
	if(m->data[m->cursor] != 'f') {
		return -1;
	}
	m->cursor++;

	u16 a = 0;
	i16 rc_a = _read_varu(m, &a);
	if(rc_a < 0) {
		m->cursor = s;
		return rc_a;
	}

	// Read symbol name
	if(m->cursor >= m->length) {
		m->cursor = s;
		return MIRAGE_ERR_BUFFERSIZE;
	}
	if(m->data[m->cursor] != 's') {
		m->cursor = s;
		return -1;
	}
	m->cursor++;

	u16 L = 0;
	i16 rc_L = _read_varu(m, &L);
	if(rc_L < 0) {
		m->cursor = s;
		return rc_L;
	}
	if((nmax >= 0 && L > (u16) nmax) || L > (u16) (m->length - m->cursor)) {
		m->cursor = s;
		return MIRAGE_ERR_BUFFERSIZE;
	}
	if(nmax > 0) {
		_memcpy((u8 *) name, m->data + m->cursor, L);
	}
	m->cursor += (i16) L;
	*nlen = (i16) L;
	if(argc) {
		*argc = (i16) a;
	}
	return m->cursor - s;
}

i16 mirage_read_i8(mirage_msg *m, i8 *v) {
	if(!_validate_msg(m) || !v) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(2 > m->length - m->cursor) {
		return -2;
	}
	if(m->data[m->cursor] != 'C') {
		return -1;
	}
	*v = (i8) m->data[m->cursor + 1];
	m->cursor += 2;
	return 2;
}

i16 mirage_read_i16(mirage_msg *m, i16 *v) {
	if(!_validate_msg(m) || !v) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(3 > m->length - m->cursor) {
		return -2;
	}
	if(m->data[m->cursor] != 'j') {
		return -1;
	}
	m->cursor++;
	_r16le(m, (u16 *) v);
	return 3;
}

i16 mirage_read_i32(mirage_msg *m, i32 *v) {
	if(!_validate_msg(m) || !v) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(5 > m->length - m->cursor) {
		return -2;
	}
	if(m->data[m->cursor] != 'i') {
		return -1;
	}
	m->cursor++;
	_r32le(m, (u32 *) v);
	return 5;
}

i16 mirage_read_i64(mirage_msg *m, i64 *v) {
	if(!_validate_msg(m) || !v) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(9 > m->length - m->cursor) {
		return -2;
	}
	if(m->data[m->cursor] != 'L') {
		return -1;
	}
	m->cursor++;
	_r64le(m, (u64 *) v);
	return 9;
}

i16 mirage_read_f64(mirage_msg *m, f64 *v) {
	if(!_validate_msg(m) || !v) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(9 > m->length - m->cursor) {
		return -2;
	}
	if(m->data[m->cursor] != 'r') {
		return -1;
	}
	m->cursor++;
	union {
		f64 d;
		u64 i;
	} u;
	_r64le(m, &u.i);
	*v = u.d;
	return 9;
}

i16 mirage_read_string(mirage_msg *m, char *dst, i16 *l, i16 max) {
	if(!_validate_msg(m) || !l) {
		return MIRAGE_ERR_BADINPUT;
	}
	return _read_buf(m, (i8 *) dst, l, max, 'S');
}

i16 mirage_read_bin(mirage_msg *m, u8 *dst, i16 *l, i16 max) {
	if(!_validate_msg(m) || !l) {
		return MIRAGE_ERR_BADINPUT;
	}
	return _read_buf(m, (i8 *) dst, l, max, 'B');
}

i16 mirage_read_sym(mirage_msg *m, char *dst, i16 *l, i16 max) {
	if(!_validate_msg(m) || !l) {
		return MIRAGE_ERR_BADINPUT;
	}
	return _read_buf(m, (i8 *) dst, l, max, 's');
}

i16 mirage_read_bigint(mirage_msg *m, char *dst, i16 *l, i16 max) {
	if(!_validate_msg(m) || !l) {
		return MIRAGE_ERR_BADINPUT;
	}
	return _read_buf(m, (i8 *) dst, l, max, 'I');
}

i16 mirage_read_bigreal(mirage_msg *m, char *dst, i16 *l, i16 max) {
	if(!_validate_msg(m) || !l) {
		return MIRAGE_ERR_BADINPUT;
	}
	return _read_buf(m, (i8 *) dst, l, max, 'R');
}

i16 mirage_read_numericarray(mirage_msg *m, mirage_array_type *t, i16 *rank,
			     i16 *dims, i16 max_rank, void *data, i16 max_bytes) {
	if(!_validate_msg(m) || !t || !rank) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(max_rank < -1 || max_bytes < MIRAGE_SKIP_DATA) {
		return MIRAGE_ERR_BADINPUT;
	}
	int skip_dims = (max_rank == -1);
	int skip_data = (max_bytes == MIRAGE_SKIP_DATA);
	if(!skip_data && !data) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(max_rank > 0 && !dims) {
		return MIRAGE_ERR_BADINPUT;
	}
	i16 s = m->cursor;

	if(m->cursor >= m->length) {
		return -2;
	}

	u8 tag = (u8) m->data[m->cursor++];
	if(tag != 0xC2 && tag != 0xC1) {
		m->cursor = s;
		return -1;
	}

	if(m->cursor >= m->length) {
		m->cursor = s;
		return -2;
	}

	// Parse array type
	u8 enc = (u8) m->data[m->cursor++];
	mirage_array_type array_type = MIRAGE_ARRAY_U8;
	int found = 0;
	for(i8 i = 0; i < 12; i++) {
		if(enc == mirage_array_type_encoding[i]) {
			array_type = (mirage_array_type) i;
			found = 1;
			break;
		}
	}
	if(!found) {
		m->cursor = s;
		return -1;
	}

	i16 elem_size = (i16) mirage_array_type_size[array_type];

	// Parse rank
	u16 r = 0;
	i16 rc_r = _read_varu(m, &r);
	if(rc_r < 0) {
		m->cursor = s;
		return rc_r;
	}
	i16 dims_start = m->cursor;

	// Parse dimensions
	i16 elems = 1;
	int overflow = 0;
	for(i16 i = 0; i < (i16) r; i++) {
		u16 d = 0;
		i16 rc_d = _read_varu(m, &d);
		if(rc_d < 0) {
			m->cursor = s;
			return rc_d;
		}
		if(d == 0) {
			elems = 0;
			overflow = 0;
		} else if(!overflow) {
			i16 new_elems;
			overflow = _checked_mul_i16(elems, (i16) d, &new_elems);
			if(!overflow) elems = new_elems;
		}
	}

	// Compute bytes needed
	i16 bytes;
	if(overflow || _checked_mul_i16(elems, elem_size, &bytes)) {
		m->cursor = s;
		return -1;
	}

	if((!skip_dims && r > (u16) max_rank)
		|| (!skip_data && bytes > max_bytes) || bytes > m->length - m->cursor) {
		m->cursor = s;
		return MIRAGE_ERR_BUFFERSIZE;
	}

	// All validation is complete. Re-read dimensions without temporary storage.
	if(!skip_dims) {
		mirage_msg dimensions = *m;
		dimensions.cursor = dims_start;
		for(i16 i = 0; i < (i16) r; ++i) {
			u16 d = 0;
			_read_varu(&dimensions, &d);
			dims[i] = (i16) d;
		}
	}
	*t = array_type;
	*rank = (i16) r;
	if(skip_data) {
		m->cursor += bytes;
		return m->cursor - s;
	}

	// Read through aligned locals before copying to the caller's byte buffer.
	switch(array_type) {
	case MIRAGE_ARRAY_I8:
	case MIRAGE_ARRAY_U8:
		_memcpy((u8 *) data, m->data + m->cursor, (u16) bytes);
		m->cursor += bytes;
		break;
	case MIRAGE_ARRAY_I16:
	case MIRAGE_ARRAY_U16: {
		u8 *p = (u8 *) data;
		for(i16 i = 0; i < elems; i++) {
			u16 value;
			_r16le(m, &value);
			_memcpy(p + i * sizeof(value), (const u8 *) &value, sizeof(value));
		}
		break;
	}
	case MIRAGE_ARRAY_C32:
		elems *= 2;
		/* fallthrough */
	case MIRAGE_ARRAY_I32:
	case MIRAGE_ARRAY_U32:
	case MIRAGE_ARRAY_F32: {
		u8 *p = (u8 *) data;
		for(i16 i = 0; i < elems; i++) {
			u32 value;
			_r32le(m, &value);
			_memcpy(p + i * sizeof(value), (const u8 *) &value, sizeof(value));
		}
		break;
	}
	case MIRAGE_ARRAY_C64:
		elems *= 2;
		/* fallthrough */
	case MIRAGE_ARRAY_I64:
	case MIRAGE_ARRAY_U64:
	case MIRAGE_ARRAY_F64: {
		u8 *p = (u8 *) data;
		for(i16 i = 0; i < elems; i++) {
			u64 value;
			_r64le(m, &value);
			_memcpy(p + i * sizeof(value), (const u8 *) &value, sizeof(value));
		}
		break;
	}
	}

	return m->cursor - s;
}

i16 mirage_read_association(mirage_msg *m, i16 *len) {
	if(!_validate_msg(m) || !len) {
		return MIRAGE_ERR_BADINPUT;
	}
	i16 s = m->cursor;
	if(m->cursor >= m->length) {
		return -2;
	}
	if(m->data[m->cursor] != 'A') {
		return -1;
	}
	m->cursor++;

	u16 L = 0;
	i16 rc_L = _read_varu(m, &L);
	if(rc_L < 0) {
		m->cursor = s;
		return rc_L;
	}
	*len = (i16) L;
	return m->cursor - s;
}

i16 mirage_read_assoc_rule(mirage_msg *m) {
	if(!_validate_msg(m)) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(m->cursor >= m->length) {
		return -2;
	}
	if(m->data[m->cursor] != '-' && m->data[m->cursor] != ':') {
		return -1;
	}
	m->cursor++;
	return 1;
}

// ---- Peek -------------------------------------------------------------------

i16 mirage_peek_element_type(mirage_msg *m, mirage_element_type *e) {
	if(!_validate_msg(m) || !e) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(m->cursor >= m->length) {
		return -2;
	}
	u8 enc = (u8) m->data[m->cursor];
	for(i8 i = 0; i < 16; i++) {
		if(enc == mirage_element_type_encoding[i]) {
			*e = i == _MIRAGE_PACKEDARRAY ? MIRAGE_NUMERICARRAY
				: i == _MIRAGE_RULE_DELAYED ? MIRAGE_RULE : (mirage_element_type) i;
			return 1; // Return bytes inspected
		}
	}
	return -1;
}
