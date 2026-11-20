/* hs_rt entry, thread trampoline and outline atomics (MIT). LL/SC atomic adds only: no lock code. */
    .text
    .global _start
    .type _start, %function
_start:
    mov x29, #0
    mov x30, #0
    mov x0, sp
    and sp, x0, #~15
    bl hs_start_c
    brk #0
    .size _start, .-_start

/* long hs_clone(flags x0, newsp x1, ptid x2, tls x3, ctid x4, fn x5, arg x6) */
    .global hs_clone
    .type hs_clone, %function
hs_clone:
    stp x5, x6, [x1, #-16]!
    mov x8, #220
    svc #0
    cbz x0, 1f
    ret
1:  mov x29, #0
    mov x30, #0
    ldp x1, x0, [sp], #16
    blr x1
    mov x8, #93          /* exit (this thread only); kernel clears + wakes ctid */
    svc #0
    brk #0
    .size hs_clone, .-hs_clone

/* outline atomics used by the objects (built for cortex-a53: no LSE). x0 = value, x1 = ptr, returns old */
.macro LDADD name, w, ldx, stx
    .global \name
    .type \name, %function
\name:
    mov \w\()16, \w\()0
1:  \ldx \w\()0, [x1]
    add \w\()17, \w\()0, \w\()16
    \stx w15, \w\()17, [x1]
    cbnz w15, 1b
    ret
    .size \name, .-\name
.endm
    LDADD __aarch64_ldadd4_acq_rel, w, ldaxr, stlxr
    LDADD __aarch64_ldadd4_rel,     w, ldxr,  stlxr
    LDADD __aarch64_ldadd8_acq_rel, x, ldaxr, stlxr
    LDADD __aarch64_ldadd8_rel,     x, ldxr,  stlxr
    LDADD __aarch64_ldadd8_relax,   x, ldxr,  stxr

    .section .note.GNU-stack,"",%progbits
