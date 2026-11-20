/* hs_rt: memory, allocator, strings, conversions, qsort, sqrt (MIT). */
#include "hs_rt.h"

typedef uint64_t __attribute__((may_alias, aligned(1))) u64u;
typedef uint32_t __attribute__((may_alias, aligned(1))) u32u;

void *memcpy(void *d, const void *s, size_t n) {
    unsigned char *a = (unsigned char *)d;
    const unsigned char *b = (const unsigned char *)s;
    while (n >= 32) {
        uint64_t x0 = *(const u64u *)b, x1 = *(const u64u *)(b + 8);
        uint64_t x2 = *(const u64u *)(b + 16), x3 = *(const u64u *)(b + 24);
        *(u64u *)a = x0; *(u64u *)(a + 8) = x1; *(u64u *)(a + 16) = x2; *(u64u *)(a + 24) = x3;
        a += 32; b += 32; n -= 32;
    }
    while (n >= 8) { *(u64u *)a = *(const u64u *)b; a += 8; b += 8; n -= 8; }
    while (n--) *a++ = *b++;
    return d;
}

void *memmove(void *d, const void *s, size_t n) {
    unsigned char *a = (unsigned char *)d;
    const unsigned char *b = (const unsigned char *)s;
    if (a == b || n == 0) return d;
    if (a < b || a >= b + n) return memcpy(d, s, n);
    a += n; b += n;
    while (n >= 8) { a -= 8; b -= 8; n -= 8; *(u64u *)a = *(const u64u *)b; }
    while (n--) *--a = *--b;
    return d;
}

void *memset(void *d, int c, size_t n) {
    unsigned char *a = (unsigned char *)d;
    uint64_t v = (unsigned char)c * 0x0101010101010101ull;
    while (n >= 32) {
        *(u64u *)a = v; *(u64u *)(a + 8) = v; *(u64u *)(a + 16) = v; *(u64u *)(a + 24) = v;
        a += 32; n -= 32;
    }
    while (n >= 8) { *(u64u *)a = v; a += 8; n -= 8; }
    while (n--) *a++ = (unsigned char)c;
    return d;
}

/* ---- allocator: every byte of state is per thread (hs_thr), so there is nothing to share and
   nothing to guard. Small blocks: power-of-two classes 32 B .. 128 KiB carved from 1 MiB per-thread
   bump regions; freed blocks go to the freeing thread's class list. Large: direct mmap. ---- */
#define HDR 16u
#define CLS_MIN 5
#define CLS_MAX 17
#define TAG_SMALL 0x48530000u
#define TAG_LARGE 0x4853ffffu
#define TAG_ALIGN 0x4853aaaau
#define REGION (1u << 20)

typedef struct { uint32_t tag, cls; uint64_t info; } hdr_t;

static void *map_anon(size_t n) {
    void *p = (void *)SC6(NR_mmap, 0, n, 3, 0x22, -1, 0);
    if ((long)p < 0 && (long)p > -4096) return 0;
    return p;
}

void *malloc(size_t n) {
    hs_thr *t = hs_self();
    unsigned c = CLS_MIN;
    hdr_t *h;
    if (n > ((size_t)1 << CLS_MAX) - HDR) {
        size_t len;
        if (n > ((size_t)1 << 46)) { t->err = 12; return 0; }
        len = (n + HDR + 4095) & ~(size_t)4095;
        h = (hdr_t *)map_anon(len);
        if (!h) { t->err = 12; return 0; }
        h->tag = TAG_LARGE; h->cls = 0; h->info = len;
        return (unsigned char *)h + HDR;
    }
    while (((size_t)1 << c) < n + HDR) c++;
    if (t->bins[c]) {
        h = (hdr_t *)t->bins[c];
        t->bins[c] = *(void **)((unsigned char *)h + HDR);
    } else {
        size_t sz = (size_t)1 << c;
        if (t->cur == 0 || (size_t)(t->end - t->cur) < sz) {
            unsigned char *r = (unsigned char *)map_anon(REGION);
            if (!r) { t->err = 12; return 0; }
            t->cur = r; t->end = r + REGION;
        }
        h = (hdr_t *)t->cur;
        t->cur += sz;
    }
    h->tag = TAG_SMALL; h->cls = c; h->info = 0;
    return (unsigned char *)h + HDR;
}

void free(void *p) {
    hdr_t *h;
    hs_thr *t;
    if (!p) return;
    h = (hdr_t *)((unsigned char *)p - HDR);
    if (h->tag == TAG_ALIGN) { free((void *)(uintptr_t)h->info); return; }
    if (h->tag == TAG_LARGE) { SC2(NR_munmap, h, h->info); return; }
    if (h->tag != TAG_SMALL || h->cls < CLS_MIN || h->cls > CLS_MAX) hs_trap("hs_rt: free() of invalid pointer\n");
    t = hs_self();
    h->tag = 0;
    *(void **)p = t->bins[h->cls];
    t->bins[h->cls] = h;
}

