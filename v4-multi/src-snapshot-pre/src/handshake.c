/* SPDX-License-Identifier: MIT */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hs_handshake.h"

#include <errno.h>
#include <limits.h>
#include <linux/futex.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

_Static_assert(sizeof(pthread_t) <= sizeof(void *),
               "pthread_t must fit in worker_threads");

static const char *const k_beat_names[] = {
    "idle", "publish", "pull", "barrier", "commit"
};

static const char *const k_hist_labels[HS_HS_HIST_BINS] = {
    "<1us", "1-2us", "2-5us", "5-10us", "10-20us", "20-50us",
    "50-100us", "100-200us", "200-500us", "500us-1ms", "1-2ms", ">=2ms"
};

static const int64_t k_hist_bound[HS_HS_HIST_BINS] = {
    1000, 2000, 5000, 10000, 20000, 50000,
    100000, 200000, 500000, 1000000, 2000000, INT64_MAX
};

uint64_t hs_now_ns(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

uint64_t hs_fnv1a64(uint64_t hash, uint64_t word)
{
    unsigned i;

    if (hash == 0) {
        hash = 0xcbf29ce484222325ull;
    }
    for (i = 0; i < 8; i++) {
        hash ^= (word >> (i * 8)) & 0xFFull;
        hash *= 0x100000001b3ull;
    }
    return hash;
}

const char *hs_beat_name(hs_beat_t beat)
{
    if ((unsigned)beat >= (unsigned)(sizeof(k_beat_names) / sizeof(k_beat_names[0]))) {
        return "unknown";
    }
    return k_beat_names[beat];
}

const char *hs_hist_bin_label(unsigned bin)
{
    if (bin >= HS_HS_HIST_BINS) {
        return "?";
    }
    return k_hist_labels[bin];
}

void hs_hs_default_config(struct hs_hs_config *cfg)
{
    if (cfg == NULL) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->phases_per_cycle = 4;
    cfg->worker_threads = 3;
    cfg->phase_budget_ns = 0;
    cfg->watchdog_ceiling_ns = HS_HS_DEFAULT_CEILING_NS;
    cfg->offender_threshold = 3;
    cfg->ledger = true;
}

static int futex_wait(_Atomic uint32_t *word, uint32_t expected)
{
    return (int)syscall(SYS_futex, word, FUTEX_WAIT_PRIVATE, expected, NULL, NULL, 0);
}

static int futex_wait_ns(_Atomic uint32_t *word, uint32_t expected, uint64_t timeout_ns)
{
    struct timespec ts;

    ts.tv_sec = (time_t)(timeout_ns / 1000000000ull);
    ts.tv_nsec = (long)(timeout_ns % 1000000000ull);
    return (int)syscall(SYS_futex, word, FUTEX_WAIT_PRIVATE, expected, &ts, NULL, 0);
}

static void futex_wake(_Atomic uint32_t *word, int n)
{
    (void)syscall(SYS_futex, word, FUTEX_WAKE_PRIVATE, n, NULL, NULL, 0);
}

static void set_beat(struct hs_hs *hs, hs_beat_t beat)
{
    atomic_store_explicit(&hs->beat, (uint32_t)beat, memory_order_release);
}

int hs_hs_init(struct hs_hs *hs, const struct hs_hs_config *cfg)
{
    struct hs_hs_config def;
    int workers;

    if (hs == NULL) {
        return -EINVAL;
    }
    memset(hs, 0, sizeof(*hs));
    if (cfg == NULL) {
        hs_hs_default_config(&def);
        cfg = &def;
    }
    hs->cfg = *cfg;
    if (hs->cfg.phases_per_cycle < 1 || hs->cfg.phases_per_cycle > 64) {
        hs->cfg.phases_per_cycle = 4;
    }
    workers = hs->cfg.worker_threads;
    if (workers <= 0) {
        workers = 3;
    }
    if (workers > HS_HS_MAX_WORKERS) {
        workers = HS_HS_MAX_WORKERS;
    }
    hs->worker_count = workers;
    hs->ceiling_ns = hs->cfg.watchdog_ceiling_ns;
    if (hs->ceiling_ns == 0) {
        hs->ceiling_ns = HS_HS_DEFAULT_CEILING_NS;
    }
    if (hs->cfg.offender_threshold == 0) {
        hs->cfg.offender_threshold = 3;
    }
    atomic_init(&hs->pub_seq, 0);
    atomic_init(&hs->generation, 1);
    atomic_init(&hs->beat, HS_BEAT_IDLE);
    atomic_init(&hs->frozen, 0);
    atomic_init(&hs->commit_epoch, 0);
    atomic_init(&hs->ledger_hash, 0xcbf29ce484222325ull);
    atomic_init(&hs->monatomic, 0);
    atomic_init(&hs->arrivals, 0);
    atomic_init(&hs->expected, 0);
    atomic_init(&hs->publish_word, 0);
    atomic_init(&hs->barrier_word, 0);
    atomic_init(&hs->stop_word, 0);
    return 0;
}

void hs_hs_fini(struct hs_hs *hs)
{
    if (hs == NULL) {
        return;
    }
    hs_hs_stop(hs);
    memset(hs, 0, sizeof(*hs));
}

uint32_t hs_hs_register(struct hs_hs *hs,
                           const struct hs_hs_comp_desc *desc,
                           hs_hs_work_fn fn, void *user)
{
    struct hs_hs_slot *s;
    uint32_t id;
    int n_pull;

    if (hs == NULL || desc == NULL || fn == NULL || hs->running) {
        return UINT32_MAX;
    }
    if (hs->n_comps >= HS_HS_MAX_COMPS) {
        return UINT32_MAX;
    }
    id = (uint32_t)hs->n_comps;
    s = &hs->comps[id];
    memset(s, 0, sizeof(*s));
    s->desc = *desc;
    s->fn = fn;
    s->user = user;
    s->id = id;
    if (desc->name != NULL && desc->name[0] != '\0') {
        strncpy(s->name, desc->name, HS_HS_NAME_MAX - 1);
    } else {
        snprintf(s->name, sizeof(s->name), "unnamed-%u", id);
    }
    s->desc.name = s->name;
    s->desc.tick_critical = desc->tick_critical || !desc->affine;
    if (desc->affine) {
        s->shard = -1;
    } else {
        n_pull = 0;
        /* Assign pull shards later in start(), once registration is closed. */
        s->shard = 0;
        (void)n_pull;
    }
    atomic_init(&s->start_stamp_ns, 0);
    atomic_init(&s->duration_ns, 0);
    atomic_init(&s->note, 0);
    atomic_init(&s->started, 0);
    atomic_init(&s->finished, 0);
    hs->n_comps++;
    return id;
}

static void assign_shards(struct hs_hs *hs)
{
    int i;
    int next = 0;

    for (i = 0; i < hs->n_comps; i++) {
        if (hs->comps[i].desc.affine) {
            hs->comps[i].shard = -1;
        } else {
            hs->comps[i].shard = next % hs->worker_count;
            next++;
        }
    }
}

static bool due_for(const struct hs_hs *hs, const struct hs_hs_slot *c,
                    uint64_t phase, uint64_t cycle, uint32_t group)
{
    uint32_t divider;
    uint32_t half;
    int ppc;

    (void)cycle;
    if (c->desc.group != 0 && group != HS_HS_ALL_GROUPS && c->desc.group != group) {
        return false;
    }
    ppc = hs->cfg.phases_per_cycle;
    if (c->desc.phase_divider != 0) {
        divider = c->desc.phase_divider;
        return (phase % (uint64_t)divider) == (uint64_t)c->desc.phase_offset;
    }
    switch (c->desc.cadence) {
    case HS_CADENCE_EVERY_PHASE:
        return true;
    case HS_CADENCE_HALF_CYCLE:
        half = (uint32_t)ppc / 2u;
        if (half < 1u) {
            half = 1u;
        }
        return (phase % (uint64_t)half) == (uint64_t)c->desc.phase_offset;
    case HS_CADENCE_CYCLE_HEAD:
        return (phase % (uint64_t)ppc) == (uint64_t)c->desc.phase_offset;
    default:
        return false;
    }
}

static bool shard_has_due(const struct hs_hs *hs, uint32_t shard,
                          uint64_t phase, uint64_t cycle, uint32_t group)
{
    int i;

    for (i = 0; i < hs->n_comps; i++) {
        if (hs->comps[i].shard == (int)shard
            && due_for(hs, &hs->comps[i], phase, cycle, group)) {
            return true;
        }
    }
    return false;
}

static bool read_published(const struct hs_hs *hs, struct hs_published *out)
{
    uint64_t s1, s2;

    s1 = atomic_load_explicit(&hs->pub_seq, memory_order_acquire);
    if ((s1 & 1ull) != 0) {
        return false;
    }
    out->seq = hs->pub_logical_seq;
    out->generation = hs->pub_generation;
    out->phase = hs->pub_phase;
    out->cycle = hs->pub_cycle;
    out->group = hs->pub_group;
    out->open_stamp_ns = hs->pub_open_stamp_ns;
    out->due_work = false;
    atomic_thread_fence(memory_order_acquire);
    s2 = atomic_load_explicit(&hs->pub_seq, memory_order_acquire);
    return s1 != 0 && s1 == s2;
}

bool hs_hs_poll_phase(const struct hs_hs *hs, uint32_t shard,
                         uint64_t seen_seq, struct hs_published *out)
{
    if (hs == NULL || out == NULL) {
        return false;
    }
    if (!read_published(hs, out)) {
        return false;
    }
    if (out->seq == 0 || out->seq <= seen_seq) {
        return false;
    }
    if (out->generation != atomic_load_explicit(&hs->generation, memory_order_acquire)) {
        return false;
    }
    out->due_work = shard_has_due(hs, shard, out->phase, out->cycle, out->group);
    return true;
}

bool hs_hs_can_tick(const struct hs_hs *hs, uint32_t shard, uint64_t *phase_out)
{
    struct hs_published snap;
    uint64_t seen;

    if (hs == NULL) {
        return false;
    }
    seen = (shard < HS_HS_MAX_WORKERS) ? hs->shard_seen[shard] : 0;
    if (!hs_hs_poll_phase(hs, shard, seen, &snap)) {
        return false;
    }
    if (!snap.due_work) {
        return false;
    }
    if (phase_out != NULL) {
        *phase_out = snap.phase;
    }
    return true;
}

static uint64_t run_item(struct hs_hs *hs, struct hs_hs_slot *c,
                         uint64_t phase, uint64_t cycle, uint64_t generation)
{
    uint64_t t0, t1, note, live;

    live = atomic_load_explicit(&hs->generation, memory_order_acquire);
    if (live != generation || atomic_load_explicit(&hs->frozen, memory_order_acquire)) {
        return 0;
    }
    atomic_store_explicit(&c->started, 1, memory_order_release);
    t0 = hs_now_ns();
    atomic_store_explicit(&c->start_stamp_ns, t0, memory_order_release);
    note = c->fn(phase, cycle, c->user);
    t1 = hs_now_ns();
    live = atomic_load_explicit(&hs->generation, memory_order_acquire);
    if (live != generation) {
        return 0;
    }
    atomic_store_explicit(&c->duration_ns, t1 - t0, memory_order_release);
    atomic_store_explicit(&c->note, note, memory_order_release);
    atomic_store_explicit(&c->finished, 1, memory_order_release);
    atomic_fetch_add_explicit(&hs->monatomic, 1, memory_order_relaxed);
    return note;
}

static void serve_shard(struct hs_hs *hs, uint32_t shard, const struct hs_published *snap)
{
    int i;

    for (i = 0; i < hs->n_comps; i++) {
        if (hs->comps[i].shard != (int)shard) {
            continue;
        }
        if (!due_for(hs, &hs->comps[i], snap->phase, snap->cycle, snap->group)) {
            continue;
        }
        if (atomic_load_explicit(&hs->generation, memory_order_acquire) != snap->generation) {
            return;
        }
        (void)run_item(hs, &hs->comps[i], snap->phase, snap->cycle, snap->generation);
    }
}

static void arrive(struct hs_hs *hs, const struct hs_published *snap)
{
    uint32_t n;
    uint32_t exp;

    if (atomic_load_explicit(&hs->generation, memory_order_acquire) != snap->generation) {
        return;
    }
    if (atomic_load_explicit(&hs->frozen, memory_order_acquire)) {
        return;
    }
    n = atomic_fetch_add_explicit(&hs->arrivals, 1, memory_order_acq_rel) + 1;
    exp = atomic_load_explicit(&hs->expected, memory_order_acquire);
    if (n >= exp) {
        atomic_store_explicit(&hs->barrier_word, n, memory_order_release);
        futex_wake(&hs->barrier_word, 1);
    }
}

typedef struct {
    struct hs_hs *hs;
    uint32_t shard;
} hs_boot_t;

static void *worker_main(void *arg)
{
    hs_boot_t *boot = (hs_boot_t *)arg;
    struct hs_hs *hs = boot->hs;
    uint32_t shard = boot->shard;
    uint64_t seen = 0;
    struct hs_published snap;
    uint32_t word;

    while (!hs->stop) {
        /* FIX (Layer-2 eval): sample the futex word BEFORE polling so a publish
         * landing between a failed poll and futex_wait cannot be lost. */
        word = atomic_load_explicit(&hs->publish_word, memory_order_acquire);
        if (hs_hs_poll_phase(hs, shard, seen, &snap)) {
            set_beat(hs, HS_BEAT_PULL);
            if (snap.due_work) {
                serve_shard(hs, shard, &snap);
            }
            arrive(hs, &snap);
            seen = snap.seq;
            if (shard < HS_HS_MAX_WORKERS) {
                hs->shard_seen[shard] = seen;
            }
            continue;
        }
        if (hs->stop) {
            break;
        }
        futex_wait(&hs->publish_word, word);
    }
    return NULL;
}

int hs_hs_start(struct hs_hs *hs)
{
    int i;
    int rc;
    pthread_t th;

    if (hs == NULL || hs->running) {
        return -EINVAL;
    }
    assign_shards(hs);
    hs->stop = 0;
    atomic_store(&hs->stop_word, 0);
    for (i = 0; i < hs->worker_count; i++) {
        hs->shard_seen[i] = 0;
        hs->boot[i].hs = hs;
        hs->boot[i].shard = (uint32_t)i;
        rc = pthread_create(&th, NULL, worker_main, &hs->boot[i]);
        if (rc != 0) {
            hs->stop = 1;
            atomic_fetch_add(&hs->publish_word, 1);
            futex_wake(&hs->publish_word, INT_MAX);
            while (--i >= 0) {
                pthread_join((pthread_t)(uintptr_t)hs->worker_threads[i], NULL);
            }
            return -rc;
        }
        hs->worker_threads[i] = (void *)(uintptr_t)th;
    }
    hs->running = 1;
    return 0;
}

void hs_hs_stop(struct hs_hs *hs)
{
    int i;
    pthread_t th;

    if (hs == NULL || !hs->running) {
        return;
    }
    hs->stop = 1;
    atomic_fetch_add_explicit(&hs->publish_word, 1, memory_order_release);
    futex_wake(&hs->publish_word, INT_MAX);
    atomic_fetch_add_explicit(&hs->barrier_word, 1, memory_order_release);
    futex_wake(&hs->barrier_word, INT_MAX);
    for (i = 0; i < hs->worker_count; i++) {
        th = (pthread_t)(uintptr_t)hs->worker_threads[i];
        pthread_join(th, NULL);
        hs->worker_threads[i] = NULL;
    }
    hs->running = 0;
}

static void publish(struct hs_hs *hs, uint64_t phase, uint64_t cycle, uint32_t group, uint64_t now)
{
    uint64_t lock;

    set_beat(hs, HS_BEAT_PUBLISH);
    lock = atomic_load_explicit(&hs->pub_seq, memory_order_relaxed);
    atomic_store_explicit(&hs->pub_seq, lock + 1, memory_order_release);
    atomic_thread_fence(memory_order_release);
    hs->pub_generation = atomic_load_explicit(&hs->generation, memory_order_relaxed);
    hs->pub_phase = phase;
    hs->pub_cycle = cycle;
    hs->pub_group = group;
    hs->pub_open_stamp_ns = now;
    hs->pub_logical_seq += 1;
    atomic_thread_fence(memory_order_release);
    atomic_store_explicit(&hs->pub_seq, lock + 2, memory_order_release);
    atomic_fetch_add_explicit(&hs->publish_word, 1, memory_order_release);
    futex_wake(&hs->publish_word, INT_MAX);
}

static int find_offender(const struct hs_hs *hs)
{
    int i;
    int best = -1;
    uint64_t best_start = UINT64_MAX;
    const struct hs_hs_slot *c;

    /* Prefer a started-but-unfinished item; else the first due unfinished. */
    for (i = 0; i < hs->n_comps; i++) {
        c = &hs->comps[i];
        if (!c->desc.tick_critical) {
            continue;
        }
        if (atomic_load_explicit(&c->finished, memory_order_acquire)) {
            continue;
        }
        if (!atomic_load_explicit(&c->started, memory_order_acquire)) {
            if (best < 0) {
                best = i;
            }
            continue;
        }
        if (c->start_stamp_ns < best_start) {
            best_start = c->start_stamp_ns;
            best = i;
        }
    }
    return best;
}

static void trip_freeze(struct hs_hs *hs, uint64_t now)
{
    int id = find_offender(hs);
    const struct hs_hs_slot *c;

    memset(&hs->last_freeze, 0, sizeof(hs->last_freeze));
    hs->last_freeze.open_stamp_ns = hs->pub_open_stamp_ns;
    hs->last_freeze.trip_stamp_ns = now;
    hs->last_freeze.elapsed_ns = now - hs->pub_open_stamp_ns;
    hs->last_freeze.ceiling_ns = hs->ceiling_ns;
    hs->last_freeze.phase = hs->pub_phase;
    hs->last_freeze.cycle = hs->pub_cycle;
    if (id >= 0) {
        c = &hs->comps[id];
        hs->last_freeze.component_id = c->id;
        strncpy(hs->last_freeze.component_name, c->name, HS_HS_NAME_MAX - 1);
    } else {
        strncpy(hs->last_freeze.component_name, "unknown", HS_HS_NAME_MAX - 1);
    }
    atomic_store_explicit(&hs->frozen, 1, memory_order_release);
    hs->freeze_count++;
}

static bool wait_barrier(struct hs_hs *hs, uint64_t open_ns)
{
    uint32_t exp, got, word;
    uint64_t now, remain, wait0;

    set_beat(hs, HS_BEAT_BARRIER);
    wait0 = hs_now_ns();
    for (;;) {
        if (atomic_load_explicit(&hs->frozen, memory_order_acquire)) {
            return false;
        }
        exp = atomic_load_explicit(&hs->expected, memory_order_acquire);
        got = atomic_load_explicit(&hs->arrivals, memory_order_acquire);
        if (got >= exp) {
            hs->barrier_wait_ns_total += hs_now_ns() - wait0;
            return true;
        }
        now = hs_now_ns();
        if (now - open_ns >= hs->ceiling_ns) {
            trip_freeze(hs, now);
            /* Wake workers so they can observe the freeze and drop out. */
            atomic_fetch_add_explicit(&hs->publish_word, 1, memory_order_release);
            futex_wake(&hs->publish_word, INT_MAX);
            return false;
        }
        remain = hs->ceiling_ns - (now - open_ns);
        if (remain > 2000000ull) {
            remain = 2000000ull; /* 2 ms slices so we re-check the watchdog */
        }
        word = atomic_load_explicit(&hs->barrier_word, memory_order_acquire);
        if (atomic_load_explicit(&hs->arrivals, memory_order_acquire) >= exp) {
            continue;
        }
        futex_wait_ns(&hs->barrier_word, word, remain);
    }
}

static void fold_stats(struct hs_hs *hs, uint64_t wall_ns)
{
    int i;
    struct hs_hs_slot *c;
    uint64_t work, cycle_budget;
    uint32_t ppc = (uint32_t)hs->cfg.phases_per_cycle;

    for (i = 0; i < hs->n_comps; i++) {
        c = &hs->comps[i];
        if (!atomic_load_explicit(&c->finished, memory_order_acquire)) {
            continue;
        }
        work = atomic_load_explicit(&c->duration_ns, memory_order_acquire);
        c->fires++;
        if (work > c->max_work_ns) {
            c->max_work_ns = work;
        }
        c->cycle_work_ns += work;
        if (hs->cfg.phase_budget_ns > 0 && ppc > 0
            && (hs->pub_phase + 1) % ppc == 0) {
            cycle_budget = hs->cfg.phase_budget_ns * (uint64_t)ppc;
            if (c->cycle_work_ns > cycle_budget) {
                c->overruns++;
                if (c->cycle_work_ns - cycle_budget > c->max_overrun_ns) {
                    c->max_overrun_ns = c->cycle_work_ns - cycle_budget;
                }
                if (c->overruns >= (uint64_t)hs->cfg.offender_threshold) {
                    c->repeat_offender = true;
                }
            }
            c->cycle_work_ns = 0;
        }
    }
    if (hs->cfg.phase_budget_ns > 0 && wall_ns > hs->cfg.phase_budget_ns) {
        hs->overrun_phases++;
        hs->overrun_ns_total += wall_ns - hs->cfg.phase_budget_ns;
    }
    if (hs->sample_count < HS_HS_SAMPLES_MAX) {
        hs->samples[hs->sample_count++] = (int64_t)wall_ns;
    }
    hs->phases_done++;
}

static void commit_ledger(struct hs_hs *hs, uint64_t phase, uint64_t cycle, uint32_t group)
{
    uint64_t h;
    uint64_t note;
    uint64_t ev;
    int i;

    set_beat(hs, HS_BEAT_COMMIT);
    ev = hs_hs_apply_matured(hs, cycle);

    h = atomic_load_explicit(&hs->ledger_hash, memory_order_relaxed);
    if (!hs->cfg.ledger) {
        atomic_fetch_add_explicit(&hs->commit_epoch, 1, memory_order_release);
        set_beat(hs, HS_BEAT_IDLE);
        return;
    }
    h = hs_fnv1a64(h, phase);
    h = hs_fnv1a64(h, cycle);
    h = hs_fnv1a64(h, group);
    for (i = 0; i < hs->n_comps; i++) {
        if (atomic_load_explicit(&hs->comps[i].finished, memory_order_acquire)) {
            note = atomic_load_explicit(&hs->comps[i].note, memory_order_acquire);
            h = hs_fnv1a64(h, (uint64_t)hs->comps[i].id);
            h = hs_fnv1a64(h, note);
        }
    }
    h = hs_fnv1a64(h, ev);
    atomic_store_explicit(&hs->ledger_hash, h, memory_order_release);
    atomic_fetch_add_explicit(&hs->commit_epoch, 1, memory_order_release);
    set_beat(hs, HS_BEAT_IDLE);
}

static void clear_phase_scratch(struct hs_hs *hs)
{
    int i;

    for (i = 0; i < hs->n_comps; i++) {
        atomic_store_explicit(&hs->comps[i].start_stamp_ns, 0, memory_order_relaxed);
        atomic_store_explicit(&hs->comps[i].duration_ns, 0, memory_order_relaxed);
        atomic_store_explicit(&hs->comps[i].note, 0, memory_order_relaxed);
        atomic_store_explicit(&hs->comps[i].started, 0, memory_order_relaxed);
        atomic_store_explicit(&hs->comps[i].finished, 0, memory_order_relaxed);
    }
}

bool hs_hs_run_phase(struct hs_hs *hs, uint64_t phase, uint64_t cycle, uint32_t group)
{
    uint64_t now;
    uint64_t wall;
    int i;

    if (hs == NULL || !hs->running) {
        return false;
    }
    if (atomic_load_explicit(&hs->frozen, memory_order_acquire)) {
        return false;
    }

    clear_phase_scratch(hs);
    atomic_store_explicit(&hs->arrivals, 0, memory_order_relaxed);
    atomic_store_explicit(&hs->expected, (uint32_t)hs->worker_count, memory_order_release);
    atomic_store_explicit(&hs->barrier_word, 0, memory_order_relaxed);

    now = hs_now_ns();
    publish(hs, phase, cycle, group, now);

    /* Affine (clock-thread) due items run in the published window. */
    for (i = 0; i < hs->n_comps; i++) {
        if (!hs->comps[i].desc.affine) {
            continue;
        }
        if (!due_for(hs, &hs->comps[i], phase, cycle, group)) {
            continue;
        }
        (void)run_item(hs, &hs->comps[i], phase, cycle,
                       atomic_load_explicit(&hs->generation, memory_order_relaxed));
    }

    set_beat(hs, HS_BEAT_PULL);
    if (!wait_barrier(hs, now)) {
        return false;
    }

    wall = hs_now_ns() - now;
    /* WAIT: we got here because every shard arrived. Nothing was skipped. */
    fold_stats(hs, wall);
    commit_ledger(hs, phase, cycle, group);
    return true;
}

bool hs_hs_frozen(const struct hs_hs *hs)
{
    if (hs == NULL) {
        return false;
    }
    return atomic_load_explicit(&hs->frozen, memory_order_acquire) != 0;
}

void hs_hs_last_freeze(const struct hs_hs *hs, struct hs_freeze *out)
{
    if (out == NULL) {
        return;
    }
    if (hs == NULL) {
        memset(out, 0, sizeof(*out));
        return;
    }
    *out = hs->last_freeze;
}

void hs_hs_reset_after_freeze(struct hs_hs *hs)
{
    if (hs == NULL) {
        return;
    }
    /* Generation bump first: in-flight workers revalidate and drop. */
    atomic_fetch_add_explicit(&hs->generation, 1, memory_order_acq_rel);
    atomic_store_explicit(&hs->arrivals, 0, memory_order_relaxed);
    atomic_store_explicit(&hs->expected, 0, memory_order_relaxed);
    atomic_store_explicit(&hs->frozen, 0, memory_order_release);
    atomic_fetch_add_explicit(&hs->publish_word, 1, memory_order_release);
    futex_wake(&hs->publish_word, INT_MAX);
    atomic_fetch_add_explicit(&hs->barrier_word, 1, memory_order_release);
    futex_wake(&hs->barrier_word, INT_MAX);
}

uint64_t hs_hs_commit_epoch(const struct hs_hs *hs)
{
    return hs ? atomic_load_explicit(&hs->commit_epoch, memory_order_acquire) : 0;
}

uint64_t hs_hs_ledger_hash(const struct hs_hs *hs)
{
    return hs ? atomic_load_explicit(&hs->ledger_hash, memory_order_acquire) : 0;
}

uint64_t hs_hs_monatomic(const struct hs_hs *hs)
{
    return hs ? atomic_load_explicit(&hs->monatomic, memory_order_acquire) : 0;
}

hs_beat_t hs_hs_beat(const struct hs_hs *hs)
{
    if (hs == NULL) {
        return HS_BEAT_IDLE;
    }
    return (hs_beat_t)atomic_load_explicit(&hs->beat, memory_order_acquire);
}

int hs_hs_worker_count(const struct hs_hs *hs)
{
    return hs ? hs->worker_count : 0;
}

int hs_hs_post_event(struct hs_hs *hs, uint64_t tick, uint32_t code, uint32_t value)
{
    struct hs_ctrl_event *e;

    if (hs == NULL || hs->n_events >= HS_HS_MAX_EVENTS) {
        return -EINVAL;
    }
    e = &hs->events[hs->n_events];
    e->seq = (uint64_t)hs->n_events + 1ull;
    e->tick = tick;
    e->code = code;
    e->value = value;
    hs->n_events++;
    return 0;
}

uint64_t hs_hs_apply_matured(struct hs_hs *hs, uint64_t cycle)
{
    uint32_t i;
    const struct hs_ctrl_event *e;
    uint64_t fold = hs->event_fold;

    /* Events were posted in seq order and are already (tick, seq) ordered
       if the test posts in tick order; we still select by tick then seq. */
    for (i = hs->events_applied; i < hs->n_events; i++) {
        e = &hs->events[i];
        if (e->tick > cycle) {
            /* Posted in nondecreasing tick order by the scoreboard. */
            break;
        }
        fold = hs_fnv1a64(fold == 0 ? 0xcbf29ce484222325ull : fold, e->seq);
        fold = hs_fnv1a64(fold, e->tick);
        fold = hs_fnv1a64(fold, e->code);
        fold = hs_fnv1a64(fold, e->value);
        hs->events_applied = i + 1;
    }
    hs->event_fold = fold;
    return fold;
}

static int cmp_i64(const void *a, const void *b)
{
    int64_t da = *(const int64_t *)a;
    int64_t db = *(const int64_t *)b;

    if (da < db) {
        return -1;
    }
    if (da > db) {
        return 1;
    }
    return 0;
}

static int64_t percentile(const int64_t *sorted, uint64_t n, unsigned p)
{
    uint64_t idx;

    if (n == 0) {
        return 0;
    }
    idx = ((uint64_t)p * n + 99ull) / 100ull;
    if (idx == 0) {
        idx = 1;
    }
    idx -= 1;
    if (idx >= n) {
        idx = n - 1;
    }
    return sorted[idx];
}

static unsigned hist_bin(int64_t abs_ns)
{
    unsigned i;

    for (i = 0; i < HS_HS_HIST_BINS; i++) {
        if (abs_ns <= k_hist_bound[i]) {
            return i;
        }
    }
    return HS_HS_HIST_BINS - 1;
}

void hs_hs_jitter(const struct hs_hs *hs, struct hs_jitter_stats *out)
{
    uint64_t n, i;
    int64_t *sorted;
    int64_t sum, v, av;

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (hs == NULL) {
        return;
    }
    n = hs->sample_count;
    if (n > HS_HS_SAMPLES_MAX) {
        n = HS_HS_SAMPLES_MAX;
    }
    out->count = n;
    out->period_ns = hs->cfg.phase_budget_ns;
    out->ticks_done = hs->phases_done;
    out->overruns = hs->overrun_phases;
    if (n == 0) {
        return;
    }
    sorted = (int64_t *)malloc(n * sizeof(int64_t));
    if (sorted == NULL) {
        return;
    }
    memcpy(sorted, hs->samples, n * sizeof(int64_t));
    qsort(sorted, (size_t)n, sizeof(int64_t), cmp_i64);
    out->min_ns = sorted[0];
    out->max_ns = sorted[n - 1];
    out->p50_ns = percentile(sorted, n, 50);
    out->p95_ns = percentile(sorted, n, 95);
    out->p99_ns = percentile(sorted, n, 99);
    sum = 0;
    for (i = 0; i < n; i++) {
        v = hs->samples[i];
        sum += v;
        av = v < 0 ? -v : v;
        out->hist[hist_bin(av)]++;
    }
    out->mean_ns = sum / (int64_t)n;
    free(sorted);
}

void hs_hs_stats(const struct hs_hs *hs, struct hs_hs_stats *out)
{
    int i;

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (hs == NULL) {
        return;
    }
    out->phases = hs->phases_done;
    out->overrun_phases = hs->overrun_phases;
    out->overrun_ns_total = hs->overrun_ns_total;
    out->barrier_wait_ns_total = hs->barrier_wait_ns_total;
    out->freezes = hs->freeze_count;
    out->n_comps = (uint32_t)hs->n_comps;
    for (i = 0; i < hs->n_comps; i++) {
        strncpy(out->comps[i].name, hs->comps[i].name, HS_HS_NAME_MAX - 1);
        out->comps[i].fires = hs->comps[i].fires;
        out->comps[i].overruns = hs->comps[i].overruns;
        out->comps[i].max_work_ns = hs->comps[i].max_work_ns;
        out->comps[i].max_overrun_ns = hs->comps[i].max_overrun_ns;
        out->comps[i].repeat_offender = hs->comps[i].repeat_offender;
    }
    hs_hs_jitter(hs, &out->jitter);
}
