/* SPDX-License-Identifier: MIT */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hs_sha256.h"

#include <fcntl.h>
#include <unistd.h>

static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static const uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static void trans(struct hs_sha256 *s, const uint8_t *p)
{
    uint32_t w[64], a, b, c, d, e, f, g, h, t1, t2;
    int i;
    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
               ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
    }
    for (i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3];
    e = s->h[4]; f = s->h[5]; g = s->h[6]; h = s->h[7];
    for (i = 0; i < 64; i++) {
        t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ ((~e) & g)) + K[i] + w[i];
        t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d;
    s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}

void hs_sha256_init(struct hs_sha256 *s)
{
    s->h[0] = 0x6a09e667u; s->h[1] = 0xbb67ae85u; s->h[2] = 0x3c6ef372u; s->h[3] = 0xa54ff53au;
    s->h[4] = 0x510e527fu; s->h[5] = 0x9b05688cu; s->h[6] = 0x1f83d9abu; s->h[7] = 0x5be0cd19u;
    s->nbits = 0;
    s->used = 0;
}

void hs_sha256_update(struct hs_sha256 *s, const void *data, size_t n)
{
    const uint8_t *p = (const uint8_t *)data;
    s->nbits += (uint64_t)n * 8u;
    while (n > 0) {
        uint32_t room = 64u - s->used;
        uint32_t take = room < n ? room : (uint32_t)n;
        uint32_t i;
        for (i = 0; i < take; i++) s->block[s->used + i] = p[i];
        s->used += take;
        p += take;
        n -= take;
        if (s->used == 64u) {
            trans(s, s->block);
            s->used = 0;
        }
    }
}

void hs_sha256_final(struct hs_sha256 *s, uint8_t out[HS_SHA256_LEN])
{
    uint8_t pad[64 + 8];
    uint32_t i, pn;
    uint64_t bits = s->nbits;
    pad[0] = 0x80;
    pn = (s->used < 56u) ? (56u - s->used) : (120u - s->used);
    for (i = 1; i < pn; i++) pad[i] = 0;
    for (i = 0; i < 8; i++) pad[pn + i] = (uint8_t)(bits >> (56 - 8 * (int)i));
    hs_sha256_update(s, pad, pn + 8);
    for (i = 0; i < 8; i++) {
        out[i * 4 + 0] = (uint8_t)(s->h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(s->h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(s->h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(s->h[i]);
    }
}

void hs_sha256(const void *data, size_t n, uint8_t out[HS_SHA256_LEN])
{
    struct hs_sha256 s;
    hs_sha256_init(&s);
    hs_sha256_update(&s, data, n);
    hs_sha256_final(&s, out);
}

int hs_sha256_file(const char *path, uint8_t out[HS_SHA256_LEN])
{
    uint8_t buf[4096];
    struct hs_sha256 s;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    long r;
    if (fd < 0) return -1;
    hs_sha256_init(&s);
    for (;;) {
        r = read(fd, buf, sizeof(buf));
        if (r < 0) { close(fd); return -1; }
        if (r == 0) break;
        hs_sha256_update(&s, buf, (size_t)r);
    }
    close(fd);
    hs_sha256_final(&s, out);
    return 0;
}

void hs_sha256_hex(const uint8_t dgst[HS_SHA256_LEN], char out[65])
{
    static const char hex[] = "0123456789abcdef";
    int i;
    for (i = 0; i < HS_SHA256_LEN; i++) {
        out[i * 2] = hex[dgst[i] >> 4];
        out[i * 2 + 1] = hex[dgst[i] & 15];
    }
    out[64] = '\0';
}
