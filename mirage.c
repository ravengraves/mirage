// mirage.c — Little-endian host implementation.
#include "mirage.h"
#include <stdlib.h>

// --- Compile-time guard: this unit assumes little-endian host ---
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#error \
		"mirage.c assumes a little-endian host. Use mirage+embedded.c on big-endian."
#endif
#if defined(_WIN32)
// Windows platforms are little-endian on supported targets.
#endif

// ---- Encoding tables --------------------------------------------------------
const u8 mirage_element_type_encoding[16] = {'f', 'C', 'j', 'i', 'L', 'r',
					     'S', 'B', 's', 'I', 'R', 0xC1, 0xC2, 'A', '-', ':'};
const u8 mirage_array_type_encoding[12] = {0x0, 0x1, 0x2, 0x3, 0x10, 0x11,
					   0x12, 0x13, 0x22, 0x23, 0x33, 0x34};
const u8 mirage_array_type_size[12] = {1, 2, 4, 8, 1, 2, 4, 8, 4, 8, 8, 16};

// ---- Small helpers ----------------------------------------------------------
#if defined(__GNUC__) || defined(__clang__)
#define RESTRICT __restrict__
#elif defined(_MSC_VER)
#define RESTRICT __restrict
static inline i64 __builtin_mul_overflow(i64 x, i64 y, i64 *out) {
	// Check for overflow in element count, for positive integers
	// portable version
	if(x == 0 || y == 0) {
		*out = 0;
		return 0;
	}
	if(x < 0) {
		return 1;
	}
	if(y < 0) {
		return 1;
	}
	i64 y_approx = 1;
	i8 log2_y = 1;
	for(; log2_y < 63 && y_approx < y; ++log2_y) {
		y_approx <<= 1;
	}
	if(x > (INT64_MAX >> log2_y)) {
		return 1;
	}
	*out = x * y;
	return 0;
}
#else
#define RESTRICT
#endif

// memcpy(dest, src, bytes);
static inline void fast_memcpy(u8 *RESTRICT dest, const u8 *RESTRICT src, u64 n) {
	u64 i = 0;
	for(; i + 64 <= n; i += 64) {
		u64 *dstptr = (u64 *) (dest + i);
		const u64 *srcptr = (const u64 *) (src + i);
		dstptr[0] = srcptr[0];
		dstptr[1] = srcptr[1];
		dstptr[2] = srcptr[2];
		dstptr[3] = srcptr[3];
		dstptr[4] = srcptr[4];
		dstptr[5] = srcptr[5];
		dstptr[6] = srcptr[6];
		dstptr[7] = srcptr[7];
	}
	for(; i + 32 <= n; i += 32) {
		u64 *dstptr = (u64 *) (dest + i);
		const u64 *srcptr = (const u64 *) (src + i);
		dstptr[0] = srcptr[0];
		dstptr[1] = srcptr[1];
		dstptr[2] = srcptr[2];
		dstptr[3] = srcptr[3];
	}
	for(; i + 8 <= n; i += 8) {
		*(u64 *) (dest + i) = *(const u64 *) (src + i);
	}
	for(; i < n; i += 1) {
		dest[i] = src[i];
	}
}

static inline int _need_(mirage_msg *m, i64 n) {
	return (m->cursor + n <= m->length);
}

static inline i64 _varu_size_(u64 v) {
	i64 n = 0;
	do {
		v >>= 7;
		++n;
	} while(v > 0);
	return n;
}
static inline i64 _write_varu_(mirage_msg *m, u64 v) {
	do {
		m->data[m->cursor] = (i8) ((v & 0x7F) | 0x80);
		v >>= 7;
		m->cursor++;
	} while(v > 0);
	m->data[m->cursor - 1] &= 0x7F;
	return 0;
}
static inline i64 _read_varu_(mirage_msg *m, u64 *out) {
	i64 s = m->cursor;
	if(m->cursor >= m->length) {
		return -2;
	}
	if((u8) m->data[m->cursor] < 0x80) {
		*out = (u8) m->data[m->cursor++];
		return 1;
	}
	u64 acc = 0;
	for(i64 sh = 0; sh + 6 < (i64) sizeof(u64) * 8; sh += 7) {
		if(m->cursor >= m->length) {
			m->cursor = s;
			return -2;
		}
		u8 b = (u8) m->data[m->cursor++];
		acc |= (u64) (b & 0x7F) << sh;
		if(b < 0x80 && acc < INT64_MAX) {
			*out = acc;
			return m->cursor - s;
		}
	}
	m->cursor = s;
	return -2;
}

