// mirage+embedded.c - Portable embedded/big-endian safe implementation.
// Always encodes/decodes little-endian on the wire via explicit byte shifts.
// This implementation also limits message sizes to 32,767 bytes  and uses
//  16-bit integer math for reduced footprint on microcontrollers.

#include "mirage.h"
#include "mirage_style.h"
#include <stdlib.h>

// ---- Encoding tables --------------------------------------------------------
const u8 mirage_element_type_encoding[16] = {'f', 'C', 'j', 'i', 'L', 'r',
					     'S', 'B', 's', 'I', 'R', 0xC1, 0xC2, 'A', ':', '-'};
const u8 mirage_array_type_encoding[12] = {0x0, 0x1, 0x2, 0x3, 0x10, 0x11,
					   0x12, 0x13, 0x22, 0x23, 0x33, 0x34};
const u8 mirage_array_type_size[12] = {1, 2, 4, 8, 1, 2, 4, 8, 4, 8, 8, 16};

// ---- Small helpers ----------------------------------------------------------

#if defined(__GNUC__) || defined(__clang__)
#define RESTRICT __restrict__
#elif defined(_MSC_VER)
#define RESTRICT __restrict
#else
#define RESTRICT
#endif

// if builtin not available __builtin_mul_overflow
// static i16 _approx_mul_overflow(i16 x, i16 y, i16 *out) {
// 	// Check for overflow in element count, for positive i16
// 	// portable version
// 	if (x == 0 || y == 0) {
// 		*out = 0;
// 		return 0;
// 	}
// 	if  (x < 0 || y < 0) {
// 		return 1;
// 	}
// 	i16 y_approx = 1;
// 	i8 log2_y=1;
// 	for(;log2_y < 15 && y_approx < y; ++log2_y){
// 		y_approx <<= 1;
// 	}
// 	if(x > ((0x7FFF) >> log2_y)) {
// 		return 1;
// 	}
// 	*out = x*y;
// 	return 0;
// }


// memcpy(dest, src, bytes);
static inline void fast_memcpy(u8 *RESTRICT dest, const u8 *RESTRICT src, u16 n) {
	for(u16 i = 0; i < n; i += 1) {
		dest[i] = src[i];
	}
}

static inline int _need_(mirage_msg *m, i64 n) { // check n <= remaining
	return (n <= m->length - m->cursor);
}

static inline i16 _varu_size_(u16 v) {
	i16 n = 0;
	do {
		v >>= 7;
		++n;
	} while(v > 0);
	return n;
}

static inline i16 _write_varu_(mirage_msg *m, u16 v) {
	do {
		m->data[m->cursor] = (i8) ((v & 0x7F) | 0x80);
		v >>= 7;
		m->cursor++;
	} while(v > 0);
	m->data[m->cursor - 1] &= 0x7F;
	return 0;
}

static inline i16 _read_varu_(mirage_msg *m, u16 *out) {
	i16 s = (i16) m->cursor;
	if(m->cursor >= m->length) {
		return -2;
	}
	if((u8) m->data[m->cursor] < 0x80) {
		*out = (u8) m->data[m->cursor++];
		return 1;
	}
	u32 acc = 0;
	for(i8 sh = 0; sh + 6 < (i8) sizeof(u32) * 8; sh += 7) { // embedded fail for large nums
		if(m->cursor >= m->length) {
			m->cursor = s;
			return -2;
		}
		u8 b = (u8) m->data[m->cursor++];
		acc |= (u64) (b & 0x7F) << sh;
		if(b < 0x80 && acc < INT16_MAX) {
			*out = acc;
			return m->cursor - s;
		}
	}
	m->cursor = s;
	return -2;
}

static inline void _w8_(mirage_msg *m, u8 v) { m->data[m->cursor++] = (i8) v; }
static inline void _w16le_(mirage_msg *m, u16 v) {
	m->data[m->cursor + 0] = (u8) ((v >> 0) & 0xFF);
	m->data[m->cursor + 1] = (u8) ((v >> 8) & 0xFF);
	m->cursor += 2;
}

