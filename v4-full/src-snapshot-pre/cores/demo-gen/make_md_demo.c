/* SPDX-License-Identifier: MIT
 * make_md_demo.c - writes a HandshakeOS v4-multi built-in demo ROM by
 * reusing, verbatim, the clean-room synthetic ROM builder verified by the
 * hs-genesis build_rom (planes + window + sprites + YM2612 + PSG) test suite. Host tool only (writes one file). */
#include <stdio.h>
#define main hs_gen_test_main
#include "test_hs_genesis.c"
#undef main

/* The generator never benchmarks; the test's timing hook is a stub. */
uint64_t hs_bench_now_ns(void) { return 0; }

int main(int argc, char **argv)
{
    static uint8_t rom[ROM_SIZE];
    FILE *fp;
    int ok;
    if (argc != 2) { fprintf(stderr, "usage: %s out\n", argv[0]); return 2; }
    build_rom(rom);
    fp = fopen(argv[1], "wb");
    if (fp == NULL) return 1;
    ok = fwrite(rom, 1, sizeof rom, fp) == sizeof rom;
    if (fclose(fp) != 0 || !ok) return 1;
    printf("wrote %s (%u bytes)\n", argv[1], (unsigned)sizeof rom);
    (void)hs_gen_test_main;
    return 0;
}
