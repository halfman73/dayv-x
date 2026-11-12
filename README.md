# DAYV-X

DAYV-X is a custom firmware for the **M-VAVE FM-1**: hazy, slightly detuned synths, dusty synthesised drums and a
tape stage over the whole mix. Every sound is synthesised; there are no samples of its own.

DAYV-X is a modified version of [Felucca](https://github.com/hugelton/Felucca) 1.8.2-beta by Leo Kuroshita
(@kurogedelic), Hügelton Instruments, and keeps everything Felucca does: four parts, a 64-step sequencer, its
engines, effects, editor protocol and installer. Felucca's own README is [README-FELUCCA.md](README-FELUCCA.md);
its controls, pages and shortcuts apply here unchanged unless this page says otherwise.

DAYV-X is not affiliated with or endorsed by M-VAVE or Hügelton Instruments. Please report DAYV-X problems to this
repository, not to Felucca.

**Status: v0.1, pre-release.** It has been run in emulators and passes Felucca's host tests. It has not yet been
run on a real FM-1. Installing custom firmware is at your own risk.

## What DAYV-X adds

| | Where | What |
| --- | --- | --- |
| **DRIFT** | an engine (PRESETS, the last one: one step back from ANALOG) | Two oscillators, a sub and a low-pass filter, with pitch that never sits still: a detune LFO per voice and wow and flutter per part |
| **AGE** | FX button, the page after CHORUS | A tape stage on the whole mix: WEAR and HISS |
| **DUST** | DRUM engine, KIT = DUST (preset DUST KIT) | A dull, soft drum kit |
| **GRIT** | DRUM engine, second edit page | A low-rate, low-bit hold on any kit |
| **Presets** | FM6 and DRIFT | Bells, a music box, a chime, a slap bass, two acid basses |
| **Phrases** | SAVE > PHRASES | DUSTBEAT, BROKEN, SQUELCH, CHIMES |
| **Look** | MENU > DISPLAY > COLOR | A DAYV-X colour palette, the default on a unit with no saved settings |

At power-on the four parts are: 1 DRIFT ACID SAW (bass), 2 DRIFT SUNFADE (pad), 3 FM6 HAZE BELL (lead),
4 DRUM DUST KIT (drums).

### DRIFT

| Page | Knob 1 | Knob 2 | Knob 3 | Knob 4 |
| --- | --- | --- | --- | --- |
| OSC | WAVE (SAW, PLS, TRI, S+P) | DTN: oscillator 2's detune, cents | DRFT: how far the detune wanders | RATE: how fast (0.05 to 12 Hz) |
| TONE | CUT: filter cutoff | RES: resonance | SUB: the sub oscillator | WOW: wow and flutter for the part |

- Each note starts a few cents off, by a different amount every time.
- Each voice has its own detune LFO, so the notes of a chord drift apart and back.
- At DTN 0 the two oscillators start together, which is what the acid basses use.
- MIDI CC 74 and 71 set CUT and RES.

Presets: SUNFADE, OLD FILM, TV KEYS, WARBLE, DUSK PAD, HAZE LD, SUB DRFT, REEL STR, ACID SAW, ACID SQR.

### AGE

| Knob | What it does |
| --- | --- |
| WEAR | 0 is off and leaves the mix untouched. Turning it up adds wow, flutter and a slow pitch wander, soft saturation and a treble roll-off. Above 48 the level dips briefly every few seconds. The low half of the knob is subtle. |
| HISS | Tape noise under the mix, there in silence too. |

Both are saved with the project and are 0 in a new one. AGE adds up to 4 ms of latency when WEAR is up.

### DUST and GRIT

- **KIT = DUST** selects the kit; TUNE, TONE, DECY, SNAP, ACC and DRV work as on every kit.
- **GRIT** (0 to 127) holds each drum at a lower sample rate with fewer bits, like an early sampler. It works on
  every kit. DUST KIT loads with GRIT at 80.
- GRIT takes the place of Felucca's KICK knob. The standard kit's round kick is now MIDI note 35; note 36 is the
  punchy one.

### Presets

| Engine | Preset | Character |
| --- | --- | --- |
| FM6 | HAZE BELL | A soft, wide bell |
| FM6 | MUSICBOX | Bright, short, high partials |
| FM6 | GLASSY | A brighter, more metallic bell |
| FM6 | TV CHIME | A dull, detuned electric piano |
| FM6 | SLAP BASS | A hard, short FM bass |
| DRIFT | ACID SAW, ACID SQR | Resonant mono basses; they answer the sequencer's accents and slides |

## MIDI

DAYV-X keeps Felucca's fixed CC map, on the channel of the part:

| CC | Sets |
| --- | --- |
| 5 | GLIDE |
| 7 | LEVEL |
| 10 | PAN |
| 71 | The engine's resonance (DRIFT: RES) |
| 72 | Release |
| 73 | Attack |
| 74 | The engine's brightness (DRIFT: CUT, DRUM: TONE, FM6: MLVL) |
| 75 | Decay |
| 91 | Reverb send |
| 93 | Chorus send |
| 94 | Delay send |

Program Change picks a part's preset. Felucca's MIDI LEARN assigns other controls.

## Install

1. Charge the FM-1 fully and keep M-VAVE's official V15 package to hand.
2. Get `dayv-x.fwsc`: from a release of this repository, or from the `packages` artifact of a `build` run under
   Actions.
3. Install it with Felucca's installer: `pip3 install mido python-rtmidi`, then
   `python3 tools/fm1_install.py dayv-x.fwsc`.
4. If the unit does not start, hold OCT- while switching it on and install again. M-VAVE's own updater returns it
   to the official firmware.

DAYV-X identifies itself as `FM-1_947`. While an update is running the loader reports `ota-FM-1_900`, as
Felucca's does. Projects and settings use Felucca's formats.

## Build and test

[BUILDING.md](BUILDING.md) is Felucca's and applies; the package is `build/dayv-x.fwsc`. Every push is built on
GitHub (`.github/workflows/dayvx-build.yml`), and a tag `vX.Y` makes a release (`dayvx-release.yml`).

```sh
sh tests/dayvx/run.sh              # DRIFT and AGE on a computer, against golden renders
WAV=out sh tests/dayvx/run.sh      # the same, and the renders as WAV files
sh tests/run_tests.sh              # Felucca's host suite (needs a finished ./build.sh)
```

| File | What |
| --- | --- |
| `firmware/src/eng_drift.c` | DRIFT |
| `firmware/src/age.c` | AGE |
| `firmware/src/drum_voice.c`, `eng_drum.c` | The DUST kit, GRIT |
| `firmware/src/eng_fm6.c` | The FM presets |
| `firmware/src/engines.c` | The phrases |
| `firmware/src/main.c` | The parts at power-on |
| `tools/gen_ui_palettes.py` | The palette |
| `tests/dayvx/` | DAYV-X's own host tests |

## Licence and credits

DAYV-X is free software under **GPL-3.0-only**, as Felucca is: see [LICENSE](LICENSE), [LICENSING.md](LICENSING.md)
and `LICENSES/` for Felucca's own notices and those of the code it includes, which all apply here.

- Felucca, its engines, drum voices, effects and tools: (C) 2026 Leo Kuroshita, Hügelton Instruments.
- DAYV-X's additions (DRIFT, AGE, DUST, GRIT, its presets, phrases and palette): (C) 2026 halfman73.
- Built with the FM-1 community's shared workflow, [zvenson/fm1-build](https://github.com/zvenson/fm1-build).