static inline void _w32le_(mirage_msg *m, u32 v) {
	m->data[m->cursor + 0] = (u8) ((v >> 0) & 0xFF);
	m->data[m->cursor + 1] = (u8) ((v >> 8) & 0xFF);
	m->data[m->cursor + 2] = (u8) ((v >> 16) & 0xFF);
	m->data[m->cursor + 3] = (u8) ((v >> 24) & 0xFF);
	m->cursor += 4;
}

static inline void _w64le_(mirage_msg *m, u64 v) {
	m->data[m->cursor + 0] = (u8) ((v >> 0) & 0xFF);
	m->data[m->cursor + 1] = (u8) ((v >> 8) & 0xFF);
	m->data[m->cursor + 2] = (u8) ((v >> 16) & 0xFF);
	m->data[m->cursor + 3] = (u8) ((v >> 24) & 0xFF);
	m->data[m->cursor + 4] = (u8) ((v >> 32) & 0xFF);
	m->data[m->cursor + 5] = (u8) ((v >> 40) & 0xFF);
	m->data[m->cursor + 6] = (u8) ((v >> 48) & 0xFF);
	m->data[m->cursor + 7] = (u8) ((v >> 56) & 0xFF);
	m->cursor += 8;
}

static inline void _r16le_(mirage_msg *m, u16 *v) {
	if(v) {
		*v = ((u16) (u8) m->data[m->cursor + 0] << 0)
				| ((u16) (u8) m->data[m->cursor + 1] << 8);
	}
	m->cursor += 2;
}

static inline void _r32le_(mirage_msg *m, u32 *v) {
	if(v) {
		*v = ((u32) (u8) m->data[m->cursor + 0] << 0)
				| ((u32) (u8) m->data[m->cursor + 1] << 8)
				| ((u32) (u8) m->data[m->cursor + 2] << 16)
				| ((u32) (u8) m->data[m->cursor + 3] << 24);
	}
	m->cursor += 4;
}

static inline void _r64le_(mirage_msg *m, u64 *v) {
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

static inline i16 _strlen_(char *str) {
	i16 i = 0;
	while(str[i] != 0) {
		i += 1;
	}
	return i;
}

// ---- Create/Destroy ---------------------------------------------------------
mirage_msg *mirage_create(i64 buffer_size, void *(*alloc)(size_t)) {
	if(buffer_size > INT16_MAX) { // constrain for embedded
		return 0;
	}
	void *(*allocate)(size_t) = alloc ? (void *(*) (size_t)) alloc : malloc;
	mirage_msg *m = (mirage_msg *) allocate(sizeof(mirage_msg) + (size_t) buffer_size);
	if(!m) {
		return 0;
	}
	m->length = buffer_size;
	m->cursor = 0;
	m->data = (u8 *) m + sizeof(mirage_msg);
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
		return -2;
	}
	m->data[0] = '8';
	m->data[1] = ':';
	m->cursor = 2;
	return 2;
}

static i64 _write_buf_(mirage_msg *m, const i8 *buf, i64 len, i8 tag) {
	if(len > INT16_MAX || len < 0) {
		return -4;
	}
	i16 need = 1 + _varu_size_((u16) len) + len;
	if(need > (i16) m->length - (i16) m->cursor) {
		return -2;
	}
	_w8_(m, (u8) tag);
	_write_varu_(m, (u16) len);
	fast_memcpy((u8 *) m->data + m->cursor, (u8 *) buf, (u16) len);
	m->cursor += len; // raw copy of bytes
	return need;
}

