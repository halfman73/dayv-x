/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 halfman73, DAYV-X */
/* DAYV-X: AGE (firmware/src/age.c) on a computer, alone. Feeds it a 1 kHz sine at -12 dBFS (pitch, level), white
 * noise (the roll-off) and silence (HISS) in the firmware's 32-frame blocks, and prints one line per case: the
 * FNV-1a hash of the output and its peak (tests/dayvx/run.sh keeps them in age_golden.txt). With a directory as
 * its argument it also writes each case as a stereo WAV there.
 *   cc -O2 -std=c99 -Ibuild/dayvx -o build/dayvx/age_host tests/dayvx/age_host.c */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NVOICE 8
#define NPART 4
#define __attribute__(x)
#include "fe_tables.h"
#include "../../firmware/engines/src/felucca_engine.h"
typedef struct track { int16_t p[1]; voice_t v[NVOICE]; } track_t;
#include "../../firmware/engines/src/dsp.c"
enum { G_DRLVL, G_DRREV, G_COUNT };
static struct { int16_t g[G_COUNT]; } song;
#include "../../firmware/src/age.c"

#define SECS 8
static void put32(FILE *f, uint32_t x) { fputc(x & 255, f); fputc(x >> 8 & 255, f); fputc(x >> 16 & 255, f); fputc(x >> 24, f); }
static void put16(FILE *f, uint32_t x) { fputc(x & 255, f); fputc(x >> 8 & 255, f); }

/* src: 0 a sine, 1 white noise, 2 silence */
static int run(const char *name, int src, int wear, int hiss, const char *dir)
{
    int32_t l[CTL], r[CTL], wet[CTL], peak = 0, rng = 12345;
    uint64_t h = 14695981039346656037ull;
    uint32_t b, i, k, t = 0;
    int same = 1;
    FILE *f = 0;
    char path[512];
    memset(&age, 0, sizeof age);
    song.g[G_WEAR] = (int16_t)wear;
    song.g[G_HISS] = (int16_t)hiss;
    if (dir) {
        snprintf(path, sizeof path, "%s/age_%s.wav", dir, name);
        f = fopen(path, "wb");
        if (!f) { perror(path); return 1; }
        fwrite("RIFF", 1, 4, f); put32(f, 36 + SECS * FS * 4); fwrite("WAVEfmt ", 1, 8, f);
        put32(f, 16); put16(f, 1); put16(f, 2); put32(f, FS); put32(f, FS * 4); put16(f, 4); put16(f, 16);
        fwrite("data", 1, 4, f); put32(f, SECS * FS * 4);
    }
    for (b = 0; b < SECS * FS / CTL; b++) {
        int32_t in[CTL];
        for (i = 0; i < CTL; i++, t++) {
            in[i] = src == 0 ? sine_i(t * (uint32_t)(4294967296.0 * 1000.0 / FS)) >> 2   /* (integers only: the same on any host) */
                  : src == 1 ? ((int32_t)(noise32(&rng) >> 17) - 16384) / 2 : 0;
            l[i] = r[i] = in[i] - in[i] / 4;             /* three quarters dry, a quarter on the FX bus */
            wet[i] = in[i] / 4;
        }
        if (song.g[G_WEAR] > 0 || song.g[G_HISS] > 0)    /* (as fx.c mix_block calls it) */
            age_block(l, r, wet, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t o[2] = {l[i] + wet[i], r[i] + wet[i]};
            same &= o[0] == in[i] && o[1] == in[i];
            for (k = 0; k < 2u; k++) {
                uint32_t x = (uint32_t)o[k], j;
                int32_t a = o[k] < 0 ? -o[k] : o[k];
                for (j = 0; j < 4u; j++) { h ^= (x >> (8u * j)) & 0xFFu; h *= 1099511628211ull; }
                if (a > peak) peak = a;
                if (f) put16(f, (uint32_t)clamp(o[k], -32767, 32767) & 0xFFFFu);
            }
        }
    }
    if (f) fclose(f);
    printf("age/%s %016llx %d%s\n", name, (unsigned long long)h, peak, same ? " untouched" : "");
    return wear == 0 && hiss == 0 && !same;              /* off must be bit for bit */
}

int main(int argc, char **argv)
{
    const char *d = argc > 1 ? argv[1] : 0;
    int bad = 0;
    bad |= run("sine_w0", 0, 0, 0, d);
    bad |= run("sine_w32", 0, 32, 0, d);
    bad |= run("sine_w64", 0, 64, 0, d);
    bad |= run("sine_w100", 0, 100, 0, d);
    bad |= run("sine_w127", 0, 127, 0, d);
    bad |= run("noise_w0", 1, 0, 0, d);
    bad |= run("noise_w64", 1, 64, 0, d);
    bad |= run("noise_w127", 1, 127, 0, d);
    bad |= run("silence_h127", 2, 0, 127, d);
    bad |= run("silence_w64_h127", 2, 64, 127, d);
    if (bad)
        printf("FAIL: AGE at WEAR 0 / HISS 0 changed the mix\n");
    return bad;
}
