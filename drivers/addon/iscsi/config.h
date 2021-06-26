/*
 * We need to determine byte ordering for the md5 code
 */
#ifndef BROKEN_CONFIG_H
#define BROKEN_CONFIG_H

#include <asm/byteorder.h>
#ifdef __BIG_ENDIAN
#define WORDS_BIGENDIAN
#endif

typedef u32 UWORD32;
typedef u32 UINT32;
typedef u8 UINT8;
typedef u16 UINT16;
#endif