i64 mirage_write_fn(mirage_msg *m, char *name, i64 nlen, i64 argc) {
	if(argc > INT16_MAX || nlen > INT16_MAX || argc < 0 || nlen < -1) {
		return -4;
	}
	if(nlen == -1) {
		nlen = _strlen_(name);
	}
	i16 s = m->cursor;
	i16 need = 1 + _varu_size_((u16) argc) + 1 + _varu_size_((u16) nlen) + nlen;
	if(need > (i16) m->length - (i16) m->cursor) {
		return -2;
	}
	_w8_(m, 'f');
	_write_varu_(m, (u16) argc);
	_write_buf_(m, (i8 *) name, nlen, 's');
	return m->cursor - s;
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
	_w16le_(m, (u16) v);
	return 3;
}

i64 mirage_write_i32(mirage_msg *m, i32 v) {
	if(!_need_(m, 5)) {
		return -2;
	}
	_w8_(m, 'i');
	_w32le_(m, (u32) v);
	return 5;
}

i64 mirage_write_i64(mirage_msg *m, i64 v) {
	if(!_need_(m, 9)) {
		return -2;
	}
	_w8_(m, 'L');
	_w64le_(m, (u64) v);
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
	_w64le_(m, u.i);
	return 9;
}

i64 mirage_write_string(mirage_msg *m, char *v, i64 l) {
	if(l == -1) {
		l = _strlen_(v);
	}
	return _write_buf_(m, (i8 *) v, l, 'S');
}

i64 mirage_write_bin(mirage_msg *m, u8 *d, i64 l) {
	return _write_buf_(m, (i8 *) d, l, 'B');
}

i64 mirage_write_sym(mirage_msg *m, char *n, i64 l) {
	if(l == -1) {
		l = _strlen_(n);
	}
	return _write_buf_(m, (i8 *) n, l, 's');
}

i64 mirage_write_bigint(mirage_msg *m, char *v, i64 l) {
	return _write_buf_(m, (i8 *) v, l, 'I');
}

i64 mirage_write_bigreal(mirage_msg *m, char *v, i64 l) {
	return _write_buf_(m, (i8 *) v, l, 'R');
}

i64 mirage_write_numericarray(mirage_msg *m, mirage_array_type t, i64 rank, i64 *dims, void *data) {
	if(rank < 0 || rank > INT16_MAX) {
		return -4;
	}
	i16 elems = 1;
	i16 dv = 0;
	for(i16 i = 0; i < rank; ++i) {
		i16 new_elems;
		if(dims[i] > INT16_MAX || dims[0] < 0 || __builtin_mul_overflow(elems, (i16) dims[i], &new_elems)) {
			return -4;
		}
		elems = new_elems;
		dv += _varu_size_((u16) dims[i]);
	}
	i16 bytes;
	if(__builtin_mul_overflow(elems, mirage_array_type_size[t], &bytes)) {
		return -4;
	}
	i64 need = 1 + 1 + _varu_size_((u16) rank) + dv + bytes;
	if(need > m->length - m->cursor) {
		return -2;
	}

	_w8_(m, 0xC2);
	_w8_(m, mirage_array_type_encoding[t]);
	_write_varu_(m, (u16) rank);
	for(i16 i = 0; i < rank; ++i) {
		_write_varu_(m, (u16) dims[i]);
	}

	// Write payload as little-endian
	switch(t) {
	case MIRAGE_ARRAY_I8:
	case MIRAGE_ARRAY_U8: {
		fast_memcpy((u8 *) m->data + m->cursor, (u8 *) data, (u16) bytes);
		m->cursor += bytes;
		break;
	}
	case MIRAGE_ARRAY_I16:
	case MIRAGE_ARRAY_U16: {
		u16 *p = (u16 *) data;
		for(i64 i = 0; i < elems; ++i) {
			_w16le_(m, p[i]);
		}
		break;
	}
	case MIRAGE_ARRAY_C32:
		elems *= 2; /*fallthrough*/
	case MIRAGE_ARRAY_I32:
	case MIRAGE_ARRAY_U32:
	case MIRAGE_ARRAY_F32: {
		u32 *p = (u32 *) data;
		for(i64 i = 0; i < elems; ++i) {
			_w32le_(m, p[i]);
		}
		break;
	}
	case MIRAGE_ARRAY_C64:
		elems *= 2; /*fallthrough*/
	case MIRAGE_ARRAY_I64:
	case MIRAGE_ARRAY_U64:
	case MIRAGE_ARRAY_F64: {
		u64 *p = (u64 *) data;
		for(i64 i = 0; i < elems; ++i) {
			_w64le_(m, p[i]);
		}
		break;
	}
	default:
		return -4;
	}
	return need;
}

