/* SPDX-License-Identifier: MIT */
#ifndef HS_PNG_H
#define HS_PNG_H

#include <stdint.h>

/* Write an 8-bit RGB PNG. pix is XRGB8888, stride is in bytes. */
int hs_write_png(const char *path, const uint32_t *pix, int width, int height, int stride_bytes);

#endif /* HS_PNG_H */
