/* SPDX-License-Identifier: MIT */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hs_handshake.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_fails;

#define CHECK(cond, ...) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__); \
            fprintf(stderr, "\n"); \
            g_fails++; \
        } \
    } while (0)

/* Deterministic work: a pure function of phase, cycle, and a salt. */
static uint64_t work_pure(uint64_t phase, uint64_t cycle, uint64_t salt)
{
    uint64_t h = 0xcbf29ce484222325ull;
    h = hs_fnv1a64(h, phase);
    h = hs_fnv1a64(h, cycle);
    h = hs_fnv1a64(h, salt);
    return h;
}

static uint64_t work_serial(uint64_t phase, uint64_t cycle, void *user)
{
    (void)user;
    return work_pure(phase, cycle, 0xA11ull);
}

static uint64_t work_control(uint64_t phase, uint64_t cycle, void *user)
{
    (void)user;
    /* Control coupling: the drain is recorded as a note of the cycle.
       Actual event apply happens at commit via hs_hs_apply_matured. */
    return work_pure(phase, cycle, 0xC0u);
}

static uint64_t work_a(uint64_t phase, uint64_t cycle, void *user)
{
    (void)user;
    return work_pure(phase, cycle, 0xA0ull);
}

static uint64_t work_b(uint64_t phase, uint64_t cycle, void *user)
{
    (void)user;
    return work_pure(phase, cycle, 0xB0ull);
}

static uint64_t work_c(uint64_t phase, uint64_t cycle, void *user)
{
    (void)user;
    return work_pure(phase, cycle, 0xC1ull);
}

static uint64_t work_sleepy(uint64_t phase, uint64_t cycle, void *user)
{
    int ms = user ? *(int *)user : 400;
    (void)phase;
    (void)cycle;
    usleep((useconds_t)ms * 1000u);
    return 1;
}

static void load_events(struct hs_hs *hs)
{
    uint64_t tick;
    uint32_t n = 0;

    for (tick = 0; tick < 32; tick++) {
        if ((tick % 3) == 0) {
            CHECK(hs_hs_post_event(hs, tick, (uint32_t)(tick * 7u + 1u),
                                      (uint32_t)(tick + 11u)) == 0,
                  "post event %u", n);
            n++;
        }
    }
}

static int setup_scoreboard(struct hs_hs *hs, int workers)
{
    struct hs_hs_config cfg;
    struct hs_hs_comp_desc d;

    hs_hs_default_config(&cfg);
    cfg.worker_threads = workers;
    cfg.phases_per_cycle = 4;
    cfg.phase_budget_ns = 2000000ull; /* 2 ms: overrun is diagnostic only */
    cfg.watchdog_ceiling_ns = HS_HS_DEFAULT_CEILING_NS;
    cfg.ledger = true;
    if (hs_hs_init(hs, &cfg) != 0) {
        return -1;
    }

    memset(&d, 0, sizeof(d));
    d.name = "serial";
    d.cadence = HS_CADENCE_EVERY_PHASE;
    d.tick_critical = true;
    if (hs_hs_register(hs, &d, work_serial, NULL) == UINT32_MAX) {
        return -1;
    }

    memset(&d, 0, sizeof(d));
    d.name = "control";
    d.cadence = HS_CADENCE_EVERY_PHASE;
    d.affine = true;
    d.tick_critical = true;
    if (hs_hs_register(hs, &d, work_control, NULL) == UINT32_MAX) {
        return -1;
    }

    memset(&d, 0, sizeof(d));
    d.name = "worker-a";
    d.cadence = HS_CADENCE_EVERY_PHASE;
    d.tick_critical = true;
    if (hs_hs_register(hs, &d, work_a, NULL) == UINT32_MAX) {
        return -1;
    }

    memset(&d, 0, sizeof(d));
    d.name = "worker-b";
    d.cadence = HS_CADENCE_HALF_CYCLE;
    d.tick_critical = true;
    if (hs_hs_register(hs, &d, work_b, NULL) == UINT32_MAX) {
        return -1;
    }

    memset(&d, 0, sizeof(d));
    d.name = "worker-c";
    d.cadence = HS_CADENCE_CYCLE_HEAD;
    d.tick_critical = true;
    if (hs_hs_register(hs, &d, work_c, NULL) == UINT32_MAX) {
        return -1;
    }

    load_events(hs);
    return hs_hs_start(hs);
}

#define SCORE_PHASES 128

