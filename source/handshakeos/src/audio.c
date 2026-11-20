/* SPDX-License-Identifier: MIT */
/* HandshakeOS v3-audio: raw ALSA PCM + mixer through <sound/asound.h>
   ioctls. No libasound, no threads, no locks. Shell thread only. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hs_audio.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sound/asound.h>


/* ---------- hw_params helpers ---------- */

static struct snd_mask *hp_mask(struct snd_pcm_hw_params *p, int param)
{
    return &p->masks[param - SNDRV_PCM_HW_PARAM_FIRST_MASK];
}

static struct snd_interval *hp_ival(struct snd_pcm_hw_params *p, int param)
{
    return &p->intervals[param - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL];
}

static void hp_any(struct snd_pcm_hw_params *p)
{
    int i;

    memset(p, 0, sizeof(*p));
    for (i = SNDRV_PCM_HW_PARAM_FIRST_MASK; i <= SNDRV_PCM_HW_PARAM_LAST_MASK; i++) {
        struct snd_mask *m = hp_mask(p, i);
        memset(m->bits, 0xff, sizeof(m->bits));
    }
    for (i = SNDRV_PCM_HW_PARAM_FIRST_INTERVAL; i <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL; i++) {
        struct snd_interval *v = hp_ival(p, i);
        v->min = 0;
        v->max = UINT_MAX;
    }
    p->rmask = ~0u;
    p->cmask = 0;
    p->info = ~0u;
}

static void hp_set_mask(struct snd_pcm_hw_params *p, int param, unsigned val)
{
    struct snd_mask *m = hp_mask(p, param);

    memset(m->bits, 0, sizeof(m->bits));
    m->bits[val >> 5] |= 1u << (val & 31u);
}

static void hp_set_range(struct snd_pcm_hw_params *p, int param, unsigned lo, unsigned hi,
                         int integer)
{
    struct snd_interval *v = hp_ival(p, param);

    v->min = lo;
    v->max = hi;
    v->openmin = 0;
    v->openmax = 0;
    v->integer = integer ? 1u : 0u;
}

static unsigned hp_get(struct snd_pcm_hw_params *p, int param)
{
    return hp_ival(p, param)->max;
}

static int try_hw(int fd, unsigned rate, unsigned plo, unsigned phi, unsigned nlo,
                  unsigned nhi, struct snd_pcm_hw_params *out)
{
    struct snd_pcm_hw_params p;

    hp_any(&p);
    hp_set_mask(&p, SNDRV_PCM_HW_PARAM_ACCESS, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
    hp_set_mask(&p, SNDRV_PCM_HW_PARAM_FORMAT, SNDRV_PCM_FORMAT_S16_LE);
    hp_set_mask(&p, SNDRV_PCM_HW_PARAM_SUBFORMAT, SNDRV_PCM_SUBFORMAT_STD);
    hp_set_range(&p, SNDRV_PCM_HW_PARAM_CHANNELS, 2, 2, 1);
    hp_set_range(&p, SNDRV_PCM_HW_PARAM_SAMPLE_BITS, 16, 16, 1);
    hp_set_range(&p, SNDRV_PCM_HW_PARAM_FRAME_BITS, 32, 32, 1);
    hp_set_range(&p, SNDRV_PCM_HW_PARAM_RATE, rate, rate, 1);
    hp_set_range(&p, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, plo, phi, 1);
    hp_set_range(&p, SNDRV_PCM_HW_PARAM_PERIODS, nlo, nhi, 1);
    if (ioctl(fd, SNDRV_PCM_IOCTL_HW_PARAMS, &p) != 0) {
        return -errno;
    }
    *out = p;
    return 0;
}

/* ---------- mixer ---------- */

static int ctl_write_ints(int fd, const struct snd_ctl_elem_info *info, long v)
{
    struct snd_ctl_elem_value val;
    unsigned i;

    memset(&val, 0, sizeof(val));
    val.id = info->id;
    for (i = 0; i < info->count && i < 128u; i++) {
        val.value.integer.value[i] = v;
    }
    return ioctl(fd, SNDRV_CTL_IOCTL_ELEM_WRITE, &val) == 0 ? 0 : -errno;
}

static int ctl_set_enum_by_name(int fd, struct snd_ctl_elem_info *info, const char *want)
{
    unsigned items = info->value.enumerated.items, k;

    for (k = 0; k < items && k < 64u; k++) {
        struct snd_ctl_elem_info ei;
        memset(&ei, 0, sizeof(ei));
        ei.id = info->id;
        ei.value.enumerated.item = k;
        if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &ei) != 0) {
            return -errno;
        }
        if (strcmp(ei.value.enumerated.name, want) == 0) {
            struct snd_ctl_elem_value val;
            unsigned i;
            memset(&val, 0, sizeof(val));
            val.id = info->id;
            for (i = 0; i < info->count && i < 128u; i++) {
                val.value.enumerated.item[i] = k;
            }
            return ioctl(fd, SNDRV_CTL_IOCTL_ELEM_WRITE, &val) == 0 ? 0 : -errno;
        }
    }
    return -ENOENT;
}

