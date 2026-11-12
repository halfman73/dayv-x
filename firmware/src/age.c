/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 halfman73, DAYV-X */
/* AGE (DAYV-X): the master's tape stage, on the whole mix (the dry mix and the FX buses) before MASTER and the
 * limiter. Two globals, on the FX button's AGE page:
 *   WEAR (G_WEAR)  0 = off: the mix passes untouched, bit for bit. Above, all of these grow with it:
 *                  - wow (0.57 Hz), flutter (7.3 Hz) and a slow random wander of the pitch, from a delay line
 *                    read at a moving distance (WEAR 127: about +-15 cents of wow, +-5 of flutter; the depth
 *                    follows WEAR squared, so the low half of the knob stays subtle);
 *                  - saturation: the soft clipper driven harder (x1 .. x2.5), a little asymmetric, level kept;
 *                  - the top rolled off, 12 dB / octave, its corner from above the band down to ~4.5 kHz;
 *                  - above 48, dropouts: every few seconds the level dips for a moment (up to ~45 %).
 *   HISS (G_HISS)  white noise under the mix, about -36 dBFS at 127, there in silence too, as on a tape.
 * The two reuse Felucca's inert globals G_DRLVL / G_DRREV (ids 25, 26: the formats and the editor protocol keep
 * G_COUNT), so a project stores them; a project of the GM drum part's days loads them as 0 (project.c).
 * Cost: a 1024-frame stereo delay line (4 KiB of the pool); the line's delay (0.4 .. 4 ms) is the stage's latency.
 * Fixed point, no 64-bit multiplies (bsp/docs/CPU.md). Called from fx.c mix_block, in the audio ISR. */
#define G_WEAR G_DRLVL
#define G_HISS G_DRREV
#define AGE_N 1024u                                   /* the line, frames (a power of two) */
#define AGE_INC(mhz) ((uint32_t)((((uint64_t)(mhz) << 32) / 1000u) / FS))   /* a phase increment a sample */
#define AGE_WOW_A (110 << 8)                          /* the read distance's swing at WEAR 127, frames Q8: wow */
#define AGE_FLT_A ((26 << 8) / 10)                    /* .. flutter */
#define AGE_RND_A (40 << 8)                           /* .. the random wander */

static struct {
    int16_t line[2][AGE_N];
    uint32_t wp, wow, flt;
    int32_t rnd, wob, wob2;                           /* the generator; the wander, smoothed twice (Q15) */
    int32_t depth;                                    /* WEAR squared / 127, gliding (Q8): the line's swing */
    int32_t d;                                        /* the read distance now, frames Q8 */
    int32_t lp[2], lp2[2];                            /* the roll-off's states (two poles a channel) */
    int32_t gain, gain_to;                            /* the dropouts: Q15 */
    int32_t drop_in, drop_len;                        /* samples until the next one; samples left of this one */
    uint8_t on;
} age __attribute__((section(".pool")));

