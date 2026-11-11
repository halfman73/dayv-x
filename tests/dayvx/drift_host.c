/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 halfman73, DAYV-X */
/* DAYV-X: a host for the DRIFT engine (firmware/src/eng_drift.c) on a computer. Plays each preset through a
 * four-chord phrase on the engine interface of Felucca-Engines (the host of firmware/engines/tests/fe_host.c,
 * with a longer envelope) and writes one 16-bit mono WAV per preset, to listen to the engine before it runs on
 * an FM-1. Prints each render's peak and FNV-1a hash (tests/dayvx/run.sh keeps them in drift_golden.txt).
 *   cc -O2 -std=c99 -I firmware/engines/build -o build/drift_host tests/dayvx/drift_host.c
 *   build/drift_host [OUTDIR]          (no OUTDIR: no WAV files, only the lines) */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NVOICE 8
#define NPART 4
#include "fe_tables.h"
#include "../../firmware/engines/src/felucca_engine.h"

enum { P_ATK, P_REL, P_FTYPE, P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7, P_COUNT };
typedef struct track {
    int16_t p[P_COUNT];
    voice_t v[NVOICE];
} track_t;
static track_t trk[NPART];

#include "../../firmware/engines/src/dsp.c"
#include "../../firmware/src/eng_drift.c"

#define BPS (FS / CTL)                       /* blocks a second */
#define SECS 16
#define NBLK (SECS * BPS)

static uint32_t vage;
static int32_t att_step, rel_shift;

static void voice_on(track_t *t, const engine_t *e, voice_t *v, uint32_t note)
{
    int sounding = v->active && v->stage != 0;
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2], k;
    int32_t s[8];
    memcpy(s, v->s, sizeof s);
    v->note = (uint8_t)note;
    v->vel = 100;
    v->gate = 1;
    v->active = 1;
    v->stage = 1;
    v->age = ++vage;
    v->pitch16 = v->pitch_cur = (int32_t)note * 16;
    v->fine = 0;
    if (!sounding)
        v->env = v->env_out = 0;
    e->note_on(t, v);
    if (sounding) {
        v->ph[0] = ph0;
        v->ph[1] = ph1;
        v->ph[2] = ph2;
        for (k = 0; k < 8u; k++)
            if (k != 2u && k != 3u && (e->keep >> k & 1u))
                v->s[k] = s[k];
    }
}

static int32_t env_tick(voice_t *v)
{
    if (!v->active)
        return 0;
    if (v->stage == 1) {
        v->env += att_step;
        if (v->env >= 1 << 24) {
            v->env = 1 << 24;
            v->stage = 2;
        }
    } else if (v->stage == 2) {
        v->env -= (v->env - (13 << 20)) >> 9;         /* to a sustain of 0.81 */
    } else if (v->stage == 3) {
        v->env -= (v->env >> rel_shift) + 1;
        if (v->env < (1 << 24) / 2000) {
            v->env = 0;
            v->stage = 0;
            v->active = 0;
        }
    }
    return v->env >> 9;
}

static void put32(FILE *f, uint32_t x) { fputc(x & 255, f); fputc(x >> 8 & 255, f); fputc(x >> 16 & 255, f); fputc(x >> 24, f); }
static void put16(FILE *f, uint32_t x) { fputc(x & 255, f); fputc(x >> 8 & 255, f); }

/* four chords, three seconds each, the notes entered a little apart, then the tail */
static const uint8_t CHORDS[4][4] = {{50, 57, 62, 65}, {46, 53, 58, 62}, {48, 55, 60, 64}, {45, 52, 57, 60}};

static void render(const engine_t *e, uint32_t j, const char *dir)
{
    const preset_t *pr = &e->presets[j];
    track_t *t = &trk[j % NPART];
    int32_t out[CTL], last[NVOICE] = {0}, peak = 0;
    uint64_t h = 14695981039346656037ull;
    uint32_t b, i, k;
    FILE *f = 0;
    char path[512];
    memset(t, 0, sizeof *t);
    memset(drift_p, 0, sizeof drift_p);
    vage = 0;
    for (k = 0; k < 8u; k++)
        t->p[P_E0 + k] = pr->e[k];
    att_step = (1 << 24) / (1 + pr->env[0] * pr->env[0] / 12);          /* ATK 0..127 -> 1 block .. ~1 s */
    rel_shift = 5 + pr->env[3] / 24;
    if (dir) {
        snprintf(path, sizeof path, "%s/drift_%u_%s.wav", dir, j, pr->name);
        for (i = 0; path[i]; i++)
            if (path[i] == ' ' || path[i] == '+')
                path[i] = '_';
        f = fopen(path, "wb");
        if (!f) {
            perror(path);
            return;
        }
        fwrite("RIFF", 1, 4, f); put32(f, 36 + NBLK * CTL * 2); fwrite("WAVEfmt ", 1, 8, f);
        put32(f, 16); put16(f, 1); put16(f, 1); put32(f, FS); put32(f, FS * 2); put16(f, 2); put16(f, 16);
        fwrite("data", 1, 4, f); put32(f, NBLK * CTL * 2);
    }
    for (b = 0; b < NBLK; b++) {
        uint32_t bar = b / (3 * BPS), at = b % (3 * BPS);
        if (bar < 4)
            for (k = 0; k < (pr->mono ? 1u : 4u); k++) {
                if (at == k * (BPS / 12))
                    voice_on(t, e, &t->v[k], pr->mono ? CHORDS[bar][3] + 12u : CHORDS[bar][k]);
                if (at == (uint32_t)(3 * BPS - BPS / 3)) {
                    t->v[k].gate = 0;
                    t->v[k].stage = 3;
                }
            }
        e->block(t);
        memset(out, 0, sizeof out);
        for (k = 0; k < NVOICE; k++) {
            voice_t *v = &t->v[k];
            vmod_t m;
            int32_t a;
            if (!v->active)
                continue;
            a = env_tick(v);
            if (!v->active)
                continue;
            memset(&m, 0, sizeof m);
            m.pitch16 = v->pitch16;
            m.inc = pitch_inc((uint32_t)clamp(v->pitch16, 0, 2047));
            m.amp0 = last[k];
            m.amp1 = a;
            m.envq15 = a;
            m.cutoff = (a * pr->fenv) >> 5;            /* ENV -> FILTER */
            m.shape = 64 << 8;
            last[k] = a;
            e->render(t, v, out, CTL, &m);
        }
        for (i = 0; i < CTL; i++) {
            uint32_t x = (uint32_t)out[i];
            int32_t a = out[i] < 0 ? -out[i] : out[i], s = clamp(out[i] / 4, -32767, 32767);
            for (k = 0; k < 4u; k++) {
                h ^= (x >> (8u * k)) & 0xFFu;
                h *= 1099511628211ull;
            }
            if (a > peak)
                peak = a;
            if (f)
                put16(f, (uint32_t)s & 0xFFFFu);
        }
    }
    if (f)
        fclose(f);
    printf("drift/%u %016llx %d %s\n", j, (unsigned long long)h, peak, pr->name);
}

int main(int argc, char **argv)
{
    uint32_t j;
    for (j = 0; j < ENG_DRIFT.npresets; j++)
        render(&ENG_DRIFT, j, argc > 1 ? argv[1] : 0);
    return 0;
}
