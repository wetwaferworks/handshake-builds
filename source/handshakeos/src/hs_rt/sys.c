/* hs_rt: entry, errno, syscall wrappers, environment, time, threads (MIT). */
#include "hs_rt.h"

static hs_thr hs_main_thr;
static char **hs_envp;
typedef int (*hs_vdso_cgt)(int, void *);
static hs_vdso_cgt hs_cgt;

int main(int argc, char **argv, char **envp);

long hs_ret(long r) {
    if (r < 0 && r > -4096) {
        hs_self()->err = (int)-r;
        return -1;
    }
    return r;
}

int *__errno_location(void) { return &hs_self()->err; }

/* ---- REVIEW HS-O1: PID 1 fault path. No hs_rt fatal path may exit, panic or power off PID 1.
   hs_trap() / exit() in PID 1: chain a record into the fault ledger (FNV-1a64 over seq, code and the
   message; carried across re-entry in HS_FAULT_SEQ / HS_FAULT_LEDGER), print it to the console,
   append + fsync a readable line to HANDSHAKE-FAULTS.log on the boot FAT partition, then re-enter the
   boot screen by re-executing /init in place (same PID 1; kernel drivers stay bound; init reopens
   fb / input / audio). If execve fails, or a fault hits while handling a fault, idle forever.
   Never exit_group, never reboot. Non-PID-1 programs keep the old abort (exit 134). ---- */
char *getenv(const char *name);
int hs_rt_fault_force_pid1;                                      /* tests only */
const char *hs_rt_fault_log_path = "/mnt/sd/HANDSHAKE-FAULTS.log";
const char *hs_rt_fault_reexec_path = "/init";
void (*hs_rt_fault_reenter)(const char *why, unsigned seq, uint64_t ledger); /* tests only */
static char **hs_argv;
static volatile int hs_in_fault;

static void hs_idle_forever(void) {
    for (;;) { long ts[2] = { 1, 0 }; (void)SC2(NR_nanosleep, ts, 0); }
}
static uint64_t hs_fnv_byte(uint64_t h, unsigned char c) { h ^= c; return h * 0x100000001b3ull; }
static uint64_t hs_fnv_u64(uint64_t h, uint64_t v) { int i; for (i = 0; i < 8; i++) h = hs_fnv_byte(h, (unsigned char)(v >> (8 * i))); return h; }
static uint64_t hs_parse_u64(const char *s, int base) {
    uint64_t v = 0;
    if (!s) return 0;
    for (; *s; s++) {
        int d = (*s >= '0' && *s <= '9') ? *s - '0' : (*s >= 'a' && *s <= 'f') ? *s - 'a' + 10 : (*s >= 'A' && *s <= 'F') ? *s - 'A' + 10 : 99;
        if (d >= base) break;
        v = v * (uint64_t)base + (uint64_t)d;
    }
    return v;
}
static int hs_fmt(char *b, size_t n, const char *f, ...) {
    va_list ap; int r;
    va_start(ap, f); r = vsnprintf(b, n, f, ap); va_end(ap);
    return r;
}
static int hs_pfx(const char *s, const char *p) { while (*p && *s == *p) { s++; p++; } return *p == 0; }
static int hs_is_pid1(void) { return hs_rt_fault_force_pid1 || SC0(NR_getpid) == 1; }