static void age_block(int32_t *l, int32_t *r, int32_t *wet, uint32_t n)
{
    int32_t wear = song.g[G_WEAR], hiss = song.g[G_HISS] * 8;
    int32_t w2, d_to, dd, drive, makeup, a, bias, b0, blend;
    uint32_t i, k;
    if (wear <= 0) {
        age.on = 0;
        if (hiss > 0) {
            if (!age.rnd)
                age.rnd = 0x3C6EF372;
            for (i = 0; i < n; i++) {
                l[i] += mulq15((int32_t)(noise32(&age.rnd) >> 17) - 16384, hiss);
                r[i] += mulq15((int32_t)(noise32(&age.rnd) >> 17) - 16384, hiss);
            }
        }
        return;
    }
    if (!age.on) {                                    /* switched on: an empty tape, no stale sound */
        for (k = 0; k < 2u; k++)
            for (i = 0; i < AGE_N; i++)
                age.line[k][i] = 0;
        age.lp[0] = age.lp[1] = age.lp2[0] = age.lp2[1] = 0;
        age.gain = age.gain_to = 32767;
        age.drop_in = FS * 2;
        age.drop_len = 0;
        age.d = 16 << 8;
        age.depth = age.wob = age.wob2 = 0;
        if (!age.rnd)
            age.rnd = 0x3C6EF372;
        age.on = 1;
    }
    /* the swing follows WEAR squared, and glides there (~0.4 s): the read distance, hence the pitch, never jumps
     * when the knob turns */
    age.depth += ((wear * wear / 127 << 8) - age.depth) >> 9;
    w2 = age.depth;                                   /* 0 .. 127 << 8 */
    /* the read distance at the end of this block: wow + flutter + the wander around a centre that clears them.
     * The wander is noise through two one-poles (1.7 Hz, 0.4 Hz): its slope, which is what one hears, stays small */
    age.wow += AGE_INC(570) * n;
    age.flt += AGE_INC(7300) * n;
    age.wob += (((int32_t)(noise32(&age.rnd) >> 16) - 32768) - age.wob) >> 7;
    age.wob2 += (age.wob - age.wob2) >> 9;
    d_to = (16 << 8) + (((AGE_WOW_A + AGE_FLT_A + AGE_RND_A) >> 4) * (w2 >> 4)) / 127
         + (((sine_i(age.wow) >> 4) * (((AGE_WOW_A >> 4) * (w2 >> 4)) / 127)) >> 11)
         + (((sine_i(age.flt) >> 4) * (((AGE_FLT_A >> 4) * (w2 >> 4)) / 127)) >> 11)
         + (((clamp(age.wob2 * 16, -32767, 32767) >> 4) * (((AGE_RND_A >> 4) * (w2 >> 4)) / 127)) >> 11);
    d_to = clamp(d_to, 2 << 8, (int32_t)(AGE_N - 4u) << 8);
    dd = (d_to - age.d) / (int32_t)n;
    drive = 1024 + wear * 12;                         /* Q10: x1 .. x2.5 */
    makeup = 32768 * 1024 / (1024 + wear * 10);        /* Q15: moderate levels keep theirs, the peaks are squashed */
    bias = wear * 12;                                 /* the asymmetry: even harmonics */
    b0 = softclip(bias);
    blend = wear < 32 ? wear * 4 : 127;               /* the first quarter of the knob fades the saturation in */
    a = 32767 - wear * 135;                           /* the roll-off's coefficient, Q15: open .. ~4.5 kHz */
    /* the dropouts */
    if (wear > 48) {
        if (age.drop_len > 0) {
            age.drop_len -= (int32_t)n;
            if (age.drop_len <= 0)
                age.gain_to = 32767;
        } else if ((age.drop_in -= (int32_t)n) <= 0) {
            uint32_t q = noise32(&age.rnd);
            age.gain_to = 32767 - (wear - 48) * (60 + (int32_t)(q & 127u));
            age.drop_len = (int32_t)(FS / 25 + (q >> 8) % (FS / 8));          /* 40 .. 165 ms */
            age.drop_in = (int32_t)(FS * 3 / 2 + (q >> 12) % (FS * 9 / 2));   /* the next in 1.5 .. 6 s */
        }
    } else {
        age.gain_to = 32767;
        age.drop_len = 0;
    }
    age.gain += (age.gain_to - age.gain) >> 5;

    for (i = 0; i < n; i++) {
        int32_t in[2], y;
        uint32_t rp, fr;
        in[0] = l[i] + wet[i];
        in[1] = r[i] + wet[i];
        age.d += dd;
        rp = age.wp - (uint32_t)(age.d >> 8) - 1u;
        fr = (uint32_t)age.d & 255u;
        for (k = 0; k < 2u; k++) {
            int32_t x = clamp(in[k], -400000, 400000), s, s0, s1;
            /* the saturation, then onto the tape */
            s = mulq15(softclip((((x >> 2) * drive) >> 8) + bias) - b0, makeup);
            x += ((s - x) * blend) / 127;
            age.line[k][age.wp & (AGE_N - 1u)] = (int16_t)clamp(x, -32767, 32767);
            /* off the tape, a moving distance behind */
            s0 = age.line[k][rp & (AGE_N - 1u)];
            s1 = age.line[k][(rp + 1u) & (AGE_N - 1u)];
            y = s1 + (((s0 - s1) * (int32_t)fr) >> 8);
            age.lp[k] += (((y - age.lp[k]) >> 1) * a) >> 14;
            age.lp2[k] += (((age.lp[k] - age.lp2[k]) >> 1) * a) >> 14;
            y = mulq15(age.lp2[k], age.gain);
            if (hiss)
                y += mulq15((int32_t)(noise32(&age.rnd) >> 17) - 16384, hiss);
            in[k] = y;
        }
        age.wp++;
        l[i] = in[0];
        r[i] = in[1];
        wet[i] = 0;
    }
    age.d = d_to;
}
