/* SPDX-License-Identifier: GPL-3.0-only */
#include <stddef.h>

void *memset(void *destination, int value, size_t length)
{
    unsigned char *p = destination;
    while (length--) *p++ = (unsigned char)value;
    return destination;
}
void *memcpy(void *destination, const void *source, size_t length)
{
    unsigned char *p = destination;
    const unsigned char *q = source;
    while (length--) *p++ = *q++;
    return destination;
}
