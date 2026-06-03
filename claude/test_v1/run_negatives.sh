#!/usr/bin/env bash
#
# Drive negative compile-error tests in this directory.
#
# Each catalog .sl file may contain `//-EXPECT-ERROR: <substring>` markers,
# each followed immediately by a contiguous //-prefixed block (the disabled
# negative case). The runner produces a temp variant per case with that one
# block uncommented, runs slidsc, and asserts:
#   - slidsc errored (non-zero exit)
#   - stderr contains <substring>
#
# Marker variants:
#   //-EXPECT-ERROR: <substring>           run the case, expect the substring
#   //-EXPECT-ERROR-DEFERRED: <reason>     skip the case, log the reason
#
# Example:
#   //-EXPECT-ERROR: cannot infer loop type variable
#   //for (x : (Simple(1), Simple(2))) {
#   //    __println("...");
#   //}
#
# Parallelization:
#   The slidsc invocations are dispatched concurrently via `xargs -P`. Source
#   parsing and result reduction stay sequential so output remains in source
#   order. Set NEG_JOBS=N to override the parallel-job count (default nproc).

set -u

# --- worker mode -------------------------------------------------------------
# When the script is re-invoked by xargs with `--worker`, it just runs one
# slidsc case and writes a structured result line to $NEG_TMPDIR/r_$key.
# Input is a single tab-delimited arg: key<TAB>src<TAB>substring<TAB>body_start<TAB>body_end.
if [ "${1:-}" = "--worker" ]; then
    IFS=$'\t' read -r key src substring body_start body_end <<< "$2"
    case_file="$NEG_TMPDIR/case_$key.sl"
    awk -v b="$body_start" -v e="$body_end" '
        NR >= b && NR <= e { sub(/\/\//, "") }
        { print }
    ' "$src" > "$case_file"
    err=$("$SLIDSC" "$case_file" -o "$NEG_TMPDIR/case_$key.ll" --import-path . 2>&1)
    rc=$?
    # Strip rendered source-context lines so the marker substring can't match
    # its own appearance in the listing slidsc prints with each diagnostic.
    diag=$(printf '%s' "$err" | grep -Ev '^[[:space:]]*[0-9]+:|^[[:space:]]*\^')
    result_file="$NEG_TMPDIR/r_$key"
    if [ $rc -eq 0 ]; then
        printf 'FAIL\texpected error '\''%s'\'' but slidsc succeeded\n' "$substring" > "$result_file"
    elif printf '%s' "$diag" | grep -qF -- "$substring"; then
        printf 'PASS\t%s\n' "$substring" > "$result_file"
    else
        last=$(printf '%s' "$err" | tail -1)
        printf 'FAIL\texpected '\''%s'\'', got: %s\n' "$substring" "$last" > "$result_file"
    fi
    exit 0
fi

# --- driver mode -------------------------------------------------------------
# Resolve SELF before any cd so the dirname-of-$0 trick still works regardless
# of where the user invoked the script from.
SELF=$(cd "$(dirname "$0")" && pwd)/$(basename "$0")
cd "$(dirname "$0")"
SLIDSC=../bin/slidsc
NEG_TMPDIR=$(mktemp -d)
trap 'rm -rf "$NEG_TMPDIR"' EXIT
export NEG_TMPDIR SLIDSC

NEG_JOBS="${NEG_JOBS:-$(nproc 2>/dev/null || echo 4)}"

JOBS_FILE="$NEG_TMPDIR/jobs.tsv"
: > "$JOBS_FILE"

# Phase 1: parse markers and body ranges per file via awk; emit TSV.
# fields: key | src | substring | body_start | body_end | kind
# kind ∈ { SKIP, JOB, NOBODY }
src_index=0
for src in "$@"; do
    if [ ! -f "$src" ]; then
        echo "skip: $src (not found)"
        continue
    fi
    src_index=$((src_index + 1))
    awk -v src="$src" -v sidx="$src_index" '
        function flush_pending() {
            if (pending) {
                if (body_start > 0)
                    printf "%05d_%07d\t%s\t%s\t%d\t%d\tJOB\n",
                        sidx, marker_line, src, substring, body_start, body_end
                else
                    printf "%05d_%07d\t%s\t%s\t0\t0\tNOBODY\n",
                        sidx, marker_line, src, substring
                pending = 0
            }
        }
        /EXPECT-ERROR-DEFERRED:/ {
            flush_pending()
            sub(/.*EXPECT-ERROR-DEFERRED:[[:space:]]*/, "", $0)
            printf "%05d_%07d\t%s\t%s\t0\t0\tSKIP\n", sidx, NR, src, $0
            next
        }
        /EXPECT-ERROR:/ {
            flush_pending()
            sub(/.*EXPECT-ERROR:[[:space:]]*/, "", $0)
            substring = $0
            marker_line = NR
            body_start = 0
            body_end = 0
            pending = 1
            next
        }
        pending {
            if ($0 ~ /^[[:space:]]*\/\//) {
                if (body_start == 0) body_start = NR
                body_end = NR
            } else {
                flush_pending()
            }
        }
        END { flush_pending() }
    ' "$src" >> "$JOBS_FILE"
done

# Phase 2: dispatch slidsc invocations in parallel.
# One job per input line; xargs -d '\n' keeps tabs/spaces intact within args.
# The worker re-splits on tabs.
if [ -s "$JOBS_FILE" ]; then
    awk -F'\t' '$6 == "JOB" {
        print $1 "\t" $2 "\t" $3 "\t" $4 "\t" $5
    }' "$JOBS_FILE" \
    | xargs -d '\n' -I {} -P "$NEG_JOBS" "$SELF" --worker {}
fi

# Phase 3: walk the job list in source order, print results, tally.
pass=0
fail=0
skip=0
cur_src=""
while IFS=$'\t' read -r key src substring body_start body_end kind; do
    if [ "$src" != "$cur_src" ]; then
        echo "$src:"
        cur_src="$src"
    fi
    case "$kind" in
        SKIP)
            echo "  SKIP  $substring"
            skip=$((skip + 1))
            ;;
        NOBODY)
            echo "  FAIL  marker has no //-block: $substring"
            fail=$((fail + 1))
            ;;
        JOB)
            result_file="$NEG_TMPDIR/r_$key"
            if [ -f "$result_file" ]; then
                # result file: "<STATUS>\t<message>\n"
                status=$(cut -f1 "$result_file")
                rest=$(cut -f2- "$result_file")
                if [ "$status" = "PASS" ]; then
                    echo "  PASS  $rest"
                    pass=$((pass + 1))
                else
                    echo "  FAIL  $rest"
                    fail=$((fail + 1))
                fi
            else
                echo "  FAIL  no result file for '$substring'"
                fail=$((fail + 1))
            fi
            ;;
    esac
done < "$JOBS_FILE"

echo
echo "$pass passed, $skip skipped, $fail failed"
[ $fail -eq 0 ]