i64 mirage_write_association(mirage_msg *m, i64 len) {
	i16 need = 1 + _varu_size_(len);
	if(!_need_(m, need)) {
		return -2;
	}
	_w8_(m, 'A');
	_write_varu_(m, (u64) len);
	return 1 + need;
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
	if(m->data[0] == '8' && m->data[1] == ':') {
		m->cursor = 2;
		return 2;
	}
	// if (m->data[0] == '8' && m->data[1] == 'C'){
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
	if(nmax > INT16_MAX) {
		return -4;
	}
	m->cursor++;
	u16 a = 0;
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
	u16 L = 0;
	if(_read_varu_(m, &L) < 0) {
		m->cursor = s;
		return -2;
	}
	if(L > (u16) nmax || L > (u16) m->length - (u16) m->cursor) {
		m->cursor = s;
		return -4;
	}
	if(nmax > 0) {
		fast_memcpy((u8 *) name, (u8 *) m->data + m->cursor, L);
	}
	m->cursor += (i16) L;
	*nlen = (i16) L;
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
	_r16le_(m, (u16 *) v);
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
	_r32le_(m, (u32 *) v);
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
	_r64le_(m, (u64 *) v);
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
	_r64le_(m, &u.i);
	*v = u.d;
	return 9;
}

static i64 _read_buf_(mirage_msg *m, i8 *dst, i64 *len, i64 max, i8 tag) {
	i64 s = m->cursor;
	if(m->cursor >= m->length) {
		return -2;
	}
	if(m->data[m->cursor] != tag) {
		return -1;
	}
	m->cursor++;
	u16 L = 0;
	if(_read_varu_(m, &L) < 0) {
		m->cursor = s;
		return -2;
	}
	if(L > max || L > (u16) m->length - (u16) m->cursor) {
		m->cursor = s;
		return -1;
	}
	if(max > 0) {
		fast_memcpy((u8 *) dst, (u8 *) m->data + m->cursor, L);
	}
	m->cursor += (i16) L;
	*len = (i16) L;
	return m->cursor - s;
}

i64 mirage_read_string(mirage_msg *m, char *dst, i64 *l, i64 max) {
	return _read_buf_(m, (i8 *) dst, l, max, 'S');
}

