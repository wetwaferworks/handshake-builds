/* SPDX-License-Identifier: MIT */
#include "hs_png.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Uncompressed zlib+PNG writer. No libpng, no libz. */

static uint32_t crc_table[256];
static int crc_ready;

static void crc_init(void)
{
    uint32_t c;
    unsigned i, k;

    if (crc_ready) {
        return;
    }
    for (i = 0; i < 256; i++) {
        c = i;
        for (k = 0; k < 8; k++) {
            if (c & 1u) {
                c = 0xEDB88320u ^ (c >> 1);
            } else {
                c = c >> 1;
            }
        }
        crc_table[i] = c;
    }
    crc_ready = 1;
}

static uint32_t crc32(const uint8_t *data, size_t len, uint32_t crc)
{
    size_t i;

    crc ^= 0xFFFFFFFFu;
    for (i = 0; i < len; i++) {
        crc = crc_table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

static uint32_t adler32(const uint8_t *data, size_t len)
{
    uint32_t s1 = 1;
    uint32_t s2 = 0;
    size_t i;

    for (i = 0; i < len; i++) {
        s1 += data[i];
        if (s1 >= 65521u) {
            s1 -= 65521u;
        }
        s2 += s1;
        if (s2 >= 65521u) {
            s2 -= 65521u;
        }
    }
    return (s2 << 16) | s1;
}

static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static int write_chunk(FILE *fp, const char type[4], const uint8_t *data, size_t len)
{
    uint8_t header[8];
    uint8_t footer[4];
    uint32_t crc;
    uint8_t typeb[4];

    be32(header, (uint32_t)len);
    memcpy(header + 4, type, 4);
    memcpy(typeb, type, 4);
    crc = crc32(typeb, 4, 0);
    if (len > 0) {
        crc = crc32(data, len, crc ^ 0xFFFFFFFFu); /* continue? */
        /* crc32() always xors in/out, so we cannot chain that way. Recompute. */
    }
    {
        uint8_t *tmp = (uint8_t *)malloc(4 + len);
        if (tmp == NULL) {
            return -ENOMEM;
        }
        memcpy(tmp, type, 4);
        if (len > 0) {
            memcpy(tmp + 4, data, len);
        }
        crc = crc32(tmp, 4 + len, 0);
        free(tmp);
    }
    be32(footer, crc);
    if (fwrite(header, 1, 8, fp) != 8) {
        return -EIO;
    }
    if (len > 0 && fwrite(data, 1, len, fp) != len) {
        return -EIO;
    }
    if (fwrite(footer, 1, 4, fp) != 4) {
        return -EIO;
    }
    return 0;
}

int hs_write_png(const char *path, const uint32_t *pix, int width, int height, int stride_bytes)
{
    FILE *fp;
    uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    uint8_t ihdr[13];
    uint8_t *raw = NULL;
    uint8_t *zbuf = NULL;
    size_t raw_len;
    size_t zlen;
    int x, y;
    int rc = 0;
    uint32_t ad;
    const uint8_t *src;
    uint8_t *dst;
    size_t off;
    size_t remain;
    size_t chunk;
    size_t max_block = 65535;

    if (path == NULL || pix == NULL || width <= 0 || height <= 0 || stride_bytes < width * 4) {
        return -EINVAL;
    }

    crc_init();

    raw_len = (size_t)height * ((size_t)width * 3u + 1u);
    raw = (uint8_t *)malloc(raw_len);
    if (raw == NULL) {
        return -ENOMEM;
    }

    for (y = 0; y < height; y++) {
        src = (const uint8_t *)(pix + (size_t)y * ((size_t)stride_bytes / 4u));
        /* If stride is in bytes, pixel row start is stride_bytes * y. */
        src = (const uint8_t *)pix + (size_t)y * (size_t)stride_bytes;
        dst = raw + (size_t)y * ((size_t)width * 3u + 1u);
        *dst++ = 0; /* filter None */
        for (x = 0; x < width; x++) {
            uint32_t p = ((const uint32_t *)(src))[x];
            *dst++ = (uint8_t)((p >> 16) & 0xFFu);
            *dst++ = (uint8_t)((p >> 8) & 0xFFu);
            *dst++ = (uint8_t)(p & 0xFFu);
        }
    }

    /* zlib wrapper around stored deflate blocks. */
    zlen = 2 + raw_len + 4; /* header + data + adler */
    /* Plus 5 bytes per stored block. */
    zlen += 5u * ((raw_len + max_block - 1u) / max_block);
    zbuf = (uint8_t *)malloc(zlen);
    if (zbuf == NULL) {
        free(raw);
        return -ENOMEM;
    }

    zbuf[0] = 0x78; /* CMF: 32K window */
    zbuf[1] = 0x01; /* FLG: no dictionary, no compression, check bits */
    off = 2;
    remain = raw_len;
    src = raw;
    while (remain > 0) {
        chunk = remain > max_block ? max_block : remain;
        zbuf[off] = (remain - chunk == 0) ? 0x01 : 0x00; /* BFINAL, BTYPE=00 */
        le16(&zbuf[off + 1], (uint16_t)chunk);
        le16(&zbuf[off + 3], (uint16_t)~(uint16_t)chunk);
        memcpy(zbuf + off + 5, src, chunk);
        off += 5 + chunk;
        src += chunk;
        remain -= chunk;
    }
    ad = adler32(raw, raw_len);
    be32(zbuf + off, ad);
    off += 4;

    fp = fopen(path, "wb");
    if (fp == NULL) {
        rc = -errno;
        goto done;
    }
    if (fwrite(sig, 1, 8, fp) != 8) {
        rc = -EIO;
        goto done_fp;
    }

    memset(ihdr, 0, sizeof(ihdr));
    be32(ihdr, (uint32_t)width);
    be32(ihdr + 4, (uint32_t)height);
    ihdr[8] = 8;  /* bit depth */
    ihdr[9] = 2;  /* color type RGB */
    rc = write_chunk(fp, "IHDR", ihdr, sizeof(ihdr));
    if (rc != 0) {
        goto done_fp;
    }
    rc = write_chunk(fp, "IDAT", zbuf, off);
    if (rc != 0) {
        goto done_fp;
    }
    rc = write_chunk(fp, "IEND", NULL, 0);

done_fp:
    if (fclose(fp) != 0 && rc == 0) {
        rc = -EIO;
    }
done:
    free(zbuf);
    free(raw);
    return rc;
}
