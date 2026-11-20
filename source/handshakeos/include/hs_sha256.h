/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 HandshakeOS contributors
 *
 * hs_sha256.h - FIPS 180-4 SHA-256 in pure C11. No locks. Used by the v5.4 A/B
 * updater to verify .hsu packages and by tests. */
#ifndef HS_SHA256_H
#define HS_SHA256_H
#include <stddef.h>
#include <stdint.h>

enum { HS_SHA256_LEN = 32 };

struct hs_sha256 {
    uint32_t h[8];
    uint64_t nbits;
    uint8_t  block[64];
    uint32_t used;
};

void hs_sha256_init(struct hs_sha256 *s);
void hs_sha256_update(struct hs_sha256 *s, const void *data, size_t n);
void hs_sha256_final(struct hs_sha256 *s, uint8_t out[HS_SHA256_LEN]);
void hs_sha256(const void *data, size_t n, uint8_t out[HS_SHA256_LEN]);
/* 0 if the file was hashed, -1 on open/read error. */
int  hs_sha256_file(const char *path, uint8_t out[HS_SHA256_LEN]);
/* Write 64 lowercase hex chars + NUL. */
void hs_sha256_hex(const uint8_t dgst[HS_SHA256_LEN], char out[65]);

#endif
