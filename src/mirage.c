// mirage.c — Little-endian host implementation.
#include "mirage.h"
#include "mirage_style.h"

#include <stdlib.h> // malloc/free
#include <string.h>

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
					     'S', 'B', 's', 'I', 'R', 0xC1, 0xC2, 'A', ':', '-'};
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

// Portable checked multiply for i64 values
// Returns 1 if overflow, 0 if safe (result stored in *out)
static inline int checked_mul_i64(i64 a, i64 b, i64 *out) {
#if defined(__GNUC__) || defined(__clang__)
	return __builtin_mul_overflow(a, b, out);
#else
	// Portable overflow check for all cases
	if(a == 0 || b == 0) {
		*out = 0;
		return 0;
	}

	// Check for overflow in various combinations
	if(a > 0 && b > 0 && a > INT64_MAX / b) {
		return 1;
	}
	if(a > 0 && b < 0 && b < INT64_MIN / a) {
		return 1;
	}
	if(a < 0 && b > 0 && a < INT64_MIN / b) {
		return 1;
	}
	if(a < 0 && b < 0 && a < INT64_MAX / b) {
		return 1;
	}

	*out = a * b;
	return 0;
#endif
}

// memcpy(dest, src, bytes);
static inline void fast_memcpy(u8 *RESTRICT dest, const u8 *RESTRICT src, size_t n) {
	// Advance byte-by-byte until dest is 8-byte aligned (or n exhausted)
	while(n && ((uintptr_t) dest & 7)) {
		*dest++ = *src++;
		--n;
	}
	// Both pointers aligned: fast 64-byte unrolled copy
	if(!((uintptr_t) src & 7)) {
		u64 *dw = (u64 *) dest;
		const u64 *sw = (const u64 *) src;
		if(n >= 32768) {
			// Large copy: prefetch to hide L2/L3/RAM latency
			while(n >= 64) {
				__builtin_prefetch(sw + 16, 0, 1);
				__builtin_prefetch(dw + 16, 1, 1);
				dw[0] = sw[0]; dw[1] = sw[1]; dw[2] = sw[2]; dw[3] = sw[3];
				dw[4] = sw[4]; dw[5] = sw[5]; dw[6] = sw[6]; dw[7] = sw[7];
				dw += 8; sw += 8; n -= 64;
			}
		} else {
			// L1-resident: skip prefetch
			while(n >= 64) {
				dw[0] = sw[0]; dw[1] = sw[1]; dw[2] = sw[2]; dw[3] = sw[3];
				dw[4] = sw[4]; dw[5] = sw[5]; dw[6] = sw[6]; dw[7] = sw[7];
				dw += 8; sw += 8; n -= 64;
			}
		}
		while(n >= 32) {
			dw[0] = sw[0]; dw[1] = sw[1]; dw[2] = sw[2]; dw[3] = sw[3];
			dw += 4; sw += 4; n -= 32;
		}
		while(n >= 8) {
			*dw++ = *sw++;
			n -= 8;
		}
		dest = (u8 *) dw;
		src = (const u8 *) sw;
	}
	// Byte remainder (or entire copy if src was unaligned)
	while(n--) {
		*dest++ = *src++;
	}
}

static inline int _validate_msg_(const mirage_msg *m) {
	if(!m) {
		return 0;
	}
	if(m->max_length < 0 || m->length < 0 || m->cursor < 0) {
		return 0;
	}
	if(m->length > m->max_length) {
		return 0;
	}
	if(m->cursor > m->length) {
		return 0;
	}
	return 1;
}

