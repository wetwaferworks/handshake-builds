/* SPDX-License-Identifier: MIT */
#ifndef HS_HANDSHAKE_H
#define HS_HANDSHAKE_H

/*
 * HandshakeOS Timing Handshake (pure C11)
 *
 * Three pillars only: Time Serialization, control coupling, event coupling.
 * A fourth has not been received. This is not the Thread Handshake.
 *
 * Shape: one clock thread publishes phase windows; N worker threads pull.
 * The default seating is 3+1 (three workers and the clock).
 *
 * Four Gemstone beats in every phase: publish, pull, barrier, commit.
 *
 * Missed-window policy: WAIT (the phase extends; nothing is skipped).
 * Watchdog policy: FREEZE (default 250 ms). last_freeze names the offender;
 * reset_after_freeze bumps the generation so stale in-flight work dies.
 *
 * Workers do not decide when to run. They pull a consistent snapshot.
 * Commit folds a hash-chained ledger in component-id order, so the hash
 * is about the work and its order, never about which thread ran it.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#define HS_HS_MAX_WORKERS     8
#define HS_HS_MAX_COMPS       32
#define HS_HS_MAX_EVENTS      256
#define HS_HS_NAME_MAX        64
#define HS_HS_SAMPLES_MAX     8192
#define HS_HS_HIST_BINS       12
#define HS_HS_DEFAULT_CEILING_NS 250000000ull  /* 250 ms */
#define HS_HS_ALL_GROUPS      0xFFFFFFFFu

typedef enum {
    HS_CADENCE_EVERY_PHASE = 0,
    HS_CADENCE_HALF_CYCLE  = 1,
    HS_CADENCE_CYCLE_HEAD  = 2
} hs_cadence_t;

typedef enum {
    HS_BEAT_IDLE    = 0,
    HS_BEAT_PUBLISH = 1,
    HS_BEAT_PULL    = 2,
    HS_BEAT_BARRIER = 3,
    HS_BEAT_COMMIT  = 4
} hs_beat_t;

struct hs_published {
    uint64_t seq;
    uint64_t generation;
    uint64_t phase;
    uint64_t cycle;
    uint32_t group;
    uint64_t open_stamp_ns;
    bool     due_work;
};

struct hs_freeze {
    uint32_t component_id;
    char     component_name[HS_HS_NAME_MAX];
    uint64_t phase;
    uint64_t cycle;
    uint64_t open_stamp_ns;
    uint64_t trip_stamp_ns;
    uint64_t elapsed_ns;
    uint64_t ceiling_ns;
};

struct hs_hs_comp_desc {
    const char    *name;
    hs_cadence_t cadence;
    uint32_t       phase_divider; /* 0 = cadence decides; nonzero wins */
    uint32_t       phase_offset;
    bool           affine;        /* runs on the clock thread */
    bool           tick_critical; /* participates in barrier + watchdog */
    uint32_t       group;         /* 0 = every runPhase group */
};

/* Return value is folded into the ledger at commit, in component-id order. */
typedef uint64_t (*hs_hs_work_fn)(uint64_t phase, uint64_t cycle, void *user);

struct hs_hs_config {
    int      phases_per_cycle;     /* 1..64, default 4 */
    int      worker_threads;       /* 0 = 3 (the 3+1 seating) */
    uint64_t phase_budget_ns;      /* 0 = overrun accounting off */
    uint64_t watchdog_ceiling_ns;  /* 0 = 250 ms */
    uint32_t offender_threshold;   /* default 3 */
    bool     ledger;               /* default true */
};

struct hs_ctrl_event {
    uint64_t seq;
    uint64_t tick;
    uint32_t code;
    uint32_t value;
};

struct hs_comp_stats {
    char     name[HS_HS_NAME_MAX];
    uint64_t fires;
    uint64_t overruns;
    uint64_t max_work_ns;
    uint64_t max_overrun_ns;
    bool     repeat_offender;
};

struct hs_jitter_stats {
    uint64_t count;
    uint64_t period_ns;
    uint64_t ticks_done;     /* committed phases */
    uint64_t denied;
    uint64_t overruns;       /* phases whose wall time exceeded the budget */
    int64_t  p50_ns;
    int64_t  p95_ns;
    int64_t  p99_ns;
    int64_t  max_ns;
    int64_t  min_ns;
    int64_t  mean_ns;
    uint32_t hist[HS_HS_HIST_BINS];
};

struct hs_hs_stats {
    uint64_t phases;
    uint64_t overrun_phases;
    uint64_t overrun_ns_total;
    uint64_t barrier_wait_ns_total;
    uint64_t freezes;
    uint32_t n_comps;
    struct hs_comp_stats comps[HS_HS_MAX_COMPS];
    struct hs_jitter_stats jitter;
};