static void hs_rt_fault(const char *why, int code) __attribute__((noreturn));
static void hs_rt_fault(const char *why, int code) {
    static char line[256], e_seq[40], e_led[48];
    static char *envv[72];
    const char *s;
    unsigned seq;
    uint64_t led;
    long fd;
    int n, k = 0, i;
    if (hs_in_fault) hs_idle_forever();
    hs_in_fault = 1;
    seq = (unsigned)hs_parse_u64(getenv("HS_FAULT_SEQ"), 10) + 1u;
    s = getenv("HS_FAULT_LEDGER");
    led = s ? hs_parse_u64(s, 16) : 0xcbf29ce484222325ull;
    led = hs_fnv_u64(hs_fnv_u64(led, seq), (uint64_t)(unsigned)code);
    for (s = why; *s; s++) led = hs_fnv_byte(led, (unsigned char)*s);
    n = hs_fmt(line, sizeof line, "HandshakeOS FAULT #%u HS_RT code=%d fault_ledger=%016llx -> boot screen: %s",
                 seq, code, (unsigned long long)led, why);
    if (n < 0) n = 0;
    if ((size_t)n >= sizeof line) n = (int)sizeof line - 1;
    if (n == 0 || line[n - 1] != '\n') { if ((size_t)n < sizeof line - 1) line[n++] = '\n'; else line[n - 1] = '\n'; }
    fd = SC4(NR_openat, HS_AT_FDCWD, "/dev/console", 01 | 0400 | 02000000 /* WRONLY|NOCTTY|CLOEXEC */, 0);
    if (fd >= 0) { (void)SC3(NR_write, fd, line, (size_t)n); (void)SC1(NR_close, fd); }
    if (hs_rt_fault_log_path) {
        fd = SC4(NR_openat, HS_AT_FDCWD, hs_rt_fault_log_path, 01 | 0100 | 02000 | 02000000 /* WRONLY|CREAT|APPEND|CLOEXEC */, 0644);
        if (fd >= 0) { (void)SC3(NR_write, fd, line, (size_t)n); (void)SC1(NR_fsync, fd); (void)SC1(NR_close, fd); }
    }
    if (hs_rt_fault_reenter) hs_rt_fault_reenter(why, seq, led);
    hs_fmt(e_seq, sizeof e_seq, "HS_FAULT_SEQ=%u", seq);
    hs_fmt(e_led, sizeof e_led, "HS_FAULT_LEDGER=%016llx", (unsigned long long)led);
    if (hs_envp) for (i = 0; hs_envp[i] && k < 68; i++) {
        const char *e = hs_envp[i];
        if (hs_pfx(e, "HS_FAULT_SEQ=") || hs_pfx(e, "HS_FAULT_LEDGER=")) continue;
        envv[k++] = hs_envp[i];
    }
    envv[k++] = e_seq; envv[k++] = e_led; envv[k] = 0;
    { long ts[2] = { 1, 0 }; (void)SC2(NR_nanosleep, ts, 0); }   /* back-off */
    if (hs_argv && hs_rt_fault_reexec_path) (void)SC3(221 /* execve */, hs_rt_fault_reexec_path, hs_argv, envv);
    hs_idle_forever();
    for (;;) { }
}

void hs_trap(const char *why) {
    SC3(NR_write, 2, why, strlen(why));
    if (hs_is_pid1()) hs_rt_fault(why, 134);
    SC1(NR_exit_group, 134);
    for (;;) { }
}

/* ---- vDSO clock_gettime lookup (ELF64 DT_HASH walk); falls back to the syscall ---- */
typedef struct { unsigned char id[16]; uint16_t type, mach; uint32_t ver; uint64_t entry, phoff, shoff;
                 uint32_t flags; uint16_t ehsz, phentsz, phnum, shentsz, shnum, shstrndx; } e_hdr;
typedef struct { uint32_t type, flags; uint64_t off, vaddr, paddr, filesz, memsz, align; } e_phdr;
typedef struct { int64_t tag; uint64_t val; } e_dyn;
typedef struct { uint32_t name; unsigned char info, other; uint16_t shndx; uint64_t value, size; } e_sym;

static int str_eq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void vdso_init(uintptr_t base) {
    const e_hdr *eh = (const e_hdr *)base;
    const e_phdr *ph;
    const e_dyn *dyn = 0;
    uintptr_t bias = 0;
    int have_load = 0, i;
    const char *strtab = 0;
    const e_sym *symtab = 0;
    const uint32_t *hash = 0;
    if (base == 0 || eh->id[0] != 0x7f || eh->id[1] != 'E') return;
    ph = (const e_phdr *)(base + eh->phoff);
    for (i = 0; i < eh->phnum; i++) {
        if (ph[i].type == 1 && !have_load) { bias = base + ph[i].off - ph[i].vaddr; have_load = 1; }
        else if (ph[i].type == 2) dyn = (const e_dyn *)(base + ph[i].off);
    }
    if (!have_load || !dyn) return;
    for (; dyn->tag; dyn++) {
        if (dyn->tag == 5) strtab = (const char *)(bias + dyn->val);
        else if (dyn->tag == 6) symtab = (const e_sym *)(bias + dyn->val);
        else if (dyn->tag == 4) hash = (const uint32_t *)(bias + dyn->val);
    }
    if (!strtab || !symtab || !hash) return;
    for (i = 0; i < (int)hash[1]; i++) {
        const e_sym *s = &symtab[i];
        if ((s->info & 0xf) != 2 || s->shndx == 0) continue;
        if (str_eq(strtab + s->name, "__kernel_clock_gettime")) { hs_cgt = (hs_vdso_cgt)(bias + s->value); return; }
    }
}

static unsigned long *hs_auxv;
/* -fstack-protector (toolchain default): the canary word and its failure path. */
uintptr_t __stack_chk_guard = 0x48530000ff0a0000u;
void __stack_chk_fail(void) __attribute__((noreturn));
void __stack_chk_fail(void) { hs_trap("*** stack smashing detected ***: terminated\n"); }
typedef void (*hs_ctor)(int, char **, char **);
extern hs_ctor __init_array_start[] __attribute__((weak, visibility("hidden")));
extern hs_ctor __init_array_end[] __attribute__((weak, visibility("hidden")));

