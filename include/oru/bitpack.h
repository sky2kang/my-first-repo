/* SPDX-License-Identifier: MIT */
/*
 * MSB-first bit packer over a byte buffer.
 *
 * Shared by the U-plane compressors (BFP, µ-law) which both serialise
 * arbitrary-width mantissas/codewords into a packed bitstream.
 */
#ifndef ORU_BITPACK_H
#define ORU_BITPACK_H

#include "oru/types.h"

typedef struct {
    uint8_t *buf;     /* backing storage (mutable for write, casted for read) */
    size_t   len;     /* capacity in bytes            */
    size_t   bitpos;  /* current read/write position  */
} bitstream_t;

/* Initialise a stream over [buf, buf+len). bitpos starts at 0. */
static inline void bs_init(bitstream_t *bs, uint8_t *buf, size_t len)
{
    bs->buf = buf;
    bs->len = len;
    bs->bitpos = 0;
}

/* Write the low `width` bits of `val`, MSB first. The caller must ensure
 * the destination bytes are zero-initialised. Returns false on overflow. */
static inline bool bs_put(bitstream_t *bs, uint32_t val, unsigned width)
{
    if (bs->bitpos + width > bs->len * 8u)
        return false;
    for (unsigned i = 0; i < width; i++) {
        unsigned bit = (val >> (width - 1u - i)) & 1u;
        size_t pos = bs->bitpos + i;
        if (bit)
            bs->buf[pos >> 3] |= (uint8_t)(0x80u >> (pos & 7u));
    }
    bs->bitpos += width;
    return true;
}

/* Read `width` bits, MSB first, into *out. Returns false on underflow. */
static inline bool bs_get(bitstream_t *bs, unsigned width, uint32_t *out)
{
    if (bs->bitpos + width > bs->len * 8u)
        return false;
    uint32_t v = 0;
    for (unsigned i = 0; i < width; i++) {
        size_t pos = bs->bitpos + i;
        unsigned bit = (bs->buf[pos >> 3] >> (7u - (pos & 7u))) & 1u;
        v = (v << 1) | bit;
    }
    bs->bitpos += width;
    *out = v;
    return true;
}

#endif /* ORU_BITPACK_H */