void *calloc(size_t a, size_t b) {
    size_t n;
    void *p;
    if (__builtin_mul_overflow(a, b, &n)) { hs_self()->err = 12; return 0; }
    p = malloc(n);
    if (p) memset(p, 0, n);
    return p;
}

void *aligned_alloc(size_t al, size_t n) {
    unsigned char *raw, *u;
    hdr_t *h;
    if (al <= HDR) return malloc(n);
    if (al & (al - 1)) { hs_self()->err = 22; return 0; }
    if (al > ((size_t)1 << 46) || n > ((size_t)1 << 46)) { hs_self()->err = 12; return 0; } /* REVIEW HS-M2 */
    raw = (unsigned char *)malloc(n + al + HDR);
    if (!raw) return 0;
    u = (unsigned char *)(((uintptr_t)raw + HDR + al - 1) & ~(uintptr_t)(al - 1));
    h = (hdr_t *)(u - HDR);
    h->tag = TAG_ALIGN; h->cls = 0; h->info = (uint64_t)(uintptr_t)raw;
    return u;
}

/* ---- strings ---- */
size_t strlen(const char *s) { const char *p = s; while (*p) p++; return (size_t)(p - s); }
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
int strncmp(const char *a, const char *b, size_t n) {
    for (; n; n--, a++, b++) {
        if (*a != *b || !*a) return (unsigned char)*a - (unsigned char)*b;
    }
    return 0;
}
static int lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
int strcasecmp(const char *a, const char *b) {
    while (*a && lc((unsigned char)*a) == lc((unsigned char)*b)) { a++; b++; }
    return lc((unsigned char)*a) - lc((unsigned char)*b);
}
int strncasecmp(const char *a, const char *b, size_t n) {
    for (; n; n--, a++, b++) {
        int x = lc((unsigned char)*a), y = lc((unsigned char)*b);
        if (x != y || !x) return x - y;
    }
    return 0;
}
char *strncpy(char *d, const char *s, size_t n) {
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}
char *strrchr(const char *s, int c) {
    const char *r = 0;
    for (;; s++) { if (*s == (char)c) r = s; if (!*s) return (char *)r; }
}
char *strstr(const char *h, const char *n) {
    size_t k = strlen(n);
    if (!k) return (char *)h;
    for (; *h; h++) if (*h == *n && strncmp(h, n, k) == 0) return (char *)h;
    return 0;
}

/* ---- integer / float parsing ---- */
static int isspc(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
static int dval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 99;
}
/* C23 strtoull/strtol core (0b prefix accepted for base 0/2, as glibc's __isoc23_ forms). */
static unsigned long long parse_u(const char *s, char **end, int base, int *neg, int *ovf) {
    const char *p = s, *start;
    unsigned long long v = 0;
    int any = 0;
    *neg = 0; *ovf = 0;
    while (isspc((unsigned char)*p)) p++;
    if (*p == '+' || *p == '-') { *neg = (*p == '-'); p++; }
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] | 32) == 'x' && dval((unsigned char)p[2]) < 16) { p += 2; base = 16; }
    else if ((base == 0 || base == 2) && p[0] == '0' && (p[1] | 32) == 'b' && dval((unsigned char)p[2]) < 2) { p += 2; base = 2; }
    else if (base == 0) base = (p[0] == '0') ? 8 : 10;
    start = p;
    for (;; p++) {
        int d = dval((unsigned char)*p);
        if (d >= base) break;
        any = 1;
        if (v > (~0ull - (unsigned)d) / (unsigned)base) *ovf = 1;
        else v = v * (unsigned)base + (unsigned)d;
    }
    if (end) *end = (char *)(any ? p : s);
    (void)start;
    return v;
}
unsigned long long __isoc23_strtoull(const char *s, char **end, int base) {
    int neg, ovf;
    unsigned long long v = parse_u(s, end, base, &neg, &ovf);
    if (ovf) { hs_self()->err = 34; return ~0ull; }
    return neg ? -v : v;
}
long __isoc23_strtol(const char *s, char **end, int base) {
    int neg, ovf;
    unsigned long long v = parse_u(s, end, base, &neg, &ovf);
    if (ovf || v > (neg ? 0x8000000000000000ull : 0x7fffffffffffffffull)) {
        hs_self()->err = 34;
        return neg ? (long)0x8000000000000000ull : 0x7fffffffffffffffl;
    }
    return neg ? (long)(0 - v) : (long)v;
}
/* Decimal strtod: exact (correctly rounded) whenever the significant digits fit 2^53 and the decimal
   exponent is within 10^±22 (Clinger's fast path) - true for every flag value the objects parse. */