i64 mirage_read_bin(mirage_msg *m, u8 *dst, i64 *l, i64 max) {
	return _read_buf_(m, (i8 *) dst, l, max, 'B');
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
			     i64 *dims, i64 max_rank, void *data, i64 max_bytes) {
	i8 skip = 0;
	if(max_rank == -1 || max_bytes == -1) {
		skip = 1;
	} else {
		if(!rank || !data || max_rank < -1 || max_bytes < -1) {
			return -4;
		}
	}

	// Validate inputs are within i16 range
	if(m->cursor > 32766 || m->length > 32766 || max_rank > 32766
	   || max_bytes > INT16_MAX) {
		return -4;
	}

	i16 length = (i16) m->length;
	i16 rmax = (i16) max_rank;
	i16 bmax = (i16) max_bytes;
	i16 s = (i16) m->cursor;

	if(s >= length) {
		return -2;
	}

	u8 tag = (u8) m->data[m->cursor++];
	if(tag != 0xC2 && tag != 0xC1) {
		m->cursor = s;
		return -1;
	}

	if((i16) m->cursor >= length) {
		m->cursor = s;
		return -2;
	}

	// Parse array type encoding
	u8 enc = (u8) m->data[m->cursor++];
	int found = 0;
	for(i8 i = 0; i < 12; ++i) {
		if(enc == mirage_array_type_encoding[i]) {
			*t = (mirage_array_type) i;
			found = 1;
			break;
		}
	}
	if(!found) {
		m->cursor = s;
		return -1;
	}

	i16 elem_size = (i16) mirage_array_type_size[*t];

	// Parse rank
	u16 r = 0;
	if(_read_varu_(m, &r) < 0) {
		m->cursor = s;
		return -2;
	}
	if(r > (u16) rmax) {
		m->cursor = s;
		return -4;
	}
	*rank = (i64) r;
	i16 r16 = (i16) r;

	// Parse dimensions and compute element count
	i16 elems = 1;

	// rank 0 tensor is scalar (1 element)

	for(i16 i = 0; i < r16; ++i) {
		u16 d = 0;
		if(_read_varu_(m, &d) < 0) {
			m->cursor = s;
			return -2;
		}
		if(!skip) {
			dims[i] = (i64) d;
		}

		i16 new_elems;
		if(__builtin_mul_overflow(elems, (i16) d, &new_elems)) {
			m->cursor = s;
			return -1;
		}
		elems = new_elems;
	}

	// Compute total bytes needed
	i16 bytes;
	if(__builtin_mul_overflow(elems, elem_size, &bytes)) {
		m->cursor = s;
		return -1;
	}

	if(bytes > bmax || bytes > (i16) m->length - (i16) m->cursor) {
		m->cursor = s;
		return -2;
	}

	// Read array data based on type
	switch(*t) {
	case MIRAGE_ARRAY_I8:
	case MIRAGE_ARRAY_U8:
		if(!skip) {
			fast_memcpy((u8 *) data, (u8 *) m->data + m->cursor, (u16) bytes);
		}
		m->cursor += bytes;
		break;

	case MIRAGE_ARRAY_I16:
	case MIRAGE_ARRAY_U16: {
		u16 *p = (u16 *) data;
		for(i16 i = 0; i < elems; ++i) {
			u16 *idx = skip ? 0 : p + i;
			_r16le_(m, idx);
		}
		break;
	}

	case MIRAGE_ARRAY_C32:
		elems *= 2;
		/* fallthrough */
	case MIRAGE_ARRAY_I32:
	case MIRAGE_ARRAY_U32:
	case MIRAGE_ARRAY_F32: {
		u32 *p = (u32 *) data;
		for(i16 i = 0; i < elems; ++i) {
			u32 *idx = skip ? 0 : p + i;
			_r32le_(m, idx);
		}
		break;
	}

	case MIRAGE_ARRAY_C64:
		elems *= 2;
		/* fallthrough */
	case MIRAGE_ARRAY_I64:
	case MIRAGE_ARRAY_U64:
	case MIRAGE_ARRAY_F64: {
		u64 *p = (u64 *) data;
		for(i16 i = 0; i < elems; ++i) {
			u64 *idx = skip ? 0 : p + i;
			_r64le_(m, idx);
		}
		break;
	}

	default:
		m->cursor = s;
		return -1;
	}

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
	u16 L = 0;
	if(_read_varu_(m, &L) < 0) {
		m->cursor = s;
		return -2;
	}
	*len = (i16) L;
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
// better size
i64 mirage_peek_element_type(mirage_msg *m, mirage_element_type *e) {
	if(m->cursor >= m->length) {
		return -2;
	}
	u8 enc = (u8) m->data[m->cursor];
	for(i8 i = 0; i < 16; ++i) {
		if(enc == mirage_element_type_encoding[i]) {
			*e = (mirage_element_type) i;
			return 0;
		}
	}
	return -1;
}