unsigned long __getauxval(unsigned long type) {
    unsigned long *a = hs_auxv;
    if (a) for (; a[0]; a += 2) if (a[0] == type) return a[1];
    hs_self()->err = 2; /* ENOENT */
    return 0;
}
unsigned long getauxval(unsigned long type) { return __getauxval(type); }

void hs_start_c(unsigned long *sp);
void hs_start_c(unsigned long *sp) {
    int argc = (int)sp[0];
    char **argv = (char **)(sp + 1);
    char **envp = argv + argc + 1;
    char **e = envp;
    unsigned long *aux;
    hs_main_thr.self = &hs_main_thr;
    __asm__ volatile("msr tpidr_el0, %0" :: "r"(&hs_main_thr) : "memory");
    while (*e) e++;
    aux = (unsigned long *)(e + 1);
    hs_envp = envp;
    hs_argv = argv;
    hs_auxv = aux;
    for (; aux[0]; aux += 2) if (aux[0] == 25 /* AT_RANDOM */) {
        uintptr_t g; memcpy(&g, (const void *)aux[1], sizeof g);
        __stack_chk_guard = g & ~(uintptr_t)0xff;   /* low byte 0, as glibc */
    }
    aux = hs_auxv;
    for (; aux[0]; aux += 2) if (aux[0] == 33) vdso_init((uintptr_t)aux[1]);
    {   /* static constructors (none in HandshakeOS objects; libgcc may add one) */
        size_t n = (size_t)(__init_array_end - __init_array_start), i;
        for (i = 0; i < n; i++) __init_array_start[i](argc, argv, envp);
    }
    exit(main(argc, argv, envp));
}

void exit(int code) {
    hs_stdio_flush_all();
    if (hs_is_pid1()) hs_rt_fault("hs_rt: exit() called in PID 1\n", code);   /* REVIEW HS-O1 */
    for (;;) SC1(NR_exit_group, code);
}

char *getenv(const char *name) {
    size_t n = strlen(name);
    char **e;
    if (!hs_envp) return 0;
    for (e = hs_envp; *e; e++) {
        const char *a = *e, *b = name;
        size_t i = 0;
        while (i < n && a[i] == b[i]) i++;
        if (i == n && a[n] == '=') return (char *)a + n + 1;
    }
    return 0;
}

/* ---- thin syscall wrappers (glibc return conventions) ---- */
int open(const char *p, int flags, ...) {
    va_list ap; unsigned mode = 0;
    va_start(ap, flags);
    if ((flags & 0100) || (flags & 020040000) == 020040000) mode = va_arg(ap, unsigned);
    va_end(ap);
    return (int)hs_ret(SC4(NR_openat, HS_AT_FDCWD, p, flags, mode));
}
long read(int fd, void *b, size_t n) { return hs_ret(SC3(NR_read, fd, b, n)); }
long write(int fd, const void *b, size_t n) { return hs_ret(SC3(NR_write, fd, b, n)); }
int close(int fd) { return (int)hs_ret(SC1(NR_close, fd)); }
long lseek(int fd, long off, int whence) { return hs_ret(SC3(NR_lseek, fd, off, whence)); }
int fstat(int fd, void *st) { return (int)hs_ret(SC2(NR_fstat, fd, st)); }
long __read_chk(int fd, void *b, size_t n, size_t dlen) { (void)dlen; return read(fd, b, n); }
int ioctl(int fd, unsigned long req, ...) {
    va_list ap; void *arg;
    va_start(ap, req); arg = va_arg(ap, void *); va_end(ap);
    return (int)hs_ret(SC3(NR_ioctl, fd, req, arg));
}
int access(const char *p, int mode) { return (int)hs_ret(SC4(NR_faccessat, HS_AT_FDCWD, p, mode, 0)); }
int mkdir(const char *p, unsigned mode) { return (int)hs_ret(SC3(NR_mkdirat, HS_AT_FDCWD, p, mode)); }
int unlink(const char *p) { return (int)hs_ret(SC3(NR_unlinkat, HS_AT_FDCWD, p, 0)); }
int rename(const char *a, const char *b) { return (int)hs_ret(SC5(NR_renameat2, HS_AT_FDCWD, a, HS_AT_FDCWD, b, 0)); }
int stat(const char *p, void *st) { return (int)hs_ret(SC4(NR_newfstatat, HS_AT_FDCWD, p, st, 0)); }
int fsync(int fd) { return (int)hs_ret(SC1(NR_fsync, fd)); }
void sync(void) { SC0(NR_sync); }
int getpid(void) { return (int)SC0(NR_getpid); }
int mount(const char *s, const char *t, const char *type, unsigned long fl, const void *d) {
    return (int)hs_ret(SC5(NR_mount, s, t, type, fl, d));
}
/* v5.3 USB transfer: release / re-attach the FAT partition, link the configfs function. */
int umount2(const char *t, int fl) { return (int)hs_ret(SC2(NR_umount2, t, fl)); }
int umount(const char *t) { return (int)hs_ret(SC2(NR_umount2, t, 0)); }
int symlink(const char *a, const char *b) { return (int)hs_ret(SC3(NR_symlinkat, a, HS_AT_FDCWD, b)); }
/* v5.3 test harness (test-usb-gadget, linked like the shipped binaries) */
int rmdir(const char *p) { return (int)hs_ret(SC3(NR_unlinkat, HS_AT_FDCWD, p, 0x200 /* AT_REMOVEDIR */)); }
long readlink(const char *p, char *b, size_t n) { return hs_ret(SC4(NR_readlinkat, HS_AT_FDCWD, p, b, n)); }
int reboot(int how) { return (int)hs_ret(SC4(NR_reboot, 0xfee1dead, 672274793, how, 0)); }
void *mmap(void *a, size_t n, int prot, int fl, int fd, long off) {
    long r = SC6(NR_mmap, a, n, prot, fl, fd, off);
    if (r < 0 && r > -4096) { hs_self()->err = (int)-r; return (void *)-1; }
    return (void *)r;
}
int munmap(void *a, size_t n) { return (int)hs_ret(SC2(NR_munmap, a, n)); }
int ppoll(void *fds, unsigned long nfds, const void *ts, const void *sigmask) {
    return (int)hs_ret(SC5(NR_ppoll, fds, nfds, ts, sigmask, 8));
}
long syscall(long n, long a, long b, long c, long d, long e, long f) {
    /* AAPCS64 passes variadic integer arguments in x1..x6 exactly like these. */
    return hs_ret(hs_sc(n, a, b, c, d, e, f));
}

