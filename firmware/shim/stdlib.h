#ifndef SHIM_STDLIB_H
#define SHIM_STDLIB_H
#include <stddef.h>
void *malloc(size_t);
void *realloc(void *, size_t);
void free(void *);
void abort(void);
int abs(int);
long labs(long);
#endif
