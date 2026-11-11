/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 halfman73, DAYV-X */
/* DRIFT (DAYV-X): a hazy polyphonic pad / lead voice. Two band-limited oscillators and a sub square into the
 * trapezoidal low-pass, with three layers of pitch instability:
 *   - a fixed offset per voice, drawn as the note starts (no two notes of a chord sit exactly in tune);
 *   - a slow random walk per voice (DRFT): each voice wanders on its own, so held chords smear and beat;
 *   - WOW, one wow + flutter movement for the whole part (drift_block), as a tape transport would add it.
 * Fixed point, no 64-bit multiplies (bsp/docs/CPU.md), no allocation: the conventions of Felucca-Engines.
 *
 * Voice state: ph[0] OSC 1, ph[1] OSC 2, ph[2] SUB; s[0] s[1] the filter, s[2] the random generator,
 * s[3] the note's fixed offset (Q15, set by note_on), s[4] the walk's value (Q15), s[5] its target (Q15),
 * s[6] blocks until the next target. s[4..6] are kept over a retrigger (keep): the walk never jumps.
 * Part state (drift_p[], by part as WHEEL's): the wow and flutter phases and the part's current pitch offset. */
static const char *const N_DRIFT_WAVE[] = {"SAW", "PLS", "TRI", "S+P"};

#define DRIFT_CT(x) ((x) * 2367 / 1000)              /* cents -> the 1/4096 factor cents_inc() uses */
#define DRIFT_FIX_CT 9                               /* the fixed offset at DRFT 127: +-9 cents */
#define DRIFT_WALK_CT 32                             /* the walk at DRFT 127: +-32 cents */
#define DRIFT_WOW_CT 22                              /* WOW 127: +-22 cents of wow, a third of it flutter */
/* a phase increment a block for f millihertz (a block is CTL samples) */
#define DRIFT_LFO(mhz) ((uint32_t)((((uint64_t)(mhz) << 32) / 1000u) * CTL / FS))

typedef struct {
    uint32_t wow, flt;                               /* phases */
    int32_t rnd, wob;                                /* the generator, a smoothed random value (Q15) */
    int32_t fine;                                    /* this block's offset of the part, 1/4096 */
} drift_part_t;
static drift_part_t drift_p[NPART];

static void drift_block(track_t *t)
{
    drift_part_t *d = &drift_p[(uint32_t)(t - trk) % NPART];
    int32_t depth = t->p[P_E6], x;
    if (!d->rnd)
        d->rnd = (int32_t)(0x2545F491u + (uint32_t)(t - trk));
    d->wow += DRIFT_LFO(570);                        /* 0.57 Hz: a slightly eccentric reel */
    d->flt += DRIFT_LFO(7300);                       /* 7.3 Hz flutter */
    /* a slow random component, so the wow never repeats exactly: a one-pole over noise, ~1.5 Hz */
    d->wob += (((int32_t)(noise32(&d->rnd) >> 16) - 32768) - d->wob) >> (7 + FE_OCT);
    x = (sine_i(d->wow) * 5 + sine_i(d->flt) * 2 + clamp(d->wob * 6, -32767, 32767) * 3) / 10;   /* Q15 */
    d->fine = (x * depth / 127) * DRIFT_CT(DRIFT_WOW_CT) >> 15;
}

static void drift_note_on(track_t *t, voice_t *v)
{
    (void)t;
    if (!v->s[2])
        v->s[2] = (int32_t)(0x1F123BB5u + v->age * 2654435761u);
    v->ph[0] = noise32(&v->s[2]);                    /* free-running oscillators: no two attacks alike */
    v->ph[1] = noise32(&v->s[2]);
    v->ph[2] = v->ph[0] >> 1;
    v->s[0] = v->s[1] = 0;
    v->s[3] = (int32_t)(noise32(&v->s[2]) >> 16) - 32768;
}