struct hs_ts { long sec, nsec; };
int clock_gettime(int clk, struct hs_ts *ts) {
    long r;
    if (hs_cgt) r = hs_cgt(clk, ts);
    else r = SC2(NR_clock_gettime, clk, ts);
    return (int)hs_ret(r);
}
int clock_nanosleep(int clk, int flags, const struct hs_ts *req, struct hs_ts *rem) {
    long r = SC4(NR_clock_nanosleep, clk, flags, req, rem);
    return r < 0 ? (int)-r : 0;    /* returns the error number, errno untouched */
}
int usleep(unsigned us) {
    struct hs_ts ts;
    ts.sec = us / 1000000u;
    ts.nsec = (long)(us % 1000000u) * 1000;
    return (int)hs_ret(SC2(NR_nanosleep, &ts, 0));
}

/* ---- threads: raw clone, per-thread descriptor at the top of an mmap'd stack ---- */
long hs_clone(unsigned long flags, void *sp, int *ptid, void *tls, int *ctid, void *(*fn)(void *), void *arg);

static void *hs_thread_entry(void *p) {
    hs_thr *t = (hs_thr *)p;
    t->ret = t->fn(t->arg);
    return 0;
}

#define HS_STACK (8u << 20)
#define HS_GUARD 65536u
int pthread_create(unsigned long *th, const void *attr, void *(*fn)(void *), void *arg) {
    size_t len = HS_STACK + HS_GUARD + 4096;
    unsigned char *m;
    hs_thr *t;
    long r;
    (void)attr;
    m = (unsigned char *)mmap(0, len, 3, 0x22 | 0x20000 /* PRIVATE|ANON|STACK */, -1, 0);
    if (m == (void *)-1) return 11; /* EAGAIN */
    if (SC3(226 /* mprotect */, m, HS_GUARD, 0) != 0) { munmap(m, len); return 11; } /* REVIEW HS-L3 */
    t = (hs_thr *)(m + len - 4096);
    t->self = t;
    t->fn = fn;
    t->arg = arg;
    t->map = m;
    t->map_len = len;
    t->tid = 0;
    r = hs_clone(0x100 | 0x200 | 0x400 | 0x800 | 0x10000 | 0x40000 | 0x80000 | 0x100000 | 0x200000,
                 (void *)((uintptr_t)t & ~(uintptr_t)15), (int *)&t->tid, t, (int *)&t->tid,
                 hs_thread_entry, t);
    if (r < 0) { munmap(m, len); return (int)-r; }
    *th = (unsigned long)t;
    return 0;
}

int pthread_join(unsigned long th, void **ret) {
    hs_thr *t = (hs_thr *)th;
    struct hs_ts ts = { 0, 200000 };
    /* Join: poll the tid word the kernel zeroes when the thread has exited.
       Sleep-poll, deliberately not FUTEX_WAIT. */
    while (__atomic_load_n(&t->tid, __ATOMIC_ACQUIRE) != 0) SC2(NR_nanosleep, &ts, 0);
    if (ret) *ret = t->ret;
    munmap(t->map, t->map_len);
    return 0;
}
