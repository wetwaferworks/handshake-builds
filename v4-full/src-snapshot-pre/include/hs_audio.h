/* SPDX-License-Identifier: MIT */
#ifndef HS_AUDIO_H
#define HS_AUDIO_H

#include <stddef.h>
#include <stdint.h>

/* v3-audio: raw Linux ALSA (no libasound). Shell thread only, no locks,
   no threads. Every call is non-blocking; with no sound card the device
   stays closed and every call is a cheap no-op (synth-only). */

#define HS_AUDIO_MAX_VOL_CTLS 8
#define HS_AUDIO_OUT_MAX      (16384 * 2)

struct hs_audio_volctl {
    unsigned numid;
    unsigned count;
    long     min;
    long     max;
};

struct hs_audio {
    int      pcm_fd;
    int      ctl_fd;
    int      live;            /* 1 = PCM configured and prepared */
    unsigned rate;            /* device rate (48000 / 44100 / ...) */
    unsigned channels;
    unsigned period;          /* frames */
    unsigned buffer;          /* frames */
    unsigned src_rate;        /* APU rate fed to hs_audio_write */
    uint32_t step;            /* 16.16 source frames per output frame */
    uint32_t pos;             /* 16.16 resampler phase */
    int16_t  prev[2];
    int      vol_pct;
    int      n_vol;
    struct hs_audio_volctl vol[HS_AUDIO_MAX_VOL_CTLS];
    int      ctl_set;         /* controls written at open */
    uint64_t frames_out;      /* frames accepted by the device */
    uint64_t xruns;           /* EPIPE recoveries */
    uint64_t dropped;         /* frames dropped (ring full) */
    char     card[48];
    char     status[96];
    int16_t  out[HS_AUDIO_OUT_MAX];
};

/* Open the best card 0..7 (H616 codec over HDMI; controlCn + pcmCnD0p). Returns 0 when
   live, negative errno when synth-only. src_rate = hs_gb_pcm_rate_hz(). */
int  hs_audio_open(struct hs_audio *a, unsigned src_rate);
int  hs_audio_open_dev(struct hs_audio *a, const char *ctl_path, const char *pcm_path,
                       unsigned src_rate);
void hs_audio_close(struct hs_audio *a);

/* Resample interleaved S16 stereo at src_rate and queue it. */
int  hs_audio_write(struct hs_audio *a, const int16_t *stereo, size_t frames);
/* Queue `frames` of silence (e.g. while paused). */
int  hs_audio_silence(struct hs_audio *a, size_t frames);

int  hs_audio_set_volume(struct hs_audio *a, int pct);  /* 0..100 */
/* v4-multi: change the source rate fed to hs_audio_write (e.g. 48000). */
void hs_audio_set_src_rate(struct hs_audio *a, unsigned src_rate);
int  hs_audio_volume(const struct hs_audio *a);
const char *hs_audio_status(const struct hs_audio *a);

/* Pure helper (testable without hardware): resample n stereo frames into
   out (capacity out_cap frames). Returns frames produced. */
size_t hs_audio_resample(struct hs_audio *a, const int16_t *in, size_t n,
                         int16_t *out, size_t out_cap);

#endif /* HS_AUDIO_H */
