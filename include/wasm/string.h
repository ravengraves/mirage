/* WASM stub - string functions provided by mirage_wasm.c */
#ifndef _WASM_STRING_H
#define _WASM_STRING_H

#include <stddef.h>

size_t strlen(const char *s);
void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);

#endif
