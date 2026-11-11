#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 halfman73, DAYV-X
# DAYV-X host test: the DRIFT engine's presets rendered on a computer against tests/dayvx/drift_golden.txt.
#   sh tests/dayvx/run.sh            UPDATE=1 rewrites the golden file (after an intended change of the sound)
#   WAV=dir sh tests/dayvx/run.sh    also writes one WAV per preset into dir
set -e
cd "$(dirname "$0")/../.."
[ -f firmware/engines/src/dsp.c ] || { echo "firmware/engines is empty: git submodule update --init"; exit 1; }
mkdir -p build/dayvx
python3 firmware/engines/tools/gen_engine_tables.py build/dayvx/fe_tables.h
W="-std=c99 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Ibuild/dayvx"
${CC:-cc} -O2 $W -o build/dayvx/drift_host tests/dayvx/drift_host.c
[ -z "$WAV" ] || mkdir -p "$WAV"
build/dayvx/drift_host $WAV > build/dayvx/drift_renders.txt
if [ "${UPDATE:-0}" = 1 ]; then
    cp build/dayvx/drift_renders.txt tests/dayvx/drift_golden.txt
    echo "tests/dayvx/drift_golden.txt rewritten"
    exit 0
fi
diff -u tests/dayvx/drift_golden.txt build/dayvx/drift_renders.txt || { echo "FAIL: DRIFT renders differ"; exit 1; }
echo "ok: $(wc -l < tests/dayvx/drift_golden.txt | tr -d ' ') DRIFT renders as in tests/dayvx/drift_golden.txt"
SAN="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all"
printf 'int main(void){return 0;}\n' > build/dayvx/san_probe.c
if ${CC:-cc} $SAN -o build/dayvx/san_probe build/dayvx/san_probe.c 2>/dev/null && build/dayvx/san_probe; then
    ${CC:-cc} $SAN $W -o build/dayvx/drift_host_san tests/dayvx/drift_host.c
    build/dayvx/drift_host_san | diff -u tests/dayvx/drift_golden.txt - || { echo "FAIL: DRIFT under ASan/UBSan"; exit 1; }
    echo "ok: the same renders under ASan/UBSan"
else
    echo "skip: ASan/UBSan unavailable"
fi