double strtod(const char *s, char **end) {
    static const double p10[] = { 1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
                                  1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22 };
    const char *p = s;
    unsigned long long m = 0;
    int neg = 0, any = 0, dexp = 0, nd = 0;
    double v;
    while (isspc((unsigned char)*p)) p++;
    if (*p == '+' || *p == '-') { neg = (*p == '-'); p++; }
    for (; *p >= '0' && *p <= '9'; p++) { any = 1; if (nd < 19) { m = m * 10 + (unsigned)(*p - '0'); if (m) nd++; } else dexp++; }
    if (*p == '.') {
        p++;
        for (; *p >= '0' && *p <= '9'; p++) { any = 1; if (nd < 19) { m = m * 10 + (unsigned)(*p - '0'); if (m) nd++; dexp--; } }
    }
    if (!any) { if (end) *end = (char *)s; return 0.0; }
    if ((*p | 32) == 'e') {
        const char *q = p + 1;
        int en = 0, e = 0;
        if (*q == '+' || *q == '-') { en = (*q == '-'); q++; }
        if (*q >= '0' && *q <= '9') {
            for (; *q >= '0' && *q <= '9'; q++) if (e < 100000) e = e * 10 + (*q - '0');
            dexp += en ? -e : e;
            p = q;
        }
    }
    if (end) *end = (char *)p;
    v = (double)m;
    while (dexp > 22) { v *= 1e22; dexp -= 22; }
    while (dexp < -22) { v /= 1e22; dexp += 22; }
    if (dexp >= 0) v *= p10[dexp]; else v /= p10[-dexp];
    return neg ? -v : v;
}

double sqrt(double x) { double r; __asm__("fsqrt %d0, %d1" : "=w"(r) : "w"(x)); return r; }
float sqrtf(float x) { float r; __asm__("fsqrt %s0, %s1" : "=w"(r) : "w"(x)); return r; }

/* ---- qsort: stable top-down merge sort (same order as glibc 2.41's merge-sort path) ---- */
static void msort(unsigned char *b, size_t n, size_t sz, int (*cmp)(const void *, const void *), unsigned char *tmp) {
    size_t n1, n2, i = 0, j = 0, k = 0;
    unsigned char *b2;
    if (n <= 1) return;
    n1 = n / 2; n2 = n - n1;
    b2 = b + n1 * sz;
    msort(b, n1, sz, cmp, tmp);
    msort(b2, n2, sz, cmp, tmp);
    while (i < n1 && j < n2) {
        if (cmp(b + i * sz, b2 + j * sz) <= 0) { memcpy(tmp + k * sz, b + i * sz, sz); i++; }
        else { memcpy(tmp + k * sz, b2 + j * sz, sz); j++; }
        k++;
    }
    if (i < n1) memcpy(tmp + k * sz, b + i * sz, (n1 - i) * sz);
    k += n1 - i;
    (void)k;
    memcpy(b, tmp, (n - n2 + j) * sz);
}
void qsort(void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *)) {
    unsigned char *tmp;
    if (n < 2 || sz == 0) return;
    if (n > (size_t)-1 / sz) hs_trap("hs_rt: qsort size overflow\n"); /* REVIEW HS-M3 */
    tmp = (unsigned char *)malloc(n * sz);
    if (!tmp) hs_trap("hs_rt: qsort out of memory\n");
    msort((unsigned char *)base, n, sz, cmp, tmp);
    free(tmp);
}

/* ---- _FORTIFY_SOURCE memory entry points ---- */
void *__memcpy_chk(void *d, const void *s, size_t n, size_t dlen) {
    if (dlen < n) hs_trap("*** buffer overflow detected ***: terminated\n");
    return memcpy(d, s, n);
}
void *__memmove_chk(void *d, const void *s, size_t n, size_t dlen) {
    if (dlen < n) hs_trap("*** buffer overflow detected ***: terminated\n");
    return memmove(d, s, n);
}
void *__memset_chk(void *d, int c, size_t n, size_t dlen) {
    if (dlen < n) hs_trap("*** buffer overflow detected ***: terminated\n");
    return memset(d, c, n);
}
char *__strncpy_chk(char *d, const char *s, size_t n, size_t dlen) {
    if (dlen < n) hs_trap("*** buffer overflow detected ***: terminated\n");
    return strncpy(d, s, n);
}
int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
    for (; n; n--, x++, y++) if (*x != *y) return *x - *y;
    return 0;
}