static void drift_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    const drift_part_t *d = &drift_p[(uint32_t)(t - trk) % NPART];
    uint32_t wave = (uint32_t)p[P_E0], i;
    int32_t det = p[P_E1], drft = p[P_E2], sub = p[P_E3] * 129, hiss = p[P_E7] * 24;
    int32_t walk = v->s[4], fine, cut;
    uint32_t inc1, inc2, pw;
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
    int32_t ic1 = v->s[0], ic2 = v->s[1], nst = v->s[2];
    tsvf_t flt;

    /* the walk: once a block, towards a target that is drawn again every 0.4 .. 1.9 s */
    if (--v->s[6] <= 0) {
        v->s[5] = (int32_t)(noise32(&nst) >> 16) - 32768;
        v->s[6] = FE_T(550 + (int32_t)(noise32(&nst) >> 21));          /* blocks */
    }
    walk += (v->s[5] - walk) >> (9 + FE_OCT);
    v->s[4] = walk;

    fine = ((v->s[3] * drft / 127) * DRIFT_CT(DRIFT_FIX_CT) >> 15)
         + ((walk * drft / 127) * DRIFT_CT(DRIFT_WALK_CT) >> 15)
         + d->fine;
    inc1 = m->inc + (uint32_t)((int32_t)(m->inc >> 12) * fine);
    /* OSC 2: DTN cents above, and half the walk the other way: the beating between the two wanders too */
    inc2 = cents_inc(m->pitch16, det, m->fine + fine - ((walk * drft / 127) * DRIFT_CT(DRIFT_WALK_CT) >> 16));
    /* pulse width: the shape routings around 50 %, and the walk leans on it a little */
    pw = 0x80000000u + ((uint32_t)(m->shape - (64 << 8)) << 15) + ((uint32_t)(walk * drft / 127) << 13);
    /* the cutoff follows the keys by half, and each note's offset nudges it (+-2 steps) */
    cut = (p[P_E4] << 8) + m->cutoff + ((v->pitch16 - 60 * 16) * 2) + (v->s[3] * drft / 127 >> 6);
    tsvf_coef(&flt, cut, p[P_E5]);

    for (i = 0; i < n; i++) {
        int32_t a, b, s, y, ab;
        switch (wave) {
        case 1:
            a = osc_pulse(ph0, inc1, pw);
            b = osc_pulse(ph1, inc2, pw);
            break;
        case 2:
            a = osc_tri(ph0);
            b = osc_tri(ph1);
            break;
        case 3:
            a = osc_saw(ph0, inc1);
            b = osc_pulse(ph1, inc2, pw);
            break;
        default:
            a = osc_saw(ph0, inc1);
            b = osc_saw(ph1, inc2);
            break;
        }
        s = (a + b) >> 1;
        if (sub)
            s += mulq15(osc_pulse(ph2, inc1 >> 1, 0x80000000u), sub);
        ph0 += inc1;
        ph1 += inc2;
        ph2 += inc1 >> 1;
        y = tsvf_lp(&flt, s >> 1, &ic1, &ic2);
        ab = y < 0 ? -y : y;
        if (ab > 16000) {                             /* the soft knee of ANALOG: only resonance peaks saturate */
            ab = 16000 + (softclip((ab - 16000) * 2) >> 1);
            y = y < 0 ? -ab : ab;
        }
        if (hiss)                                     /* after the filter: it rides the note, as hiss on a recording */
            y += mulq15((int32_t)(noise32(&nst) >> 17) - 16384, hiss);
        out[i] += voice_amp(y * 2, m, i) * 2;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = nst;
}

/* {WAVE, DTN, DRFT, SUB, CUT, RES, WOW, HISS}, {ATK DEC SUS REL}, ENV -> FILTER, MONO, sends */
static const preset_t DRIFT_PRESETS[] = {
    {"SUNFADE", {0, 14, 70, 20, 62, 18, 45, 10}, {70, 90, 105, 95}, 8, 0, FX(0, 40, 30, 70), PAT(5)},
    {"OLD FILM", {3, 18, 95, 0, 55, 25, 80, 30}, {55, 85, 100, 90}, 12, 0, FX(0, 30, 35, 60), PAT(5)},
    {"TV KEYS", {2, 9, 60, 0, 78, 10, 55, 8}, {2, 75, 40, 60}, 20, 0, FX(0, 25, 40, 45), PAT(6)},
    {"WARBLE", {1, 6, 110, 0, 70, 35, 100, 12}, {10, 70, 90, 55}, 25, 1, FX(0, 20, 45, 35), PAT(4)},
    {"DUSK PAD", {0, 24, 85, 45, 48, 12, 35, 18}, {95, 95, 115, 105}, 5, 0, FX(0, 55, 25, 85), PAT(5)},
    {"HAZE LD", {3, 11, 75, 0, 84, 40, 60, 6}, {4, 65, 85, 50}, 30, 1, FX(8, 15, 50, 30), PAT(4)},
    {"SUB DRFT", {2, 4, 40, 90, 44, 20, 25, 0}, {0, 60, 100, 30}, 15, 1, FX(0, 0, 10, 10), PAT(2)},
    {"REEL STR", {1, 20, 90, 10, 66, 15, 70, 15}, {75, 90, 110, 95}, 6, 0, FX(0, 60, 20, 75), PAT(5)},
};

static const engine_t ENG_DRIFT = {
    .name = "DRIFT",
    .page_title = {"OSC", "TONE"},
    .edit = {
        {"WAVE", F_ENUM, 0, 3, 0, N_DRIFT_WAVE, 0},
        {"DTN", F_INT, 0, 127, 14, 0, "ct"},
        {"DRFT", F_PCT, 0, 127, 70, 0, 0},
        {"SUB", F_PCT, 0, 127, 0, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 64, 0, 0},
        {"RES", F_PCT, 0, 127, 18, 0, 0},
        {"WOW", F_PCT, 0, 127, 45, 0, 0},
        {"HISS", F_PCT, 0, 127, 10, 0, 0},
    },
    .presets = DRIFT_PRESETS,
    .npresets = NELEM(DRIFT_PRESETS),
    .note_on = drift_note_on,
    .render = drift_render,
    .block = drift_block,
    .knob = {P_E4, P_E2, P_E6, P_REL},
    .keep = 0x73,                /* the filter and the walk */
};