static long pct_to_raw(const struct hs_audio_volctl *c, int pct)
{
    return c->min + ((c->max - c->min) * (long)pct + 50) / 100;
}

static void mixer_setup(struct hs_audio *a)
{
    struct snd_ctl_elem_list list;
    struct snd_ctl_elem_id *ids;
    struct snd_ctl_card_info ci;
    unsigned i, count;

    memset(&ci, 0, sizeof(ci));
    if (ioctl(a->ctl_fd, SNDRV_CTL_IOCTL_CARD_INFO, &ci) == 0) {
        snprintf(a->card, sizeof(a->card), "%s", (const char *)ci.name);
    }
    memset(&list, 0, sizeof(list));
    if (ioctl(a->ctl_fd, SNDRV_CTL_IOCTL_ELEM_LIST, &list) != 0 || list.count == 0) {
        return;
    }
    count = list.count > 512u ? 512u : list.count;
    ids = (struct snd_ctl_elem_id *)calloc(count, sizeof(*ids));
    if (ids == NULL) {
        return;
    }
    list.offset = 0;
    list.space = count;
    list.pids = ids;
    if (ioctl(a->ctl_fd, SNDRV_CTL_IOCTL_ELEM_LIST, &list) != 0) {
        free(ids);
        return;
    }
    for (i = 0; i < list.used; i++) {
        struct snd_ctl_elem_info info;
        const char *nm = (const char *)ids[i].name;

        if (ids[i].iface != SNDRV_CTL_ELEM_IFACE_MIXER) {
            continue;
        }
        memset(&info, 0, sizeof(info));
        info.id = ids[i];
        if (ioctl(a->ctl_fd, SNDRV_CTL_IOCTL_ELEM_INFO, &info) != 0) {
            continue;
        }
        if (!(info.access & SNDRV_CTL_ELEM_ACCESS_WRITE)) {
            continue;
        }
        switch (info.type) {
        case SNDRV_CTL_ELEM_TYPE_BOOLEAN:
            /* Unmute every playback path, but never the L<->R swap. */
            if (strstr(nm, "Reversed") != NULL) {
                if (ctl_write_ints(a->ctl_fd, &info, 0) == 0) {
                    a->ctl_set++;
                }
            } else if (strstr(nm, "Playback Switch") != NULL ||
                       strcmp(nm, "Speaker Switch") == 0 ||
                       strcmp(nm, "Headphone Switch") == 0) {
                if (ctl_write_ints(a->ctl_fd, &info, 1) == 0) {
                    a->ctl_set++;
                }
            }
            break;
        case SNDRV_CTL_ELEM_TYPE_INTEGER:
            if (strstr(nm, "Playback Volume") == NULL) {
                break;
            }
            if (strcmp(nm, "DAC Playback Volume") == 0) {
                /* H616 digital volume: max is 0 dB (inverted register). */
                if (ctl_write_ints(a->ctl_fd, &info, info.value.integer.max) == 0) {
                    a->ctl_set++;
                }
            } else if (a->n_vol < HS_AUDIO_MAX_VOL_CTLS) {
                struct hs_audio_volctl *c = &a->vol[a->n_vol++];
                c->numid = info.id.numid;
                c->count = info.count;
                c->min = info.value.integer.min;
                c->max = info.value.integer.max;
                if (ctl_write_ints(a->ctl_fd, &info, pct_to_raw(c, a->vol_pct)) == 0) {
                    a->ctl_set++;
                }
            }
            break;
        case SNDRV_CTL_ELEM_TYPE_ENUMERATED:
            if (strcmp(nm, "Line Out Source Playback Route") == 0) {
                if (ctl_set_enum_by_name(a->ctl_fd, &info, "Stereo") == 0) {
                    a->ctl_set++;
                }
            }
            break;
        default:
            break;
        }
    }
    free(ids);
}

