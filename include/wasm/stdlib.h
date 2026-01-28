/* WASM stub - malloc/free provided by mirage_wasm.c */
#ifndef _WASM_STDLIB_H
#define _WASM_STDLIB_H

#include <stddef.h>

void *malloc(size_t size);
void free(void *ptr);

#endif
