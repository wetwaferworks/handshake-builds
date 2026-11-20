/* SPDX-License-Identifier: MIT */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hs_handshake.h"
#include "hs_scoreboard.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    struct hs_hs hs;
    struct hs_jitter_stats jit;
    struct hs_hs_stats st;
    uint64_t hash;
    int workers = 3;
    unsigned b;

    if (argc > 1) {
        workers = atoi(argv[1]);
    }
    if (hs_scoreboard_setup(&hs, workers) != 0) {
        fprintf(stderr, "setup failed\n");
        return 1;
    }
    hash = hs_scoreboard_run(&hs);
    hs_hs_stats(&hs, &st);
    jit = st.jitter;

    printf("HandshakeOS timing handshake jitter bench\n");
    printf("workers=%d  phases=%llu  ledger=%016llx  epoch=%llu  monatomic=%llu\n",
           hs_hs_worker_count(&hs),
           (unsigned long long)st.phases,
           (unsigned long long)hash,
           (unsigned long long)hs_hs_commit_epoch(&hs),
           (unsigned long long)hs_hs_monatomic(&hs));
    printf("open-to-commit  p50=%lld ns  p95=%lld ns  p99=%lld ns  min=%lld ns  max=%lld ns  mean=%lld ns\n",
           (long long)jit.p50_ns, (long long)jit.p95_ns, (long long)jit.p99_ns,
           (long long)jit.min_ns, (long long)jit.max_ns, (long long)jit.mean_ns);
    printf("overruns=%llu  overrun_ns_total=%llu  barrier_wait_ns_total=%llu  freezes=%llu\n",
           (unsigned long long)st.overrun_phases,
           (unsigned long long)st.overrun_ns_total,
           (unsigned long long)st.barrier_wait_ns_total,
           (unsigned long long)st.freezes);
    printf("histogram:\n");
    for (b = 0; b < HS_HS_HIST_BINS; b++) {
        if (jit.hist[b] != 0) {
            printf("  %-10s %u\n", hs_hist_bin_label(b), jit.hist[b]);
        }
    }
    hs_hs_fini(&hs);
    return hash == 0 ? 1 : 0;
}