int hs_audio_set_volume(struct hs_audio *a, int pct)
{
    int i, rc = 0;

    if (a == NULL) {
        return -EINVAL;
    }
    if (pct < 0) {
        pct = 0;
    }
    if (pct > 100) {
        pct = 100;
    }
    a->vol_pct = pct;
    if (a->ctl_fd < 0) {
        return 0;
    }
    for (i = 0; i < a->n_vol; i++) {
        struct snd_ctl_elem_value val;
        long raw = pct_to_raw(&a->vol[i], pct);
        unsigned k;
        memset(&val, 0, sizeof(val));
        val.id.numid = a->vol[i].numid;
        for (k = 0; k < a->vol[i].count && k < 128u; k++) {
            val.value.integer.value[k] = raw;
        }
        if (ioctl(a->ctl_fd, SNDRV_CTL_IOCTL_ELEM_WRITE, &val) != 0) {
            rc = -errno;
        }
    }
    return rc;
}

int hs_audio_volume(const struct hs_audio *a)
{
    return a ? a->vol_pct : 0;
}

/* ---------- open / close ---------- */

static void set_status(struct hs_audio *a)
{
    if (a->live) {
        snprintf(a->status, sizeof(a->status), "LIVE %s %uHz %ux%u",
                 a->card[0] ? a->card : "card0", a->rate, a->period,
                 a->period ? a->buffer / a->period : 0u);
    } else {
        snprintf(a->status, sizeof(a->status), "SYNTH ONLY");
    }
}