/* Internal component slot. Callers must not write these fields. */
struct hs_hs_slot {
    struct hs_hs_comp_desc desc;
    hs_hs_work_fn fn;
    void    *user;
    int      shard;
    char     name[HS_HS_NAME_MAX];
    uint32_t id;
    _Atomic uint64_t start_stamp_ns;
    _Atomic uint64_t duration_ns;
    _Atomic uint64_t note;
    _Atomic uint32_t started;
    _Atomic uint32_t finished;
    uint64_t fires;
    uint64_t overruns;
    uint64_t max_work_ns;
    uint64_t max_overrun_ns;
    uint64_t cycle_work_ns;
    bool     repeat_offender;
};

struct hs_hs {
    struct hs_hs_config cfg;
    uint64_t ceiling_ns;
    int      worker_count;
    int      n_comps;
    int      running;
    int      stop;
    struct hs_hs_slot comps[HS_HS_MAX_COMPS];

    /* Publication (clock thread writes, workers seqlock-read). */
    _Atomic uint64_t pub_seq;        /* seqlock: odd = write in progress */
    uint64_t pub_generation;
    uint64_t pub_phase;
    uint64_t pub_cycle;
    uint32_t pub_group;
    uint64_t pub_open_stamp_ns;
    uint64_t pub_logical_seq;        /* published phase sequence (even, >= 1) */

    _Atomic uint64_t generation;
    _Atomic uint32_t beat;
    _Atomic uint32_t frozen;
    _Atomic uint64_t commit_epoch;
    _Atomic uint64_t ledger_hash;
    _Atomic uint64_t monatomic;
    _Atomic uint32_t arrivals;
    _Atomic uint32_t expected;
    _Atomic uint32_t publish_word;   /* futex word for workers */
    _Atomic uint32_t barrier_word;   /* futex word for the clock thread */
    _Atomic uint32_t stop_word;

    struct hs_freeze last_freeze;

    /* Control coupling: stamped (seq, tick) stream, drained in PRE. */
    uint32_t n_events;
    uint32_t events_applied;
    struct hs_ctrl_event events[HS_HS_MAX_EVENTS];
    uint64_t control_fold;
    uint64_t event_fold;

    /* Jitter of phase open-to-commit (WAIT-extended duration). */
    uint32_t sample_count;
    int64_t  samples[HS_HS_SAMPLES_MAX];
    uint64_t overrun_phases;
    uint64_t overrun_ns_total;
    uint64_t barrier_wait_ns_total;
    uint64_t freeze_count;
    uint64_t phases_done;

    /* Worker threads (pthread, but the handshake itself has no mutex). */
    void    *worker_threads[HS_HS_MAX_WORKERS];
    uint64_t shard_seen[HS_HS_MAX_WORKERS];
    /* Boot args for worker threads (pointer stays valid for the thread's life). */
    struct {
        struct hs_hs *hs;
        uint32_t         shard;
    } boot[HS_HS_MAX_WORKERS];
};

void hs_hs_default_config(struct hs_hs_config *cfg);
int  hs_hs_init(struct hs_hs *hs, const struct hs_hs_config *cfg);
void hs_hs_fini(struct hs_hs *hs);

uint32_t hs_hs_register(struct hs_hs *hs,
                           const struct hs_hs_comp_desc *desc,
                           hs_hs_work_fn fn, void *user);

int  hs_hs_start(struct hs_hs *hs);
void hs_hs_stop(struct hs_hs *hs);

/* Clock thread: publish, wait at the barrier (WAIT / FREEZE), commit. */
bool hs_hs_run_phase(struct hs_hs *hs, uint64_t phase, uint64_t cycle, uint32_t group);

bool hs_hs_poll_phase(const struct hs_hs *hs, uint32_t shard,
                         uint64_t seen_seq, struct hs_published *out);
bool hs_hs_can_tick(const struct hs_hs *hs, uint32_t shard, uint64_t *phase_out);

bool hs_hs_frozen(const struct hs_hs *hs);
void hs_hs_last_freeze(const struct hs_hs *hs, struct hs_freeze *out);
void hs_hs_reset_after_freeze(struct hs_hs *hs);

uint64_t    hs_hs_commit_epoch(const struct hs_hs *hs);
uint64_t    hs_hs_ledger_hash(const struct hs_hs *hs);
uint64_t    hs_hs_monatomic(const struct hs_hs *hs);
hs_beat_t hs_hs_beat(const struct hs_hs *hs);
int         hs_hs_worker_count(const struct hs_hs *hs);

void hs_hs_stats(const struct hs_hs *hs, struct hs_hs_stats *out);
void hs_hs_jitter(const struct hs_hs *hs, struct hs_jitter_stats *out);

/* Control coupling: stamp events before start, or from the clock thread. */
int  hs_hs_post_event(struct hs_hs *hs, uint64_t tick, uint32_t code, uint32_t value);
/* Event coupling helper used at commit: apply matured events in (tick, seq) order. */
uint64_t hs_hs_apply_matured(struct hs_hs *hs, uint64_t cycle);

const char *hs_beat_name(hs_beat_t beat);
const char *hs_hist_bin_label(unsigned bin);
uint64_t    hs_now_ns(void);
uint64_t    hs_fnv1a64(uint64_t hash, uint64_t word);

#endif /* HS_HANDSHAKE_H */