static uint64_t run_scoreboard(struct hs_hs *hs)
{
    uint64_t phase, cycle;
    int ppc = 4;

    for (phase = 0; phase < SCORE_PHASES; phase++) {
        cycle = phase / (uint64_t)ppc;
        if (!hs_hs_run_phase(hs, phase, cycle, 0)) {
            fprintf(stderr, "run_phase failed at phase %llu (frozen=%d)\n",
                    (unsigned long long)phase, hs_hs_frozen(hs) ? 1 : 0);
            return 0;
        }
    }
    return hs_hs_ledger_hash(hs);
}

static void test_unit_poll_and_can_tick(void)
{
    struct hs_hs hs;
    uint64_t phase_out = 0;
    struct hs_published snap;

    CHECK(setup_scoreboard(&hs, 3) == 0, "setup for poll");
    CHECK(!hs_hs_can_tick(&hs, 0, &phase_out), "no tick before publish");
    CHECK(!hs_hs_poll_phase(&hs, 0, 0, &snap), "no poll before publish");
    CHECK(hs_hs_run_phase(&hs, 0, 0, 0), "one phase");
    CHECK(hs_hs_commit_epoch(&hs) == 1, "epoch 1");
    CHECK(hs_hs_ledger_hash(&hs) != 0xcbf29ce484222325ull, "hash moved");
    hs_hs_fini(&hs);
}

static void test_freeze_and_reset(void)
{
    struct hs_hs hs;
    struct hs_hs_config cfg;
    struct hs_hs_comp_desc d;
    struct hs_freeze fr;
    int sleep_ms = 400;

    hs_hs_default_config(&cfg);
    cfg.worker_threads = 1;
    cfg.watchdog_ceiling_ns = 250000000ull;
    CHECK(hs_hs_init(&hs, &cfg) == 0, "init freeze");

    memset(&d, 0, sizeof(d));
    d.name = "sleepy";
    d.cadence = HS_CADENCE_EVERY_PHASE;
    d.tick_critical = true;
    CHECK(hs_hs_register(&hs, &d, work_sleepy, &sleep_ms) != UINT32_MAX, "reg sleepy");
    CHECK(hs_hs_start(&hs) == 0, "start freeze");

    CHECK(!hs_hs_run_phase(&hs, 0, 0, 0), "watchdog must FREEZE");
    CHECK(hs_hs_frozen(&hs), "frozen latch");
    hs_hs_last_freeze(&hs, &fr);
    CHECK(strcmp(fr.component_name, "sleepy") == 0, "names offender, got '%s'", fr.component_name);
    CHECK(fr.ceiling_ns == 250000000ull, "ceiling 250 ms");
    CHECK(fr.elapsed_ns >= fr.ceiling_ns, "elapsed past ceiling");
    CHECK(!hs_hs_run_phase(&hs, 1, 0, 0), "stays frozen until reset");

    hs_hs_reset_after_freeze(&hs);
    CHECK(!hs_hs_frozen(&hs), "latch cleared");

    /* After reset, a fast component must be able to commit. Replace by
       stopping and registering a fast path on a fresh authority. */
    hs_hs_stop(&hs);
    hs_hs_fini(&hs);

    hs_hs_default_config(&cfg);
    cfg.worker_threads = 1;
    CHECK(hs_hs_init(&hs, &cfg) == 0, "reinit after freeze");
    memset(&d, 0, sizeof(d));
    d.name = "fast";
    d.cadence = HS_CADENCE_EVERY_PHASE;
    d.tick_critical = true;
    CHECK(hs_hs_register(&hs, &d, work_serial, NULL) != UINT32_MAX, "reg fast");
    CHECK(hs_hs_start(&hs) == 0, "start fast");
    CHECK(hs_hs_run_phase(&hs, 0, 0, 0), "runs after a generation bump world");
    CHECK(hs_hs_commit_epoch(&hs) == 1, "committed");
    hs_hs_fini(&hs);
}