int hs_audio_open_dev(struct hs_audio *a, const char *ctl_path, const char *pcm_path,
                      unsigned src_rate)
{
    struct snd_pcm_hw_params hw;
    struct snd_pcm_sw_params sw;
    static const unsigned rates[] = { 48000u, 44100u };
    size_t r;
    int rc = -ENODEV;

    if (a == NULL) {
        return -EINVAL;
    }
    memset(a, 0, sizeof(*a));
    a->pcm_fd = -1;
    a->ctl_fd = -1;
    a->vol_pct = 80;
    a->src_rate = src_rate ? src_rate : 32768u;
    set_status(a);

    if (ctl_path != NULL) {
        a->ctl_fd = open(ctl_path, O_RDWR | O_CLOEXEC | O_NONBLOCK);
        if (a->ctl_fd >= 0) {
            mixer_setup(a);
        }
    }
    if (pcm_path == NULL) {
        return -ENODEV;
    }
    a->pcm_fd = open(pcm_path, O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (a->pcm_fd < 0) {
        rc = -errno;
        set_status(a);
        return rc;
    }
    for (r = 0; r < sizeof(rates) / sizeof(rates[0]); r++) {
        /* 4 x 512 first (~43 ms at 48 kHz), then let the driver pick. */
        if ((rc = try_hw(a->pcm_fd, rates[r], 512, 512, 4, 4, &hw)) == 0 ||
            (rc = try_hw(a->pcm_fd, rates[r], 256, 1024, 3, 8, &hw)) == 0 ||
            (rc = try_hw(a->pcm_fd, rates[r], 64, 8192, 2, 64, &hw)) == 0) {
            a->rate = rates[r];
            break;
        }
    }
    if (rc != 0) {
        close(a->pcm_fd);
        a->pcm_fd = -1;
        set_status(a);
        return rc;
    }
    a->channels = 2;
    a->period = hp_get(&hw, SNDRV_PCM_HW_PARAM_PERIOD_SIZE);
    a->buffer = hp_get(&hw, SNDRV_PCM_HW_PARAM_BUFFER_SIZE);
    if (a->buffer == 0 || a->buffer > HS_AUDIO_OUT_MAX) {
        a->buffer = a->period * hp_get(&hw, SNDRV_PCM_HW_PARAM_PERIODS);
    }

    memset(&sw, 0, sizeof(sw));
    sw.tstamp_mode = SNDRV_PCM_TSTAMP_NONE;
    sw.period_step = 1;
    sw.avail_min = a->period;
    sw.start_threshold = a->period * 2u <= a->buffer ? a->period * 2u : a->buffer;
    sw.stop_threshold = a->buffer;
    sw.silence_threshold = 0;
    sw.silence_size = 0;
    sw.boundary = a->buffer;
    while (sw.boundary * 2u <= (snd_pcm_uframes_t)(LONG_MAX - a->buffer)) {
        sw.boundary *= 2u;
    }
    sw.proto = SNDRV_PCM_VERSION;
    if (ioctl(a->pcm_fd, SNDRV_PCM_IOCTL_SW_PARAMS, &sw) != 0 ||
        ioctl(a->pcm_fd, SNDRV_PCM_IOCTL_PREPARE) != 0) {
        rc = -errno;
        close(a->pcm_fd);
        a->pcm_fd = -1;
        set_status(a);
        return rc;
    }
    /* 16.16 step: one division, here, never per sample. */
    a->step = (uint32_t)(((uint64_t)a->src_rate << 16) / a->rate);
    a->live = 1;
    set_status(a);
    return 0;
}

/* Card score: the H616 analog codec (speaker + headphone) wins over HDMI. */
static int card_score(int card)
{
    char path[64];
    struct snd_ctl_card_info ci;
    int fd, score;

    snprintf(path, sizeof(path), "/dev/snd/pcmC%dD0p", card);
    if (access(path, F_OK) != 0) {
        return -1;
    }
    score = 1;
    snprintf(path, sizeof(path), "/dev/snd/controlC%d", card);
    fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        return score;
    }
    memset(&ci, 0, sizeof(ci));
    if (ioctl(fd, SNDRV_CTL_IOCTL_CARD_INFO, &ci) == 0) {
        const char *nm = (const char *)ci.name;
        if (strstr(nm, "HDMI") != NULL || strstr(nm, "hdmi") != NULL) {
            score = 0;
        } else if (strstr(nm, "Codec") != NULL || strstr(nm, "codec") != NULL) {
            score = 3;
        } else {
            score = 2;
        }
    }
    close(fd);
    return score;
}

int hs_audio_open(struct hs_audio *a, unsigned src_rate)
{
    char ctl[64], pcm[64];
    int card, best = -1, best_score = -1;

    for (card = 0; card < 8; card++) {
        int sc = card_score(card);
        if (sc > best_score) {
            best_score = sc;
            best = card;
        }
    }
    if (best < 0) {
        /* No card: synth-only, but still remember the volume. */
        return hs_audio_open_dev(a, NULL, NULL, src_rate);
    }
    snprintf(ctl, sizeof(ctl), "/dev/snd/controlC%d", best);
    snprintf(pcm, sizeof(pcm), "/dev/snd/pcmC%dD0p", best);
    return hs_audio_open_dev(a, access(ctl, F_OK) == 0 ? ctl : NULL, pcm, src_rate);
}

void hs_audio_close(struct hs_audio *a)
{
    if (a == NULL) {
        return;
    }
    if (a->pcm_fd >= 0) {
        (void)ioctl(a->pcm_fd, SNDRV_PCM_IOCTL_DROP);
        close(a->pcm_fd);
    }
    if (a->ctl_fd >= 0) {
        close(a->ctl_fd);
    }
    a->pcm_fd = -1;
    a->ctl_fd = -1;
    a->live = 0;
    set_status(a);
}

const char *hs_audio_status(const struct hs_audio *a)
{
    return a ? a->status : "SYNTH ONLY";
}

/* ---------- resample + write ---------- */

