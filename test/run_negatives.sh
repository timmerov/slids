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

set -u

cd "$(dirname "$0")"
SLIDSC=../bin/slidsc
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

pass=0
fail=0
skip=0
fail_lines=()

for src in "$@"; do
    if [ ! -f "$src" ]; then
        echo "skip: $src (not found)"
        continue
    fi
    file_header_emitted=0

    # walk lines; recognize markers and the contiguous //-block that follows.
    nlines=$(wc -l < "$src")
    line_no=1
    while [ "$line_no" -le "$nlines" ]; do
        line=$(sed -n "${line_no}p" "$src")
        marker=""
        substring=""
        case "$line" in
            *EXPECT-ERROR-DEFERRED:*)
                marker="DEFERRED"
                substring="${line#*EXPECT-ERROR-DEFERRED:}"
                substring="${substring# }"
                ;;
            *EXPECT-ERROR:*)
                marker="ACTIVE"
                substring="${line#*EXPECT-ERROR:}"
                substring="${substring# }"
                ;;
        esac

        if [ -z "$marker" ]; then
            line_no=$((line_no + 1))
            continue
        fi

        if [ "$file_header_emitted" -eq 0 ]; then
            echo "$src:"
            file_header_emitted=1
        fi

        if [ "$marker" = "DEFERRED" ]; then
            echo "  SKIP  $substring"
            skip=$((skip + 1))
            line_no=$((line_no + 1))
            continue
        fi

        # find the contiguous //-prefixed block immediately after the marker.
        body_start=$((line_no + 1))
        body_end=$body_start
        while [ "$body_end" -le "$nlines" ]; do
            body_line=$(sed -n "${body_end}p" "$src")
            # stop on another marker — adjacent markers must not chain bodies.
            if printf '%s\n' "$body_line" | grep -q 'EXPECT-ERROR'; then
                break
            fi
            # match leading-whitespace then `//` (extglob via grep to keep
            # the script POSIX-portable).
            if printf '%s\n' "$body_line" | grep -q '^[[:space:]]*//'; then
                body_end=$((body_end + 1))
            else
                break
            fi
        done
        body_end=$((body_end - 1))

        if [ "$body_end" -lt "$body_start" ]; then
            echo "  FAIL  marker has no //-block: $substring"
            fail=$((fail + 1))
            fail_lines+=("$src:$line_no")
            line_no=$((line_no + 1))
            continue
        fi

        # produce the temp variant: uncomment lines [body_start..body_end].
        case_file="$TMPDIR/case_${line_no}.sl"
        awk -v b="$body_start" -v e="$body_end" '
            NR >= b && NR <= e {
                sub(/^([[:space:]]*)\/\//, "\\1")
            }
            { print }
        ' "$src" > "$case_file"

        # run slidsc, capture combined stderr.
        err=$("$SLIDSC" "$case_file" -o "$TMPDIR/case.ll" --import-path . 2>&1)
        rc=$?

        if [ $rc -eq 0 ]; then
            echo "  FAIL  expected error '$substring' but slidsc succeeded"
            fail=$((fail + 1))
            fail_lines+=("$src:$line_no")
        elif printf '%s' "$err" | grep -qF -- "$substring"; then
            echo "  PASS  $substring"
            pass=$((pass + 1))
        else
            last=$(printf '%s' "$err" | tail -1)
            echo "  FAIL  expected '$substring', got: $last"
            fail=$((fail + 1))
            fail_lines+=("$src:$line_no")
        fi

        line_no=$((body_end + 1))
    done
done

echo
echo "$pass passed, $skip skipped, $fail failed"
[ $fail -eq 0 ]
