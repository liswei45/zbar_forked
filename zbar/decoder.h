/*------------------------------------------------------------------------
 *  Copyright 2007-2009 (c) Jeff Brown <spadix@users.sourceforge.net>
 *
 *  This file is part of the ZBar Bar Code Reader.
 *
 *  The ZBar Bar Code Reader is free software; you can redistribute it
 *  and/or modify it under the terms of the GNU Lesser Public License as
 *  published by the Free Software Foundation; either version 2.1 of
 *  the License, or (at your option) any later version.
 *
 *  The ZBar Bar Code Reader is distributed in the hope that it will be
 *  useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 *  of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser Public License
 *  along with the ZBar Bar Code Reader; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 *  Boston, MA  02110-1301  USA
 *
 *  http://sourceforge.net/projects/zbar
 *------------------------------------------------------------------------*/
#ifndef _DECODER_H_
#define _DECODER_H_

#include <config.h>
#include <stdlib.h>     /* realloc */

#include <zbar.h>

#define NUM_CFGS (ZBAR_CFG_MAX_LEN - ZBAR_CFG_MIN_LEN + 1)

#ifdef ENABLE_EAN
# include "decoder/ean.h"
#endif
#ifdef ENABLE_I25
# include "decoder/i25.h"
#endif
#ifdef ENABLE_CODE39
# include "decoder/code39.h"
#endif
#ifdef ENABLE_CODE128
# include "decoder/code128.h"
#endif
#ifdef ENABLE_PDF417
# include "decoder/pdf417.h"
#endif
#ifdef ENABLE_QRCODE
# include "decoder/qr_finder.h"
#endif

/* size of bar width history (implementation assumes power of two) */
#ifndef DECODE_WINDOW
# define DECODE_WINDOW  16
#endif

/* initial data buffer allocation */
#ifndef BUFFER_MIN
# define BUFFER_MIN   0x20
#endif

/* maximum data buffer allocation
 * (longer symbols are rejected)
 */
#ifndef BUFFER_MAX
# define BUFFER_MAX  0x100
#endif

/* buffer allocation increment */
#ifndef BUFFER_INCR
# define BUFFER_INCR  0x10
#endif

#define CFG(dcode, cfg) ((dcode).configs[(cfg) - ZBAR_CFG_MIN_LEN])
#define TEST_CFG(config, cfg) (((config) >> (cfg)) & 1)

/* symbology independent decoder state */
// 符号独立的解码器状态 不明白是什么意思?
struct zbar_decoder_s {
    unsigned char idx;                  /* current width index 当前像素的宽索引*/
    unsigned w[DECODE_WINDOW];          /* window of last N bar widths */
    zbar_symbol_type_t type;            /* type of last decoded data */
                                        // 上一个解码的类型
                                        // 猜测: 正常情况下，在同一张图片中这个值是不应该改变的
                                        // 因为一个码的图片就只能是一种吗. 这里的上一个说明:
                                        // 程序通过一个小的window，结合边缘在猜测究竟是哪一种码
                                        // 通过多个边缘的相关信息来猜出最终是什么类型的码

    zbar_symbol_type_t lock;            /* buffer lock */
                                        // 这个和内存操作有关? 
                                        
    /* everything above here is automatically reset */
    unsigned buf_alloc;                 /* dynamic buffer allocation */
    unsigned buflen;                    /* binary data length */
    unsigned char *buf;                 /* decoded characters */
    void *userdata;                     /* application data */
    zbar_decoder_handler_t *handler;    /* application callback */

    /* symbology specific state */
#ifdef ENABLE_EAN
    ean_decoder_t ean;                  /* EAN/UPC parallel decode attempts */
#endif
#ifdef ENABLE_I25
    i25_decoder_t i25;                  /* Interleaved 2 of 5 decode state */
#endif
#ifdef ENABLE_CODE39
    code39_decoder_t code39;            /* Code 39 decode state */
#endif
#ifdef ENABLE_CODE128
    code128_decoder_t code128;          /* Code 128 decode state */
#endif
#ifdef ENABLE_PDF417
    pdf417_decoder_t pdf417;            /* PDF417 decode state */
#endif
#ifdef ENABLE_QRCODE
    qr_finder_t qrf;                    /* QR Code finder state */
#endif
};

/* return current element color */
static inline char get_color (const zbar_decoder_t *dcode)
{
    return(dcode->idx & 1);
}

/* retrieve i-th previous element width */
static inline unsigned get_width (const zbar_decoder_t *dcode,
                                  unsigned char offset)
{
    return(dcode->w[(dcode->idx - offset) & (DECODE_WINDOW - 1)]);
}

/* retrieve bar+space pair width starting at offset i */
static inline unsigned pair_width (const zbar_decoder_t *dcode,
                                   unsigned char offset)
{
    return(get_width(dcode, offset) + get_width(dcode, offset + 1));
}

/* calculate total character width "s"
 *   - start of character identified by context sensitive offset
 *     (<= DECODE_WINDOW - n)
 *   - size of character is n elements
 */
static inline unsigned calc_s (const zbar_decoder_t *dcode,
                               unsigned char offset,
                               unsigned char n)
{
    /* FIXME check that this gets unrolled for constant n */
    unsigned s = 0;
    while(n--)
        s += get_width(dcode, offset++);
    return(s);
}

/* fixed character width decode assist
 * bar+space width are compared as a fraction of the reference dimension "x"
 *   - +/- 1/2 x tolerance
 *   - measured total character width (s) compared to symbology baseline (n)
 *     (n = 7 for EAN/UPC, 11 for Code 128)
 *   - bar+space *pair width* "e" is used to factor out bad "exposures"
 *     ("blooming" or "swelling" of dark or light areas)
 *     => using like-edge measurements avoids these issues
 *   - n should be > 3
 */
static inline int decode_e (unsigned e,
                            unsigned s,
                            unsigned n)
{
    /* result is encoded number of units - 2
     * (for use as zero based index)
     * or -1 if invalid
     */
    unsigned char E = ((e * n * 2 + 1) / s - 3) / 2;
    return((E >= n - 3) ? -1 : E);
}

/* acquire shared state lock */
static inline char get_lock (zbar_decoder_t *dcode,
                             zbar_symbol_type_t req)
{
    if(dcode->lock)
        return(1);
    dcode->lock = req;
    return(0);
}

/* ensure output buffer has sufficient allocation for request */
static inline char size_buf (zbar_decoder_t *dcode,
                             unsigned len)
{
    if(len < dcode->buf_alloc)
        /* FIXME size reduction heuristic? */
        return(0);
    if(len > BUFFER_MAX)
        return(1);
    if(len < dcode->buf_alloc + BUFFER_INCR) {
        len = dcode->buf_alloc + BUFFER_INCR;
        if(len > BUFFER_MAX)
            len = BUFFER_MAX;
    }
    unsigned char *buf = realloc(dcode->buf, len);
    if(!buf)
        return(1);
    dcode->buf = buf;
    dcode->buf_alloc = len;
    return(0);
}

extern const char *_zbar_decoder_buf_dump (unsigned char *buf,
                                            unsigned int buflen);

#endif