static inline int _need_(mirage_msg *m, i64 n) {
	if(!_validate_msg_(m) || n < 0) {
		return 0;
	}
	return (m->cursor + n <= m->max_length);
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
	m->length = m->cursor;
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
static inline void _w8_(mirage_msg *m, u8 v) {
	m->data[m->cursor++] = (i8) v;
	m->length = m->cursor;
}
static inline void _w16_(mirage_msg *m, u16 v) {
	*(u16 *) (m->data + m->cursor) = v;
	m->cursor += 2;
	m->length = m->cursor;
}
static inline void _w32_(mirage_msg *m, u32 v) {
	*(u32 *) (m->data + m->cursor) = v;
	m->cursor += 4;
	m->length = m->cursor;
}
static inline void _w64_(mirage_msg *m, u64 v) {
	*(u64 *) (m->data + m->cursor) = v;
	m->cursor += 8;
	m->length = m->cursor;
}
static inline void _r16_(mirage_msg *m, u16 *v) {
	if(v) {
		*v = *(u16 *) (m->data + m->cursor);
	}
	m->cursor += 2;
}
static inline void _r32_(mirage_msg *m, u32 *v) {
	if(v) {
		*v = *(u32 *) (m->data + m->cursor);
	}
	m->cursor += 4;
}
static inline void _r64_(mirage_msg *m, u64 *v) {
	if(v) {
		*v = *(u64 *) (m->data + m->cursor);
	}
	m->cursor += 8;
}

// ---- Create/Destroy ---------------------------------------------------------
void mirage_init(mirage_msg *m, u8 *buffer, i64 buffer_size) {
	if(!m || !buffer || buffer_size <= 0) {
		return;
	}
	m->max_length = buffer_size;
	m->length = 0;
	m->cursor = 0;
	m->data = buffer;
}

mirage_msg *mirage_create(i64 buffer_size, void *(*alloc)(size_t)) {
	// Validate buffer size is positive
	if(buffer_size <= 0) {
		return 0;
	}

	// Check for overflow: buffer_size + sizeof(mirage_msg) must fit in size_t
	// Use unsigned comparison to avoid signed/unsigned issues
	u64 buf_u = (u64) buffer_size;
	if(buf_u > SIZE_MAX - sizeof(mirage_msg)) {
		return 0;
	}

	void *(*allocate)(size_t) = alloc ? alloc : malloc;
	size_t total_size = sizeof(mirage_msg) + (size_t) buf_u;

	mirage_msg *m = (mirage_msg *) allocate(total_size);
	if(!m) {
		return 0;
	}
	m->max_length = buffer_size;
	m->length = buffer_size;
	m->cursor = 0;
	m->data = (u8 *) m + sizeof(mirage_msg);
	mirage_write_start(m);
	return m;
}

void mirage_destroy(mirage_msg **m, void (*dealloc)(void *)) {
	if(!m) {
		return; // Guard against NULL pointer
	}

	void (*deallocate)(void *) = dealloc ? dealloc : free;
	if(*m) {
		deallocate(*m);
		*m = 0; // Clear pointer after free
	}
}

// ---- Write ------------------------------------------------------------------
i64 mirage_write_start(mirage_msg *m) {
	if(!_validate_msg_(m)) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(m->max_length < 2) {
		return -1;
	}
	m->data[0] = '8';
	m->data[1] = ':';
	m->cursor = 2;
	m->length = 2;
	return 2;
}

i64 mirage_reset(mirage_msg *m) {
	if(!_validate_msg_(m)) {
		return MIRAGE_ERR_BADINPUT;
	}
	m->cursor = 0;
	m->length = 0;
	return 0;
}

i64 mirage_remaining(const mirage_msg *m) {
	if(!_validate_msg_(m)) {
		return 0;
	}
	return m->max_length - m->cursor;
}
static inline i64 _write_buf_(mirage_msg *m, const i8 *buf, i64 len, i8 tag) {
	if(!_validate_msg_(m) || len < 0) {
		return MIRAGE_ERR_BADINPUT;
	}
	i64 need = 1 + _varu_size_((u64) len) + len;
	if(need > m->max_length - m->cursor) {
		return -2;
	}
	_w8_(m, (u8) tag);
	_write_varu_(m, (u64) len);
	fast_memcpy((u8 *) m->data + m->cursor, (const u8 *) buf, (size_t) len);
	m->cursor += len;
	m->length = m->cursor;
	return need;
}
i64 mirage_write_fn(mirage_msg *m, const char *name, i64 nlen, i64 argc) {
	if(!_validate_msg_(m) || !name) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(argc < 0 || nlen < -1) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(nlen == -1) {
		nlen = strlen(name);
	}
	i64 start = m->cursor;
	i64 worst = 1 + _varu_size_((u64) argc) + 1 + _varu_size_((u64) nlen) + nlen;
	if(worst > m->max_length - m->cursor) {
		return -2;
	}
	_w8_(m, 'f');
	_write_varu_(m, (u64) argc);
	_write_buf_(m, (const i8 *) name, nlen, 's');
	return m->cursor - start;
}
i64 mirage_write_i8(mirage_msg *m, i8 v) {
	if(!m) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(!_need_(m, 2)) {
		return -2;
	}
	_w8_(m, 'C');
	_w8_(m, (u8) v);
	return 2;
}
i64 mirage_write_i16(mirage_msg *m, i16 v) {
	if(!m) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(!_need_(m, 3)) {
		return -2;
	}
	_w8_(m, 'j');
	_w16_(m, (u16) v);
	return 3;
}
i64 mirage_write_i32(mirage_msg *m, i32 v) {
	if(!m) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(!_need_(m, 5)) {
		return -2;
	}
	_w8_(m, 'i');
	_w32_(m, (u32) v);
	return 5;
}
i64 mirage_write_i64(mirage_msg *m, i64 v) {
	if(!m) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(!_need_(m, 9)) {
		return -2;
	}
	_w8_(m, 'L');
	_w64_(m, (u64) v);
	return 9;
}
i64 mirage_write_f64(mirage_msg *m, f64 v) {
	if(!m) {
		return MIRAGE_ERR_BADINPUT;
	}
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
i64 mirage_write_string(mirage_msg *m, const char *v, i64 l) {
	if(!m || !v) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(l == -1) {
		l = strlen(v);
	}
	return _write_buf_(m, (const i8 *) v, l, 'S');
}
i64 mirage_write_bin(mirage_msg *m, const u8 *d, i64 l) {
	if(!m || !d) {
		return MIRAGE_ERR_BADINPUT;
	}
	return _write_buf_(m, (const i8 *) d, l, 'B');
}
i64 mirage_write_sym(mirage_msg *m, const char *n, i64 l) {
	if(!m || !n) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(l == -1) {
		l = strlen(n);
	}
	return _write_buf_(m, (const i8 *) n, l, 's');
}
i64 mirage_write_bigint(mirage_msg *m, const char *v, i64 l) {
	if(!m || !v) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(l == -1) {
		l = strlen(v);
	}
	return _write_buf_(m, (const i8 *) v, l, 'I');
}
i64 mirage_write_bigreal(mirage_msg *m, const char *v, i64 l) {
	if(!m || !v) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(l == -1) {
		l = strlen(v);
	}
	return _write_buf_(m, (const i8 *) v, l, 'R');
}

i64 mirage_write_numericarray(mirage_msg *m, mirage_array_type t, i64 rank,
			      const i64 *dims, const void *data) {
	if(!_validate_msg_(m) || !data) {
		return MIRAGE_ERR_BADINPUT;
	}
	if((int) t < 0 || (int) t >= 12) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(rank < 0) {
		return -4;
	}
	if(rank > 0 && !dims) {
		return MIRAGE_ERR_BADINPUT; // dims required when rank > 0
	}
	i64 elems = 1;
	i64 dv = 0;
	for(i64 i = 0; i < rank; ++i) {
		i64 new_elems;
		if(dims[i] < 0 || checked_mul_i64(elems, dims[i], &new_elems)) {
			return -4;
		}
		elems = new_elems;
		dv += _varu_size_((u64) dims[i]);
	}
	i64 bytes;
	if(checked_mul_i64(elems, mirage_array_type_size[t], &bytes)) {
		return -4;
	}
	i64 need = 1 + 1 + _varu_size_((u64) rank) + dv + bytes;
	if(need > m->max_length - m->cursor) {
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
	m->length = m->cursor;
	return need;
}

i64 mirage_write_association(mirage_msg *m, i64 len) {
	if(!_validate_msg_(m) || len < 0) {
		return MIRAGE_ERR_BADINPUT;
	}
	i64 need = 1 + _varu_size_(len);
	if(!_need_(m, need)) {
		return -2;
	}
	_w8_(m, 'A');
	_write_varu_(m, (u64) len);
	return 1 + need;
}

i64 mirage_write_assoc_rule(mirage_msg *m) {
	if(!m) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(!_need_(m, 1)) {
		return -2;
	}
	_w8_(m, '-');
	return 1;
}

// ---- Read -------------------------------------------------------------------
i64 mirage_read_start(mirage_msg *m) {
	if(!_validate_msg_(m)) {
		return MIRAGE_ERR_BADINPUT;
	}
	m->cursor = 0;
	if(m->cursor + 2 > m->length) {
		return -2;
	}
	if(m->data[0] == '8' && m->data[1] == ':') {
		m->cursor = 2;
		return 2;
	}
	// if (m->data[0] == '8' && m->data[1] == 'C') {
	// 	return -10; // compressed (zlib) not implemented
	// }
	return -1;
}

i64 mirage_read_fn(mirage_msg *m, char *name, i64 *nlen, i64 nmax, i64 *argc) {
	if(!_validate_msg_(m) || !nlen) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(nmax < 0) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(nmax > 0 && !name) {
		return MIRAGE_ERR_BADINPUT;
	}
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
	if(argc != NULL) {
		*argc = (i64) a;
	}
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
	if((nmax && (i64) L > nmax) || (i64) L > m->length - m->cursor) {
		m->cursor = s;
		return -4;
	}
	if(nmax > 0) {
		fast_memcpy((u8 *) name, (u8 *) m->data + m->cursor, (size_t) L);
	}
	m->cursor += (i64) L;
	*nlen = (i64) L;
	return m->cursor - s;
}
i64 mirage_read_i8(mirage_msg *m, i8 *v) {
	if(!_validate_msg_(m) || !v) {
		return MIRAGE_ERR_BADINPUT;
	}
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
	if(!_validate_msg_(m) || !v) {
		return MIRAGE_ERR_BADINPUT;
	}
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
	if(!_validate_msg_(m) || !v) {
		return MIRAGE_ERR_BADINPUT;
	}
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
	if(!_validate_msg_(m) || !v) {
		return MIRAGE_ERR_BADINPUT;
	}
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
	if(!_validate_msg_(m) || !v) {
		return MIRAGE_ERR_BADINPUT;
	}
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
	if(!_validate_msg_(m)) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(max < -1) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(max == -1) {
		max = 0;
	}
	if(max > 0 && !dst) {
		return MIRAGE_ERR_BADINPUT;
	}
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
	if((max && (i64) L > max) || (i64) L > m->length - m->cursor) {
		m->cursor = s;
		return -1;
	}
	if(max > 0) {
		fast_memcpy((u8 *) dst, (u8 *) m->data + m->cursor, (size_t) L);
	}
	m->cursor += (i64) L;
	*len = (i64) L;
	return m->cursor - s;
}
i64 mirage_read_string(mirage_msg *m, char *dst, i64 *l, i64 max) {
	if(!m || !l) {
		return MIRAGE_ERR_BADINPUT;
	}
	return _read_buf_(m, (i8 *) dst, l, max, 'S');
}
i64 mirage_read_bin(mirage_msg *m, u8 *dst, i64 *l, i64 max) {
	if(!m || !l) {
		return MIRAGE_ERR_BADINPUT;
	}
	return _read_buf_(m, (i8 *) dst, l, max, 'B');
}
i64 mirage_read_sym(mirage_msg *m, char *dst, i64 *l, i64 max) {
	if(!m || !l) {
		return MIRAGE_ERR_BADINPUT;
	}
	return _read_buf_(m, (i8 *) dst, l, max, 's');
}
i64 mirage_read_bigint(mirage_msg *m, char *dst, i64 *l, i64 max) {
	if(!m || !l) {
		return MIRAGE_ERR_BADINPUT;
	}
	return _read_buf_(m, (i8 *) dst, l, max, 'I');
}
i64 mirage_read_bigreal(mirage_msg *m, char *dst, i64 *l, i64 max) {
	if(!m || !l) {
		return MIRAGE_ERR_BADINPUT;
	}
	return _read_buf_(m, (i8 *) dst, l, max, 'R');
}

i64 mirage_read_numericarray(mirage_msg *m, mirage_array_type *t, i64 *rank,
			     i64 *dims, i64 max_rank, void *data,
			     i64 max_bytes) {
	if(!_validate_msg_(m) || !t || !rank) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(max_rank < 0 || max_bytes < -1) {
		return MIRAGE_ERR_BADINPUT;
	}
	i64 skip = (max_bytes == -1);
	// dims can be NULL if max_rank is 0 (scalar), data can be NULL if max_bytes is -1
	if(!skip && !data) {
		return -4;
	}
	if(max_rank > 0 && !dims) {
		return -4; // dims required when max_rank > 0
	}
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
	if(!data && !skip) {
		m->cursor = s;
		return -4;
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
		if(dims) {
			dims[i] = (i64) d;
		}

		i64 new_elems;
		if(checked_mul_i64(elems, (i64) d, &new_elems)) {
			m->cursor = s;
			return -1;
		}
		elems = new_elems;
	}

	// compute bytes
	i64 bytes;
	if(checked_mul_i64(elems, (i64) mirage_array_type_size[*t], &bytes)) {
		m->cursor = s;
		return -1;
	}
	if((!skip && bytes > max_bytes) || bytes > m->length - m->cursor) {
		m->cursor = s;
		return -2;
	}
	if(!skip) {
		fast_memcpy((u8 *) data, (u8 *) m->data + m->cursor, (size_t) bytes);
	}
	m->cursor += bytes;
	return m->cursor - s;
}
i64 mirage_read_association(mirage_msg *m, i64 *len) {
	if(!_validate_msg_(m) || !len) {
		return MIRAGE_ERR_BADINPUT;
	}
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
	if(!_validate_msg_(m)) {
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
	if(!_validate_msg_(m) || !e) {
		return MIRAGE_ERR_BADINPUT;
	}
	if(m->cursor >= m->length) {
		return -2;
	}
	u8 enc = (u8) m->data[m->cursor];
	u8 res = _mirage_keymap_[enc];
	if(res == 0 && enc != 'f') {
		return -1;
	}
	*e = res;
	return 1; // Return bytes inspected instead of 0
}
