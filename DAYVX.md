# DAYV-X

A custom firmware for the M-VAVE FM-1: hazy, slightly detuned synths and crunchy synthesised drums.
Work in progress, on the branch `dayvx`.

DAYV-X is a modified version of [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita
(@kurogedelic), Hügelton Instruments, under the same licence (GPL-3.0-only, see `LICENSE` and
`LICENSING.md`). DAYV-X is not affiliated with or endorsed by M-VAVE or Hügelton Instruments; please report
DAYV-X problems to this repository, not to Felucca.

## What is here so far

| File | What |
| --- | --- |
| `firmware/src/eng_drift.c` | DRIFT: the pad / lead engine (two oscillators, sub, low-pass; a fixed pitch offset per note, a detune LFO per voice (DRFT its depth, RATE its speed), wow and flutter per part). Engine 14, last in the PRESETS order, picked with the ENG knob |
| `firmware/src/age.c` | AGE: the master's tape stage, on the FX button's AGE page. WEAR (wow, flutter, saturation, treble roll-off, dropouts; 0 = off, the mix untouched) and HISS |
| `tests/dayvx/age_host.c` | AGE alone on a computer: pitch, level, roll-off and hiss cases against `age_golden.txt` |
| `tests/dayvx/drift_host.c`, `run.sh` | DRIFT on a computer: `WAV=out sh tests/dayvx/run.sh` writes one WAV per preset |
| `.github/workflows/dayvx-build.yml` | every push: the firmware package built on GitHub (artifact `packages`), and the DRIFT host test |
| `.github/workflows/dayvx-release.yml` | a tag `vX.Y`: the release package, attested |

Until the identity is changed in `tools/build.py` (`PRODUCT`), builds report Felucca's development identity
`FM-1_900`.
