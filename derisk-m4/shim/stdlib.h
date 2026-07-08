#ifndef SHIM_STDLIB_H
#define SHIM_STDLIB_H
#ifndef NULL
#define NULL ((void*)0)
#endif
typedef unsigned long size_t;
void *malloc(unsigned long);
void free(void*);
#endif
