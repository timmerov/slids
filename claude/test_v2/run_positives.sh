#!/usr/bin/env bash
#
# Drive positive tests for one subdirectory of test_v2/.
#
# Usage: run_positives.sh <subdir-name> <sample> [<sample>...]
#
# For each sample, runs $SCRIPT_DIR/../bin/<sample> (built by `make all`),
# captures stdout+stderr, and compares against exp.<sample> in the current
# working directory. PASS iff exit-code 0 AND output matches the golden file
# byte-for-byte.
#
# Prints a per-subdir tally. Exits 0 if all PASS, 1 otherwise.

set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
BIN_DIR="$SCRIPT_DIR/../bin"

if [ $# -lt 1 ]; then
    echo "usage: $0 <subdir-name> <sample>..." >&2
    exit 2
fi

subdir="$1"
shift

POS_TMPDIR=$(mktemp -d)
trap 'rm -rf "$POS_TMPDIR"' EXIT

echo "$subdir/:"

pass=0
fail=0

for sample in "$@"; do
    bin="$BIN_DIR/$sample"
    exp="exp.$sample"
    if [ ! -x "$bin" ]; then
        echo "  FAIL  $sample: missing binary $bin"
        fail=$((fail + 1))
        continue
    fi
    if [ ! -f "$exp" ]; then
        echo "  FAIL  $sample: missing $exp"
        fail=$((fail + 1))
        continue
    fi
    out="$POS_TMPDIR/$sample.out"
    "$bin" > "$out" 2>&1
    rc=$?
    if [ $rc -eq 0 ] && cmp -s "$out" "$exp"; then
        echo "  PASS  $sample"
        pass=$((pass + 1))
    else
        echo "  FAIL  $sample"
        if [ $rc -ne 0 ]; then
            echo "    exit=$rc"
        fi
        diff "$exp" "$out" 2>/dev/null | sed 's/^/    /'
        fail=$((fail + 1))
    fi
done

echo
echo "$pass passed, $fail failed"
[ $fail -eq 0 ]