size_t hs_audio_resample(struct hs_audio *a, const int16_t *in, size_t n,
                         int16_t *out, size_t out_cap)
{
    size_t produced = 0;
    uint32_t pos;

    if (a == NULL || in == NULL || n == 0 || out == NULL || a->step == 0) {
        return 0;
    }
    pos = a->pos;
    /* Virtual input: v[0] = previous call's last frame, v[k] = in[k-1]. */
    for (;;) {
        size_t idx = pos >> 16;
        int32_t frac = (int32_t)(pos & 0xFFFFu);
        int c;

        if (idx + 1u > n || produced >= out_cap) {
            break;
        }
        for (c = 0; c < 2; c++) {
            int32_t s0 = idx == 0 ? a->prev[c] : in[(idx - 1u) * 2u + (size_t)c];
            int32_t s1 = in[idx * 2u + (size_t)c];
            out[produced * 2u + (size_t)c] = (int16_t)(s0 + (((s1 - s0) * frac) >> 16));
        }
        produced++;
        pos += a->step;
    }
    if ((size_t)(pos >> 16) >= n) {
        pos -= (uint32_t)(n << 16);
    } else {
        pos &= 0xFFFFu; /* out_cap hit: drop the rest of this block */
    }
    a->pos = pos;
    a->prev[0] = in[(n - 1u) * 2u];
    a->prev[1] = in[(n - 1u) * 2u + 1u];
    return produced;
}

static int pcm_push(struct hs_audio *a, int16_t *buf, size_t frames)
{
    size_t done = 0;
    int tries = 0;

    while (done < frames && tries < 3) {
        struct snd_xferi x;
        memset(&x, 0, sizeof(x));
        x.buf = buf + done * 2u;
        x.frames = (snd_pcm_uframes_t)(frames - done);
        if (ioctl(a->pcm_fd, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &x) == 0) {
            if (x.result <= 0) {
                break;
            }
            done += (size_t)x.result;
            continue;
        }
        tries++;
        if (errno == EPIPE) {
            /* Underrun: re-prepare and refill; start_threshold restarts it. */
            a->xruns++;
            if (ioctl(a->pcm_fd, SNDRV_PCM_IOCTL_PREPARE) != 0) {
                return -errno;
            }
            continue;
        }
        if (errno == ESTRPIPE) {
            if (ioctl(a->pcm_fd, SNDRV_PCM_IOCTL_RESUME) != 0) {
                (void)ioctl(a->pcm_fd, SNDRV_PCM_IOCTL_PREPARE);
            }
            continue;
        }
        if (errno == EAGAIN || errno == EINTR) {
            break; /* ring full: never block the frame */
        }
        return -errno;
    }
    a->frames_out += done;
    a->dropped += frames - done;
    return (int)done;
}

int hs_audio_write(struct hs_audio *a, const int16_t *stereo, size_t frames)
{
    size_t n;

    if (a == NULL || !a->live || stereo == NULL || frames == 0) {
        return 0;
    }
    n = hs_audio_resample(a, stereo, frames, a->out, HS_AUDIO_OUT_MAX / 2u);
    return n > 0 ? pcm_push(a, a->out, n) : 0;
}

int hs_audio_silence(struct hs_audio *a, size_t frames)
{
    if (a == NULL || !a->live) {
        return 0;
    }
    if (frames > HS_AUDIO_OUT_MAX / 2u) {
        frames = HS_AUDIO_OUT_MAX / 2u;
    }
    memset(a->out, 0, frames * 4u);
    return pcm_push(a, a->out, frames);
}

/* v4-multi: switch the resampler's source rate (Game Boy APU ~32731 Hz
   vs the 48 kHz registry cores). Shell thread only, between writes. */
void hs_audio_set_src_rate(struct hs_audio *a, unsigned src_rate)
{
    if (a == NULL || src_rate == 0 || src_rate == a->src_rate) {
        return;
    }
    a->src_rate = src_rate;
    a->pos = 0;
    a->prev[0] = a->prev[1] = 0;
    if (a->rate != 0) {
        a->step = (uint32_t)(((uint64_t)a->src_rate << 16) / a->rate);
    }
}