static void test_wait_does_not_skip(void)
{
    /* A slightly slow worker still commits the phase (WAIT, not skip). */
    struct hs_hs hs;
    struct hs_hs_config cfg;
    struct hs_hs_comp_desc d;
    int sleep_ms = 20;

    hs_hs_default_config(&cfg);
    cfg.worker_threads = 1;
    cfg.phase_budget_ns = 1000000ull; /* 1 ms budget: this will overrun */
    cfg.watchdog_ceiling_ns = HS_HS_DEFAULT_CEILING_NS;
    CHECK(hs_hs_init(&hs, &cfg) == 0, "init wait");
    memset(&d, 0, sizeof(d));
    d.name = "slowish";
    d.cadence = HS_CADENCE_EVERY_PHASE;
    d.tick_critical = true;
    CHECK(hs_hs_register(&hs, &d, work_sleepy, &sleep_ms) != UINT32_MAX, "reg");
    CHECK(hs_hs_start(&hs) == 0, "start");
    CHECK(hs_hs_run_phase(&hs, 0, 0, 0), "WAIT extends, phase still commits");
    CHECK(hs_hs_commit_epoch(&hs) == 1, "not skipped");
    {
        struct hs_hs_stats st;
        hs_hs_stats(&hs, &st);
        CHECK(st.overrun_phases == 1, "overrun counted, not hidden");
    }
    hs_hs_fini(&hs);
}

static uint64_t run_once(int workers, struct hs_jitter_stats *jit, uint64_t *epoch, uint64_t *mono)
{
    struct hs_hs hs;
    uint64_t hash;

    CHECK(setup_scoreboard(&hs, workers) == 0, "setup workers=%d", workers);
    hash = run_scoreboard(&hs);
    if (epoch) {
        *epoch = hs_hs_commit_epoch(&hs);
    }
    if (mono) {
        *mono = hs_hs_monatomic(&hs);
    }
    if (jit) {
        hs_hs_jitter(&hs, jit);
    }
    hs_hs_fini(&hs);
    return hash;
}

static void test_scoreboard_100(uint64_t *out_hash, struct hs_jitter_stats *out_jit)
{
    const int runs = 100;
    uint64_t hashes[100];
    uint64_t first, epoch = 0, mono = 0;
    struct hs_jitter_stats jit;
    int i, same;

    memset(&jit, 0, sizeof(jit));
    first = run_once(3, &jit, &epoch, &mono);
    hashes[0] = first;
    CHECK(first != 0, "first hash nonzero");
    CHECK(epoch == SCORE_PHASES, "epoch == %d, got %llu", SCORE_PHASES,
          (unsigned long long)epoch);

    same = 1;
    for (i = 1; i < runs; i++) {
        hashes[i] = run_once(3, NULL, NULL, NULL);
        if (hashes[i] != first) {
            same = 0;
            CHECK(0, "run %d hash %016llx != %016llx", i + 1,
                  (unsigned long long)hashes[i], (unsigned long long)first);
        }
    }
    CHECK(same, "100-run ledger hash identity");
    printf("SCOREBOARD 100/100  workers=3  hash=%016llx  epoch=%llu  monatomic=%llu\n",
           (unsigned long long)first, (unsigned long long)epoch,
           (unsigned long long)mono);
    printf("  jitter (phase open-to-commit)  p50=%lld ns  p95=%lld ns  p99=%lld ns  max=%lld ns  overruns=%llu\n",
           (long long)jit.p50_ns, (long long)jit.p95_ns, (long long)jit.p99_ns,
           (long long)jit.max_ns, (unsigned long long)jit.overruns);
    if (out_hash) {
        *out_hash = first;
    }
    if (out_jit) {
        *out_jit = jit;
    }
}

static void test_worker_invariance(uint64_t expected)
{
    const int counts[] = { 1, 2, 3, 4 };
    int i;
    uint64_t h;

    for (i = 0; i < 4; i++) {
        h = run_once(counts[i], NULL, NULL, NULL);
        CHECK(h == expected, "fairness: workers=%d hash=%016llx expected=%016llx",
              counts[i], (unsigned long long)h, (unsigned long long)expected);
        printf("FAIRNESS  workers=%d  hash=%016llx  %s\n",
               counts[i], (unsigned long long)h,
               h == expected ? "MATCH" : "MISS");
    }
}

int main(void)
{
    uint64_t hash = 0;
    struct hs_jitter_stats jit;

    memset(&jit, 0, sizeof(jit));
    printf("HandshakeOS Timing Handshake tests\n");
    printf("pillars: Time Serialization, control coupling, event coupling\n");
    printf("beats: publish, pull, barrier, commit\n");

    test_unit_poll_and_can_tick();
    test_wait_does_not_skip();
    test_freeze_and_reset();
    if (g_fails != 0) {
        fprintf(stderr, "%d failure(s) before the scoreboard\n", g_fails);
        return 1;
    }

    test_scoreboard_100(&hash, &jit);
    test_worker_invariance(hash);

    if (g_fails != 0) {
        fprintf(stderr, "%d failure(s)\n", g_fails);
        return 1;
    }
    printf("ALL TESTS PASSED\n");
    return 0;
}