// Little-endian host: memcpy for 16/32/64 is OK (ABI endianness == wire LE).
static inline void _w8_(mirage_msg *m, u8 v) { m->data[m->cursor++] = (i8) v; }
static inline void _w16_(mirage_msg *m, u16 v) {
	*(u16 *) (m->data + m->cursor) = v;
	m->cursor += 2;
}
static inline void _w32_(mirage_msg *m, u32 v) {
	*(u32 *) (m->data + m->cursor) = v;
	m->cursor += 4;
}
static inline void _w64_(mirage_msg *m, u64 v) {
	*(u64 *) (m->data + m->cursor) = v;
	m->cursor += 8;
}
static inline void _r16_(mirage_msg *m, u16 *v) {
	*v = *(u16 *) (m->data + m->cursor);
	m->cursor += 2;
}
static inline void _r32_(mirage_msg *m, u32 *v) {
	*v = *(u32 *) (m->data + m->cursor);
	m->cursor += 4;
}
static inline void _r64_(mirage_msg *m, u64 *v) {
	*v = *(u64 *) (m->data + m->cursor);
	m->cursor += 8;
}

// ---- Create/Destroy ---------------------------------------------------------
mirage_msg *mirage_create(i64 buffer_size, void *(*alloc)(size_t)) {
	void *(*allocate)(size_t) = alloc ? alloc : malloc;
	mirage_msg *m = (mirage_msg *) allocate(sizeof(mirage_msg) + (size_t) buffer_size);
	if(!m) {
		return 0;
	}
	m->length = buffer_size;
	m->cursor = 0;
	m->data = (i8 *) m + sizeof(mirage_msg);
	mirage_write_start(m);
	return m;
}

void mirage_destroy(mirage_msg **m, void (*dealloc)(void *)) {
	void (*deallocate)(void *) = dealloc ? dealloc : free;
	if(*m) {
		deallocate(*m);
	}
	*m = 0;
}

// ---- Write ------------------------------------------------------------------
i64 mirage_write_start(mirage_msg *m) {
	if(m->length < 2) {
		return -1;
	}
	m->data[0] = '8';
	m->data[1] = ':';
	m->cursor = 2;
	return 2;
}
static inline i64 _write_buf_(mirage_msg *m, const i8 *buf, i64 len, i8 tag) {
	i64 need = 1 + _varu_size_((u64) len) + len;
	if(need > m->length - m->cursor) {
		return -2;
	}
	_w8_(m, (u8) tag);
	_write_varu_(m, (u64) len);
	fast_memcpy((u8 *) m->data + m->cursor, (u8 *) buf, (size_t) len);
	m->cursor += len;
	return need;
}
i64 mirage_write_fn(mirage_msg *m, char *name, i64 nlen, i64 argc) {
	i64 start = m->cursor;
	i64 worst = 1 + _varu_size_((u64) argc) + 1 + _varu_size_((u64) nlen) + nlen;
	if(worst > m->length - m->cursor) {
		return -2;
	}
	_w8_(m, 'f');
	_write_varu_(m, (u64) argc);
	_write_buf_(m, (i8 *) name, nlen, 's');
	return m->cursor - start;
}
i64 mirage_write_i8(mirage_msg *m, i8 v) {
	if(!_need_(m, 2)) {
		return -2;
	}
	_w8_(m, 'C');
	_w8_(m, (u8) v);
	return 2;
}
i64 mirage_write_i16(mirage_msg *m, i16 v) {
	if(!_need_(m, 3)) {
		return -2;
	}
	_w8_(m, 'j');
	_w16_(m, (u16) v);
	return 3;
}
i64 mirage_write_i32(mirage_msg *m, i32 v) {
	if(!_need_(m, 5)) {
		return -2;
	}
	_w8_(m, 'i');
	_w32_(m, (u32) v);
	return 5;
}
i64 mirage_write_i64(mirage_msg *m, i64 v) {
	if(!_need_(m, 9)) {
		return -2;
	}
	_w8_(m, 'L');
	_w64_(m, (u64) v);
	return 9;
}
i64 mirage_write_f64(mirage_msg *m, f64 v) {
	if(!_need_(m, 9)) {
		return -2;
	}
	_w8_(m, 'r');
	union {
		f64 d;
		u64 i;
	} u;
	u.d = v;
	_w64_(m, u.i);
	return 9;
}
i64 mirage_write_string(mirage_msg *m, char *v, i64 l) {
	return _write_buf_(m, (i8 *) v, l, 'S');
}
i64 mirage_write_bin(mirage_msg *m, i8 *d, i64 l) {
	return _write_buf_(m, d, l, 'B');
}
i64 mirage_write_sym(mirage_msg *m, char *n, i64 l) {
	return _write_buf_(m, (i8 *) n, l, 's');
}
i64 mirage_write_bigint(mirage_msg *m, char *v, i64 l) {
	return _write_buf_(m, (i8 *) v, l, 'I');
}
i64 mirage_write_bigreal(mirage_msg *m, char *v, i64 l) {
	return _write_buf_(m, (i8 *) v, l, 'R');
}

