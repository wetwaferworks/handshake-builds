/* hs_rt: HandshakeOS freestanding aarch64 Linux runtime (MIT).
 * Provides exactly the libc surface the shipped handshake-init / handshake-gb
 * objects import, over raw syscalls. It contains no mutex, lock or futex code:
 *  - malloc keeps all state per thread (no shared allocator state at all);
 *  - stdio streams carry no internal locking (one thread per stream);
 *  - pthread_join polls the kernel-cleared tid word with nanosleep (no futex). */
#ifndef HS_RT_H
#define HS_RT_H
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#define HS_AT_FDCWD (-100)

typedef struct hs_thr {
    struct hs_thr *self;
    int err;                       /* per-thread errno */
    volatile int tid;              /* cleared by the kernel at thread exit (CLONE_CHILD_CLEARTID) */
    void *(*fn)(void *);
    void *arg;
    void *ret;
    void *map;                     /* stack + descriptor mapping (worker threads) */
    size_t map_len;
    /* per-thread allocator state */
    void *bins[32];                /* free lists by size class */
    unsigned char *cur, *end;      /* bump region */
} hs_thr;

static inline hs_thr *hs_self(void) {
    hs_thr *t;
    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(t));
    return t;
}

static inline long hs_sc(long n, long a, long b, long c, long d, long e, long f) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    register long x4 __asm__("x4") = e;
    register long x5 __asm__("x5") = f;
    __asm__ volatile("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory", "cc");
    return x0;
}
#define SC0(n) hs_sc((n), 0, 0, 0, 0, 0, 0)
#define SC1(n, a) hs_sc((n), (long)(a), 0, 0, 0, 0, 0)
#define SC2(n, a, b) hs_sc((n), (long)(a), (long)(b), 0, 0, 0, 0)
#define SC3(n, a, b, c) hs_sc((n), (long)(a), (long)(b), (long)(c), 0, 0, 0)
#define SC4(n, a, b, c, d) hs_sc((n), (long)(a), (long)(b), (long)(c), (long)(d), 0, 0)
#define SC5(n, a, b, c, d, e) hs_sc((n), (long)(a), (long)(b), (long)(c), (long)(d), (long)(e), 0)
#define SC6(n, a, b, c, d, e, f) hs_sc((n), (long)(a), (long)(b), (long)(c), (long)(d), (long)(e), (long)(f))

/* aarch64 asm-generic syscall numbers */
enum {
    NR_getcwd = 17, NR_ioctl = 29, NR_mkdirat = 34, NR_unlinkat = 35, NR_mount = 40,
    NR_faccessat = 48, NR_openat = 56, NR_close = 57, NR_getdents64 = 61, NR_lseek = 62,
    NR_read = 63, NR_write = 64, NR_ppoll = 73, NR_newfstatat = 79, NR_fstat = 80,
    NR_sync = 81, NR_fsync = 82, NR_exit = 93, NR_exit_group = 94, NR_nanosleep = 101,
    NR_clock_gettime = 113, NR_clock_nanosleep = 115, NR_sched_yield = 124, NR_reboot = 142,
    NR_getpid = 172, NR_munmap = 215, NR_clone = 220, NR_mmap = 222, NR_renameat2 = 276
};

long hs_ret(long r);               /* -errno -> -1 + errno */
void hs_rt_init(char **envp, unsigned long *auxv);
void hs_stdio_flush_all(void);
void hs_trap(const char *why) __attribute__((noreturn));

/* the libc surface, as the objects declare it */
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
size_t strlen(const char *s);
void *malloc(size_t n);
void free(void *p);
void exit(int code) __attribute__((noreturn));
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);

typedef void (*hs_sink)(void *ctx, const char *s, size_t n);
int hs_format(hs_sink sink, void *ctx, const char *fmt, va_list ap);
#endif
