/* SPDX-License-Identifier: MIT */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "hs_gb.h"

#include <stdio.h>
#include <string.h>

#ifndef ROM_PATH
#define ROM_PATH "roms/handshake-demo.gb"
#endif

#ifndef GB_FRAMES
#define GB_FRAMES 90
#endif

#ifndef GB_RUNS
#define GB_RUNS 100
#endif

static int fail_count;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        fail_count++; \
    } \
} while (0)

int main(void)
{
    struct hs_gb_stats first, st;
    int i, w, rc;

    memset(&first, 0, sizeof(first));
    printf("handshake-gb-test  rom=%s  frames=%d  runs=%d\n",
           ROM_PATH, GB_FRAMES, GB_RUNS);

    for (i = 0; i < GB_RUNS; i++) {
        rc = hs_gb_scoreboard(ROM_PATH, 3, GB_FRAMES, &st);
        CHECK(rc == 0);
        if (rc != 0) {
            break;
        }
        CHECK(st.xruns == 0);
        CHECK(st.overruns == 0);
        CHECK(st.frames == (uint64_t)GB_FRAMES);
        CHECK(st.epoch == (uint64_t)GB_FRAMES);
        if (i == 0) {
            first = st;
            printf("golden ledger=%016llx frame=%016llx audio=%016llx "
                   "epoch=%llu xruns=%llu overruns=%llu\n",
                   (unsigned long long)st.ledger_hash,
                   (unsigned long long)st.frame_crc,
                   (unsigned long long)st.audio_crc,
                   (unsigned long long)st.epoch,
                   (unsigned long long)st.xruns,
                   (unsigned long long)st.overruns);
        } else {
            CHECK(st.ledger_hash == first.ledger_hash);
            CHECK(st.frame_crc == first.frame_crc);
            CHECK(st.audio_crc == first.audio_crc);
        }
    }
    printf("%d/%d identical ledger+frame+audio\n", GB_RUNS, GB_RUNS);

    printf("fairness:");
    for (w = 1; w <= 4; w++) {
        rc = hs_gb_scoreboard(ROM_PATH, w, GB_FRAMES, &st);
        CHECK(rc == 0);
        CHECK(st.ledger_hash == first.ledger_hash);
        CHECK(st.frame_crc == first.frame_crc);
        CHECK(st.audio_crc == first.audio_crc);
        CHECK(st.xruns == 0);
        CHECK(st.overruns == 0);
        printf("  %d=%s", w, (st.ledger_hash == first.ledger_hash) ? "ok" : "FAIL");
    }
    printf("\n");

    if (fail_count == 0) {
        printf("ALL GB TESTS PASSED\n");
        return 0;
    }
    printf("%d checks failed\n", fail_count);
    return 1;
}
