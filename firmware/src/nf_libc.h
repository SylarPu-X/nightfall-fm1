#ifndef NF_LIBC_H
#define NF_LIBC_H
#include <stddef.h>
void *memset(void *destination, int value, size_t length);
void *memcpy(void *destination, const void *source, size_t length);
#endif
