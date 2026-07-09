#ifndef SHIM_STRING_H
#define SHIM_STRING_H
typedef unsigned long size_t;
void *memset(void*, int, unsigned long);
void *memcpy(void*, const void*, unsigned long);
void *memmove(void*, const void*, unsigned long);
#endif
