#ifndef SHIM_STRING_H
#define SHIM_STRING_H
#include <stddef.h>
void *memset(void*, int, unsigned long);
void *memcpy(void*, const void*, unsigned long);
void *memmove(void*, const void*, unsigned long);
#endif