i64 mirage_write_numericarray(mirage_msg *m, mirage_array_type t, i64 rank,
			      i64 *dims, void *data) {
	if(rank < 0) {
		return -3;
	}
	i64 elems = 1;
	i64 dv = 0;
	for(i64 i = 0; i < rank; ++i) {
		i64 new_elems;
		if(dims[i] < 0 || __builtin_mul_overflow(elems, dims[i], &new_elems)) {
			return -3;
		}
		elems = new_elems;
		dv += _varu_size_((u64) dims[i]);
	}
	i64 bytes;
	if(__builtin_mul_overflow(elems, mirage_array_type_size[t], &bytes)) {
		return -3;
	}
	i64 need = 1 + 1 + _varu_size_((u64) rank) + dv + bytes;
	if(need > m->length - m->cursor) {
		return -2;
	}
	_w8_(m, 0xC2);
	_w8_(m, mirage_array_type_encoding[t]);
	_write_varu_(m, (u64) rank);
	for(i64 i = 0; i < rank; ++i) {
		_write_varu_(m, (u64) dims[i]);
	}
	fast_memcpy((u8 *) m->data + m->cursor, (u8 *) data, (size_t) bytes);
	m->cursor += bytes;
	return need;
}
i64 mirage_write_association(mirage_msg *m, i64 len) {
	if(!_need_(m, 1 + 9)) {
		return -2;
	}
	_w8_(m, 'A');
	_write_varu_(m, (u64) len);
	return 1 + _varu_size_((u64) len);
}
i64 mirage_write_assoc_rule(mirage_msg *m) {
	if(!_need_(m, 1)) {
		return -2;
	}
	_w8_(m, '-');
	return 1;
}

// ---- Read -------------------------------------------------------------------
i64 mirage_read_start(mirage_msg *m) {
	m->cursor = 0;
	if(m->cursor + 2 > m->length) {
		return -2;
	}
	if(*(i16 *) m->data == (((i16) '8') << 8 | (i16) ':')) {
		m->cursor = 2;
		return 2;
	}
	// if (m->data[0] == '8' && m->data[1] == 'C') {
	// 	return -10; // compressed (zlib) not implemented
	// }
	return -1;
}

