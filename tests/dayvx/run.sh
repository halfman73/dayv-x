#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 halfman73, DAYV-X
# DAYV-X host tests: the DRIFT engine's presets (tests/dayvx/drift_golden.txt) and AGE, the master's tape stage
# (tests/dayvx/age_golden.txt; WEAR 0 / HISS 0 must leave the mix bit for bit), rendered on a computer.
#   sh tests/dayvx/run.sh            UPDATE=1 rewrites the golden files (after an intended change of the sound)
#   WAV=dir sh tests/dayvx/run.sh    also writes the renders as WAV files into dir
set -e
cd "$(dirname "$0")/../.."
[ -f firmware/engines/src/dsp.c ] || { echo "firmware/engines is empty: git submodule update --init"; exit 1; }
mkdir -p build/dayvx
python3 firmware/engines/tools/gen_engine_tables.py build/dayvx/fe_tables.h
W="-std=c99 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Ibuild/dayvx"
${CC:-cc} -O2 $W -o build/dayvx/drift_host tests/dayvx/drift_host.c
${CC:-cc} -O2 $W -o build/dayvx/age_host tests/dayvx/age_host.c
[ -z "$WAV" ] || mkdir -p "$WAV"
build/dayvx/drift_host $WAV > build/dayvx/drift_renders.txt
build/dayvx/age_host $WAV > build/dayvx/age_renders.txt || { cat build/dayvx/age_renders.txt; exit 1; }
if [ "${UPDATE:-0}" = 1 ]; then
    cp build/dayvx/drift_renders.txt tests/dayvx/drift_golden.txt
    cp build/dayvx/age_renders.txt tests/dayvx/age_golden.txt
    echo "tests/dayvx/drift_golden.txt and age_golden.txt rewritten"
    exit 0
fi
diff -u tests/dayvx/drift_golden.txt build/dayvx/drift_renders.txt || { echo "FAIL: DRIFT renders differ"; exit 1; }
echo "ok: $(wc -l < tests/dayvx/drift_golden.txt | tr -d ' ') DRIFT renders as in tests/dayvx/drift_golden.txt"
diff -u tests/dayvx/age_golden.txt build/dayvx/age_renders.txt || { echo "FAIL: AGE renders differ"; exit 1; }
echo "ok: $(wc -l < tests/dayvx/age_golden.txt | tr -d ' ') AGE renders as in tests/dayvx/age_golden.txt, off = untouched"
SAN="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all"
printf 'int main(void){return 0;}\n' > build/dayvx/san_probe.c
if ${CC:-cc} $SAN -o build/dayvx/san_probe build/dayvx/san_probe.c 2>/dev/null && build/dayvx/san_probe; then
    ${CC:-cc} $SAN $W -o build/dayvx/drift_host_san tests/dayvx/drift_host.c
    build/dayvx/drift_host_san | diff -u tests/dayvx/drift_golden.txt - || { echo "FAIL: DRIFT under ASan/UBSan"; exit 1; }
    ${CC:-cc} $SAN $W -o build/dayvx/age_host_san tests/dayvx/age_host.c
    build/dayvx/age_host_san | diff -u tests/dayvx/age_golden.txt - || { echo "FAIL: AGE under ASan/UBSan"; exit 1; }
    echo "ok: the same renders under ASan/UBSan"
else
    echo "skip: ASan/UBSan unavailable"
fi
