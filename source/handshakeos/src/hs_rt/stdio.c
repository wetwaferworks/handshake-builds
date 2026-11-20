/* hs_rt: lock-free stdio, formatted output and directory streams (MIT).
 * Streams carry no internal lock: each stream is used by one thread. The open-stream list is a
 * lock-free singly linked list (push by CAS, never unlinked; closed nodes are reclaimed by CAS on
 * their state word). Buffering follows glibc's defaults so output bytes and order match the host:
 * stderr unbuffered, terminals line buffered, everything else fully buffered at st_blksize. */
#include "hs_rt.h"

enum { ST_FREE = 0, ST_OPEN = 1, ST_CLAIM = 2 };
enum { BM_UNSET = -1, BM_FULL = 0, BM_LINE = 1, BM_NONE = 2 };

typedef struct hs_file {
    int state;
    int fd;
    int bufmode;
    int eof, err;
    int reading;                 /* buffer currently holds read-ahead */
    unsigned char *buf;
    size_t cap, rpos, rlen, wlen;
    struct hs_file *next;
} hs_file;

static unsigned char hs_errbuf[1];
static hs_file hs_stdin_f  = { ST_OPEN, 0, BM_UNSET, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static hs_file hs_stdout_f = { ST_OPEN, 1, BM_UNSET, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static hs_file hs_stderr_f = { ST_OPEN, 2, BM_NONE,  0, 0, 0, hs_errbuf, 1, 0, 0, 0, 0 };
hs_file *stdin = &hs_stdin_f;
hs_file *stdout = &hs_stdout_f;
hs_file *stderr = &hs_stderr_f;
static hs_file *hs_list;         /* fopen'd streams */

/* ---- raw fd helpers ---- */
static int wr_all(int fd, const unsigned char *p, size_t n) {
    while (n) {
        long r = SC3(NR_write, fd, p, n);
        if (r == -4) continue;                     /* EINTR */
        if (r <= 0) { hs_self()->err = r < 0 ? (int)-r : 5; return -1; }
        p += r; n -= (size_t)r;
    }
    return 0;
}

static void setup_buf(hs_file *f) {
    unsigned char st[128];
    size_t sz = 8192;                              /* glibc BUFSIZ fallback */
    int tty = 0;
    unsigned char tio[64];
    if (f->buf) return;
    if (SC2(NR_fstat, f->fd, st) == 0) {
        unsigned mode;
        int blk;
        memcpy(&mode, st + 16, sizeof(mode)); /* REVIEW HS-L2: no aliasing/alignment UB */
        memcpy(&blk, st + 56, sizeof(blk));
        if (blk > 0) sz = (size_t)blk;
        if ((mode & 0170000) == 0020000 && SC3(NR_ioctl, f->fd, 0x5401 /* TCGETS */, tio) == 0) tty = 1;
    }
    if (f->bufmode == BM_UNSET) f->bufmode = tty ? BM_LINE : BM_FULL;
    if (f->bufmode == BM_NONE) { f->buf = hs_errbuf; f->cap = 1; return; }
    f->buf = (unsigned char *)malloc(sz);
    if (!f->buf) { f->bufmode = BM_NONE; f->buf = hs_errbuf; f->cap = 1; return; }
    f->cap = sz;
}

static int flush_w(hs_file *f) {
    if (f->wlen) {
        size_t n = f->wlen;
        f->wlen = 0;
        if (wr_all(f->fd, f->buf, n) < 0) { f->err = 1; return -1; }
    }
    return 0;
}

/* drop read-ahead, moving the fd back to the logical position */
static void drop_r(hs_file *f) {
    if (f->reading) {
        if (f->rlen > f->rpos) SC3(NR_lseek, f->fd, -(long)(f->rlen - f->rpos), 1);
        f->rpos = f->rlen = 0;
        f->reading = 0;
    }
}

int fflush(hs_file *f) {
    if (!f) { hs_stdio_flush_all(); return 0; }
    if (f->state != ST_OPEN) return 0;
    if (f->reading) { drop_r(f); return 0; }
    return flush_w(f) < 0 ? -1 : 0;
}

void hs_stdio_flush_all(void) {
    hs_file *f;
    for (f = __atomic_load_n(&hs_list, __ATOMIC_ACQUIRE); f; f = f->next)
        if (__atomic_load_n(&f->state, __ATOMIC_ACQUIRE) == ST_OPEN) fflush(f);
    fflush(&hs_stdout_f);
    fflush(&hs_stderr_f);
}

static size_t put_bytes(hs_file *f, const unsigned char *p, size_t n) {
    size_t done = n;
    if (f->state != ST_OPEN || n == 0) return 0;
    drop_r(f);
    setup_buf(f);
    if (f->bufmode == BM_NONE) {
        if (wr_all(f->fd, p, n) < 0) { f->err = 1; return 0; }
        return done;
    }
    if (f->wlen + n > f->cap) {
        size_t room = f->cap - f->wlen;
        memcpy(f->buf + f->wlen, p, room);
        f->wlen = f->cap; p += room; n -= room;
        if (flush_w(f) < 0) return 0;
        if (n >= f->cap) {
            size_t direct = n - n % f->cap;
            if (wr_all(f->fd, p, direct) < 0) { f->err = 1; return 0; }
            p += direct; n -= direct;
        }
    }
    memcpy(f->buf + f->wlen, p, n);
    f->wlen += n;
    if (f->bufmode == BM_LINE) {
        size_t i;
        for (i = 0; i < f->wlen; i++) if (f->buf[i] == '\n') { if (flush_w(f) < 0) return 0; break; }
    }
    return done;
}

/* ---- fopen / fclose ---- */
hs_file *fopen(const char *path, const char *mode) {
    int flags, plus = 0, fd;
    const char *m;
    hs_file *f, *head;
    switch (mode[0]) {
    case 'r': flags = 0; break;
    case 'w': flags = 01 | 0100 | 01000; break;               /* WRONLY CREAT TRUNC */
    case 'a': flags = 01 | 0100 | 02000; break;               /* WRONLY CREAT APPEND */
    default: hs_self()->err = 22; return 0;
    }
    for (m = mode + 1; *m; m++) {
        if (*m == '+') plus = 1;
        else if (*m == 'e') flags |= 02000000;                   /* CLOEXEC */
        else if (*m == 'x') flags |= 0200;                       /* EXCL */
    }
    if (plus) flags = (flags & ~3) | 02;
    fd = (int)hs_ret(SC4(NR_openat, HS_AT_FDCWD, path, flags, 0666));
    if (fd < 0) return 0;
    for (f = __atomic_load_n(&hs_list, __ATOMIC_ACQUIRE); f; f = f->next) {
        int exp = ST_FREE;
        if (__atomic_compare_exchange_n(&f->state, &exp, ST_CLAIM, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) break;
    }
    if (!f) {
        f = (hs_file *)malloc(sizeof *f);
        if (!f) { SC1(NR_close, fd); return 0; }
        f->state = ST_CLAIM;
        head = __atomic_load_n(&hs_list, __ATOMIC_ACQUIRE);
        do { f->next = head; } while (!__atomic_compare_exchange_n(&hs_list, &head, f, 1, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
    }
    f->fd = fd; f->bufmode = BM_UNSET; f->eof = f->err = f->reading = 0;
    f->buf = 0; f->cap = f->rpos = f->rlen = f->wlen = 0;
    __atomic_store_n(&f->state, ST_OPEN, __ATOMIC_RELEASE);
    return f;
}

int fclose(hs_file *f) {
    int r = 0;
    if (!f || f->state != ST_OPEN) return -1;
    if (!f->reading && flush_w(f) < 0) r = -1;
    if (hs_ret(SC1(NR_close, f->fd)) < 0) r = -1;
    if (f->buf && f->buf != hs_errbuf) free(f->buf);
    f->buf = 0;
    if (f == &hs_stdout_f || f == &hs_stderr_f || f == &hs_stdin_f) { f->state = ST_CLAIM; return r; }
    __atomic_store_n(&f->state, ST_FREE, __ATOMIC_RELEASE);
    return r;
}

/* ---- reading ---- */
static int fill(hs_file *f) {
    long r;
    if (f->state != ST_OPEN) return -1;
    if (!f->reading) { if (flush_w(f) < 0) return -1; f->reading = 1; f->rpos = f->rlen = 0; }
    setup_buf(f);
    if (f->cap < 2 && f->buf == hs_errbuf) return -1;
    do r = SC3(NR_read, f->fd, f->buf, f->cap); while (r == -4);
    if (r < 0) { f->err = 1; hs_self()->err = (int)-r; return -1; }
    if (r == 0) { f->eof = 1; return -1; }
    f->rpos = 0; f->rlen = (size_t)r;
    return 0;
}

size_t fread(void *dst, size_t sz, size_t n, hs_file *f) {
    unsigned char *d = (unsigned char *)dst;
    size_t want, got = 0;
    if (sz == 0 || n == 0) return 0;
    if (n > (size_t)-1 / sz) return 0; /* REVIEW HS-L1 */
    want = sz * n;
    if (!f->reading) { if (flush_w(f) < 0) return 0; f->reading = 1; f->rpos = f->rlen = 0; }
    while (got < want) {
        size_t avail = f->rlen - f->rpos;
        if (avail) {
            size_t k = want - got < avail ? want - got : avail;
            memcpy(d + got, f->buf + f->rpos, k);
            f->rpos += k; got += k;
            continue;
        }
        setup_buf(f);
        if (want - got >= f->cap) {
            long r;
            do r = SC3(NR_read, f->fd, d + got, want - got); while (r == -4);
            if (r < 0) { f->err = 1; break; }
            if (r == 0) { f->eof = 1; break; }
            got += (size_t)r;
            continue;
        }
        if (fill(f) < 0) break;
    }
    return got / sz;
}

char *fgets(char *s, int n, hs_file *f) {
    int i = 0;
    if (n <= 0) return 0;
    while (i < n - 1) {
        unsigned char c;
        if (!f->reading || f->rpos >= f->rlen) { if (fill(f) < 0) break; }
        c = f->buf[f->rpos++];
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    if (i == 0) return 0;
    s[i] = 0;
    return s;
}

int fgetc(hs_file *f) {
    if (!f->reading || f->rpos >= f->rlen) { if (fill(f) < 0) return -1; }
    return f->buf[f->rpos++];
}
int getc(hs_file *f) { return fgetc(f); }

/* ---- positioning ---- */
int fseek(hs_file *f, long off, int whence) {
    long r;
    if (f->state != ST_OPEN) return -1;
    if (f->reading) {
        if (whence == 1) off -= (long)(f->rlen - f->rpos);
        f->rpos = f->rlen = 0; f->reading = 0;
    } else if (flush_w(f) < 0) return -1;
    r = hs_ret(SC3(NR_lseek, f->fd, off, whence));
    if (r < 0) return -1;
    f->eof = 0;
    return 0;
}
long ftell(hs_file *f) {
    long r = hs_ret(SC3(NR_lseek, f->fd, 0, 1));
    if (r < 0) return -1;
    if (f->reading) return r - (long)(f->rlen - f->rpos);
    return r + (long)f->wlen;
}
void rewind(hs_file *f) { fseek(f, 0, 0); f->err = 0; }
int feof(hs_file *f) { return f->eof; }
int ferror(hs_file *f) { return f->err; }
int fileno(hs_file *f) { return f->fd; }

/* ---- writing ---- */
size_t fwrite(const void *p, size_t sz, size_t n, hs_file *f) {
    size_t t;
    if (sz == 0 || n == 0) return 0;
    if (n > (size_t)-1 / sz) return 0; /* REVIEW HS-L1 */
    t = sz * n;
    return put_bytes(f, (const unsigned char *)p, t) == t ? n : 0;
}
int fputs(const char *s, hs_file *f) { size_t n = strlen(s); return (n == 0 || put_bytes(f, (const unsigned char *)s, n) == n) ? 1 : -1; }
int fputc(int c, hs_file *f) { unsigned char b = (unsigned char)c; return put_bytes(f, &b, 1) == 1 ? b : -1; }
int putc(int c, hs_file *f) { return fputc(c, f); }
int putchar(int c) { return fputc(c, stdout); }
int puts(const char *s) { if (fputs(s, stdout) < 0) return -1; return fputc('\n', stdout) < 0 ? -1 : 1; }

/* ---- formatting core ---- */
static void emit(hs_sink sink, void *ctx, const char *s, size_t n, int *total) { if (n) { sink(ctx, s, n); *total += (int)n; } }
static void pad(hs_sink sink, void *ctx, char c, int n, int *total) {
    char b[32];
    int i;
    for (i = 0; i < 32; i++) b[i] = c;
    while (n > 0) { int k = n > 32 ? 32 : n; emit(sink, ctx, b, (size_t)k, total); n -= k; }
}

int hs_format(hs_sink sink, void *ctx, const char *fmt, va_list ap) {
    int total = 0;
    while (*fmt) {
        const char *p = fmt;
        int left = 0, plus = 0, space = 0, alt = 0, zero = 0, width = 0, prec = -1, lng = 0;
        char conv, digits[72], prefix[3];
        int nd = 0, np = 0, neg = 0;
        unsigned long long u = 0;
        while (*p && *p != '%') p++;
        emit(sink, ctx, fmt, (size_t)(p - fmt), &total);
        if (!*p) break;
        p++;
        for (;; p++) {
            if (*p == '-') left = 1; else if (*p == '+') plus = 1; else if (*p == ' ') space = 1;
            else if (*p == '#') alt = 1; else if (*p == '0') zero = 1; else break;
        }
        if (*p == '*') { width = va_arg(ap, int); if (width < 0) { left = 1; width = -width; } p++; }
        else while (*p >= '0' && *p <= '9') width = width * 10 + (*p++ - '0');
        if (*p == '.') {
            p++; prec = 0;
            if (*p == '*') { prec = va_arg(ap, int); if (prec < 0) prec = -1; p++; }
            else while (*p >= '0' && *p <= '9') prec = prec * 10 + (*p++ - '0');
        }
        /* lng: -2 hh, -1 h, 0 int, 1 long, 2 long long, 3 size_t/intmax/ptrdiff (64-bit) */
        for (;; p++) {
            if (*p == 'h') lng = (lng == -1) ? -2 : -1;
            else if (*p == 'l') lng = (lng == 1) ? 2 : 1;
            else if (*p == 'q' || *p == 'L') lng = 2;
            else if (*p == 'z' || *p == 'j' || *p == 't') lng = 3;
            else break;
        }
        conv = *p++;
        fmt = p;
        switch (conv) {
        case '%': emit(sink, ctx, "%", 1, &total); continue;
        case 'c': {
            char c = (char)va_arg(ap, int);
            if (!left) pad(sink, ctx, ' ', width - 1, &total);
            emit(sink, ctx, &c, 1, &total);
            if (left) pad(sink, ctx, ' ', width - 1, &total);
            continue;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            size_t n = 0;
            if (!s) s = (prec < 0 || prec >= 6) ? "(null)" : "";
            while ((prec < 0 || n < (size_t)prec) && s[n]) n++;
            if (!left) pad(sink, ctx, ' ', width - (int)n, &total);
            emit(sink, ctx, s, n, &total);
            if (left) pad(sink, ctx, ' ', width - (int)n, &total);
            continue;
        }
        case 'n': (void)va_arg(ap, int *); continue;
        case 'd': case 'i': {
            long long v;
            if (lng >= 1) v = va_arg(ap, long long); else v = va_arg(ap, int);
            if (lng == -1) v = (short)v; else if (lng == -2) v = (signed char)v;
            if (v < 0) { neg = 1; u = 0ull - (unsigned long long)v; } else u = (unsigned long long)v;
            if (neg) prefix[np++] = '-'; else if (plus) prefix[np++] = '+'; else if (space) prefix[np++] = ' ';
            break;
        }
        case 'u': case 'x': case 'X': case 'o': case 'p':
            if (conv == 'p') { u = (unsigned long long)(uintptr_t)va_arg(ap, void *); }
            else if (lng >= 1) u = va_arg(ap, unsigned long long);
            else u = va_arg(ap, unsigned);
            if (lng == -1) u = (unsigned short)u; else if (lng == -2) u = (unsigned char)u;
            if (conv == 'p') {
                if (u == 0) {
                    const char *nil = "(nil)";
                    if (!left) pad(sink, ctx, ' ', width - 5, &total);
                    emit(sink, ctx, nil, 5, &total);
                    if (left) pad(sink, ctx, ' ', width - 5, &total);
                    continue;
                }
                prefix[np++] = '0'; prefix[np++] = 'x'; conv = 'x';
            } else if (alt && u != 0 && (conv == 'x' || conv == 'X')) { prefix[np++] = '0'; prefix[np++] = conv; }
            break;
        default:
            hs_trap("hs_rt: unsupported printf conversion\n");
        }
        {
            unsigned base = (conv == 'x' || conv == 'X') ? 16 : (conv == 'o') ? 8 : 10;
            const char *dg = (conv == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
            char tmp[72];
            int i, zeros, body;
            while (u) { tmp[nd++] = dg[u % base]; u /= base; }
            if (prec < 0 && nd == 0) tmp[nd++] = '0';
            if (conv == 'o' && alt && (nd == 0 || tmp[nd - 1] != '0')) { if (prec <= nd) prec = nd + 1; }
            for (i = 0; i < nd; i++) digits[i] = tmp[nd - 1 - i];
            zeros = (prec > nd) ? prec - nd : 0;
            body = np + zeros + nd;
            if (zero && !left && prec < 0 && width > body) { zeros += width - body; body = width; }
            if (!left) pad(sink, ctx, ' ', width - body, &total);
            emit(sink, ctx, prefix, (size_t)np, &total);
            pad(sink, ctx, '0', zeros, &total);
            emit(sink, ctx, digits, (size_t)nd, &total);
            if (left) pad(sink, ctx, ' ', width - body, &total);
        }
    }
    return total;
}

/* ---- sinks ---- */
typedef struct { char *buf; size_t cap, len; } sbuf;
static void sbuf_sink(void *c, const char *s, size_t n) {
    sbuf *b = (sbuf *)c;
    if (b->cap && b->len < b->cap - 1) {
        size_t k = b->cap - 1 - b->len;
        if (k > n) k = n;
        memcpy(b->buf + b->len, s, k);
    }
    b->len += n;
}
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap) {
    sbuf b;
    int r;
    b.buf = buf; b.cap = n; b.len = 0;
    r = hs_format(sbuf_sink, &b, fmt, ap);
    if (n) buf[b.len < n - 1 ? b.len : n - 1] = 0;
    return r;
}
int snprintf(char *buf, size_t n, const char *fmt, ...) {
    va_list ap; int r;
    va_start(ap, fmt); r = vsnprintf(buf, n, fmt, ap); va_end(ap);
    return r;
}

/* Unbuffered streams get the whole formatted text in one write (as glibc's helper buffer does). */
typedef struct { hs_file *f; char tmp[4096]; size_t len; } fsink_t;
static void file_sink(void *c, const char *s, size_t n) {
    fsink_t *k = (fsink_t *)c;
    if (k->f->bufmode != BM_NONE) { put_bytes(k->f, (const unsigned char *)s, n); return; }
    if (k->len + n > sizeof k->tmp) {
        put_bytes(k->f, (const unsigned char *)k->tmp, k->len); k->len = 0;
        if (n > sizeof k->tmp) { put_bytes(k->f, (const unsigned char *)s, n); return; }
    }
    memcpy(k->tmp + k->len, s, n); k->len += n;
}
int vfprintf(hs_file *f, const char *fmt, va_list ap) {
    fsink_t k;
    int r;
    if (f->state != ST_OPEN) return -1;
    setup_buf(f);
    k.f = f; k.len = 0;
    r = hs_format(file_sink, &k, fmt, ap);
    if (k.len) put_bytes(f, (const unsigned char *)k.tmp, k.len);
    return f->err ? -1 : r;
}
int fprintf(hs_file *f, const char *fmt, ...) {
    va_list ap; int r;
    va_start(ap, fmt); r = vfprintf(f, fmt, ap); va_end(ap);
    return r;
}
int printf(const char *fmt, ...) {
    va_list ap; int r;
    va_start(ap, fmt); r = vfprintf(stdout, fmt, ap); va_end(ap);
    return r;
}
int vprintf(const char *fmt, va_list ap) { return vfprintf(stdout, fmt, ap); }

/* ---- directory streams (getdents64; struct dirent == linux_dirent64 on aarch64) ---- */
typedef struct { int fd; size_t pos, len; unsigned char buf[8192] __attribute__((aligned(8))); } hs_dir;
hs_dir *opendir(const char *path) {
    hs_dir *d;
    int fd = (int)hs_ret(SC4(NR_openat, HS_AT_FDCWD, path, 040000 | 02000000, 0)); /* DIRECTORY|CLOEXEC */
    if (fd < 0) return 0;
    d = (hs_dir *)malloc(sizeof *d);
    if (!d) { SC1(NR_close, fd); return 0; }
    d->fd = fd; d->pos = d->len = 0;
    return d;
}
void *readdir(hs_dir *d) {
    void *e;
    if (d->pos >= d->len) {
        long r = SC3(NR_getdents64, d->fd, d->buf, sizeof d->buf);
        if (r <= 0) { if (r < 0) hs_self()->err = (int)-r; return 0; }
        d->len = (size_t)r; d->pos = 0;
    }
    e = d->buf + d->pos;
    d->pos += *(unsigned short *)(d->buf + d->pos + 16);
    return e;
}
int closedir(hs_dir *d) {
    int r = (int)hs_ret(SC1(NR_close, d->fd));
    free(d);
    return r;
}

/* ---- _FORTIFY_SOURCE entry points (the toolchain enables fortify by default): same checks as
   glibc's, failing hard on a detected overflow. %n is not special-cased (never used). ---- */
int __vsnprintf_chk(char *buf, size_t n, int flag, size_t slen, const char *fmt, va_list ap) {
    (void)flag;
    if (slen < n) hs_trap("*** buffer overflow detected ***: terminated\n");
    return vsnprintf(buf, n, fmt, ap);
}
int __snprintf_chk(char *buf, size_t n, int flag, size_t slen, const char *fmt, ...) {
    va_list ap; int r;
    va_start(ap, fmt); r = __vsnprintf_chk(buf, n, flag, slen, fmt, ap); va_end(ap);
    return r;
}
int __fprintf_chk(hs_file *f, int flag, const char *fmt, ...) {
    va_list ap; int r;
    (void)flag;
    va_start(ap, fmt); r = vfprintf(f, fmt, ap); va_end(ap);
    return r;
}
int __printf_chk(int flag, const char *fmt, ...) {
    va_list ap; int r;
    (void)flag;
    va_start(ap, fmt); r = vfprintf(stdout, fmt, ap); va_end(ap);
    return r;
}
int __vfprintf_chk(hs_file *f, int flag, const char *fmt, va_list ap) { (void)flag; return vfprintf(f, fmt, ap); }
char *__fgets_chk(char *s, size_t slen, int n, hs_file *f) {
    if ((size_t)n > slen) hs_trap("*** buffer overflow detected ***: terminated\n");
    return fgets(s, n, f);
}
size_t __fread_chk(void *d, size_t dlen, size_t sz, size_t n, hs_file *f) {
    if (sz && n > dlen / sz) hs_trap("*** buffer overflow detected ***: terminated\n");
    return fread(d, sz, n, f);
}