i64 mirage_read_fn(mirage_msg *m, char *name, i64 *nlen, i64 nmax, i64 *argc) {
	// read function header
	i64 s = m->cursor;
	if(m->cursor >= m->length) {
		return -2;
	}
	if(m->data[m->cursor] != 'f') {
		return -1;
	}
	m->cursor++;
	u64 a = 0;
	if(_read_varu_(m, &a) < 0) {
		m->cursor = s;
		return -2;
	}
	*argc = (i64) a;
	// read symbol name
	if(m->cursor >= m->length) {
		m->cursor = s;
		return -2;
	}
	if(m->data[m->cursor] != 's') {
		m->cursor = s;
		return -1;
	}
	m->cursor++;
	u64 L = 0;
	if(_read_varu_(m, &L) < 0) {
		m->cursor = s;
		return -2;
	}
	if((i64) L > nmax || (i64) L > m->length - m->cursor) {
		m->cursor = s;
		return -3;
	}
	fast_memcpy((u8 *) name, (u8 *) m->data + m->cursor, (u64) L);
	m->cursor += (i64) L;
	*nlen = (i64) L;
	return m->cursor - s;
}
i64 mirage_read_i8(mirage_msg *m, i8 *v) {
	if(m->cursor + 2 > m->length) {
		return -2;
	}
	if(m->data[m->cursor] != 'C') {
		return -1;
	}
	*v = m->data[m->cursor + 1];
	m->cursor += 2;
	return 2;
}
i64 mirage_read_i16(mirage_msg *m, i16 *v) {
	if(m->cursor + 3 > m->length) {
		return -2;
	}
	if(m->data[m->cursor] != 'j') {
		return -1;
	}
	m->cursor++;
	_r16_(m, (u16 *) v);
	return 3;
}
i64 mirage_read_i32(mirage_msg *m, i32 *v) {
	if(m->cursor + 5 > m->length) {
		return -2;
	}
	if(m->data[m->cursor] != 'i') {
		return -1;
	}
	m->cursor++;
	_r32_(m, (u32 *) v);
	return 5;
}
i64 mirage_read_i64(mirage_msg *m, i64 *v) {
	if(m->cursor + 9 > m->length) {
		return -2;
	}
	if(m->data[m->cursor] != 'L') {
		return -1;
	}
	m->cursor++;
	_r64_(m, (u64 *) v);
	return 9;
}
i64 mirage_read_f64(mirage_msg *m, f64 *v) {
	if(m->cursor + 9 > m->length) {
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
	_r64_(m, &u.i);
	*v = u.d;
	return 9;
}
static inline i64 _read_buf_(mirage_msg *m, i8 *dst, i64 *len, i64 max, i8 tag) {
	i64 s = m->cursor;
	if(m->cursor >= m->length) {
		return -2;
	}
	if(m->data[m->cursor] != tag) {
		return -1;
	}
	m->cursor++;
	u64 L = 0;
	if(_read_varu_(m, &L) < 0) {
		m->cursor = s;
		return -2;
	}
	if((i64) L > max || (i64) L > m->length - m->cursor) {
		m->cursor = s;
		return -1;
	}
	fast_memcpy((u8 *) dst, (u8 *) m->data + m->cursor, (size_t) L);
	m->cursor += (i64) L;
	*len = (i64) L;
	return m->cursor - s;
}
i64 mirage_read_string(mirage_msg *m, char *dst, i64 *l, i64 max) {
	return _read_buf_(m, (i8 *) dst, l, max, 'S');
}
i64 mirage_read_bin(mirage_msg *m, i8 *dst, i64 *l, i64 max) {
	return _read_buf_(m, dst, l, max, 'B');
}
i64 mirage_read_sym(mirage_msg *m, char *dst, i64 *l, i64 max) {
	return _read_buf_(m, (i8 *) dst, l, max, 's');
}
i64 mirage_read_bigint(mirage_msg *m, char *dst, i64 *l, i64 max) {
	return _read_buf_(m, (i8 *) dst, l, max, 'I');
}
i64 mirage_read_bigreal(mirage_msg *m, char *dst, i64 *l, i64 max) {
	return _read_buf_(m, (i8 *) dst, l, max, 'R');
}

i64 mirage_read_numericarray(mirage_msg *m, mirage_array_type *t, i64 *rank,
			     i64 *dims, i64 max_rank, void *data,
			     i64 max_bytes) {
	i64 s = m->cursor;
	if(m->cursor >= m->length) {
		return -2;
	}
	u8 tag = (u8) m->data[m->cursor];
	if(tag != 0xC2 && tag != 0xC1) {
		return -1;
	}
	m->cursor++;
	if(m->cursor >= m->length) {
		m->cursor = s;
		return -2;
	}
	if(!data) {
		m->cursor = s;
		return -3;
	}
	u8 enc = (u8) m->data[m->cursor++];
	int found = 0;
	switch(enc) { // prioritize the most common types.
	case 16:
		found = 1;
		*t = (mirage_array_type) MIRAGE_ARRAY_U8;
		break;
	case 34:
		found = 1;
		*t = (mirage_array_type) MIRAGE_ARRAY_F32;
		break;
	case 35:
		found = 1;
		*t = (mirage_array_type) MIRAGE_ARRAY_F64;
		break;
	default:
		for(i64 i = 0; i < 12; ++i) {
			if(enc == mirage_array_type_encoding[i]) {
				*t = (mirage_array_type) i;
				found = 1;
				break;
			}
		}
	}
	if(!found) {
		m->cursor = s;
		return -1;
	}
	u64 r = 0;
	if(_read_varu_(m, &r) < 0) {
		m->cursor = s;
		return -2;
	}
	if((i64) r > max_rank) {
		m->cursor = s;
		return -1;
	}
	i64 elems = 1;
	*rank = (i64) r;

	// rank 0 tensor is scalar (1 element)

	// read ranks, accumulate total element count
	for(i64 i = 0; i < *rank; ++i) {
		u64 d = 0;
		if(_read_varu_(m, &d) < 0) {
			m->cursor = s;
			return -2;
		}
		dims[i] = (i64) d;

		i16 new_elems;
		if(__builtin_mul_overflow(elems, (i64) d, &new_elems)) {
			m->cursor = s;
			return -1;
		}
		elems = new_elems;
	}

	// compute bytes
	i64 bytes;
	if(__builtin_mul_overflow(elems, (i64) mirage_array_type_size[*t], &bytes)) {
		m->cursor = s;
		return -1;
	}
	if(bytes > max_bytes || bytes > m->length - m->cursor) {
		m->cursor = s;
		return -2;
	}
	fast_memcpy((u8 *) data, (u8 *) m->data + m->cursor, (u64) bytes);
	m->cursor += bytes;
	return m->cursor - s;
}
i64 mirage_read_association(mirage_msg *m, i64 *len) {
	i64 s = m->cursor;
	if(m->cursor >= m->length) {
		return -2;
	}
	if(m->data[m->cursor] != 'A') {
		return -1;
	}
	m->cursor++;
	u64 L = 0;
	if(_read_varu_(m, &L) < 0) {
		m->cursor = s;
		return -2;
	}
	*len = (i64) L;
	return m->cursor - s;
}
i64 mirage_read_assoc_rule(mirage_msg *m) {
	if(m->cursor >= m->length) {
		return -2;
	}
	if(m->data[m->cursor] != '-' && m->data[m->cursor] != ':') {
		return -1;
	}
	m->cursor++;
	return 1;
}


// ---- Peek (no cursor advance) -----------------------------------------------

// lookup table - 4 cache lines
static const uint8_t _mirage_keymap_[256] = {
		// 0 by default; fill with our 16 keys.
		['f'] = MIRAGE_FUNCTION,
		['C'] = MIRAGE_I8,
		['j'] = MIRAGE_I16,
		['i'] = MIRAGE_I32,
		['L'] = MIRAGE_I64,
		['r'] = MIRAGE_F64,
		['S'] = MIRAGE_STRING,
		['B'] = MIRAGE_BINSTRING,
		['s'] = MIRAGE_SYMBOL,
		['I'] = MIRAGE_BIGINT,
		['R'] = MIRAGE_BIGREAL,
		[193] = _MIRAGE_PACKEDARRAY,
		[194] = MIRAGE_NUMERICARRAY,
		['A'] = MIRAGE_ASSOCIATION,
		['-'] = MIRAGE_RULE,
		[':'] = _MIRAGE_RULE_DELAYED,
};

i64 mirage_peek_element_type(mirage_msg *m, mirage_element_type *e) {
	if(m->cursor >= m->length) {
		return -2;
	}
	u8 enc = (u8) m->data[m->cursor];
	u8 res = _mirage_keymap_[enc];
	if(res == 0 && enc != 'f') {
		return -1;
	}
	*e = res;
	return 0;
}
