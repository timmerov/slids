#!/bin/bash
#
# Build the compiler + tests, run the positive and negative suites, and print
# a grand-total summary across all subdirectories. Exits nonzero if any test
# failed (build failures abort early).

OPTS="-j 8"

echo "building slids compiler..."
make ${OPTS} -C compiler_v2/ || { echo "compiler build FAILED"; exit 1; }

echo "building test code..."
make ${OPTS} -C test_v2/ || { echo "test build FAILED"; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo "running positive tests..."
make ${OPTS} -C test_v2/ positives 2>&1 | tee "$TMP/pos.out"

echo "running negative tests..."
make ${OPTS} -C test_v2/ negatives 2>&1 | tee "$TMP/neg.out"

# Aggregate the per-subdir tally lines into grand totals. The runners print
# "N passed, M failed" (positives) and "N passed, S skipped, M failed"
# (negatives); sum those rather than trust make's loop exit status (which only
# reflects the last subdir).
read pos_pass pos_fail < <(awk \
    '/^[0-9]+ passed, [0-9]+ failed$/ { p += $1; f += $3 } END { print p+0, f+0 }' \
    "$TMP/pos.out")
read neg_pass neg_skip neg_fail < <(awk \
    '/^[0-9]+ passed, [0-9]+ skipped, [0-9]+ failed$/ { p += $1; s += $3; f += $5 } END { print p+0, s+0, f+0 }' \
    "$TMP/neg.out")

total_pass=$((pos_pass + neg_pass))
total_fail=$((pos_fail + neg_fail))

echo
echo "==================== SUMMARY ===================="
printf "  positive:  %4d passed, %4d failed\n" "$pos_pass" "$pos_fail"
printf "  negative:  %4d passed, %4d skipped, %4d failed\n" "$neg_pass" "$neg_skip" "$neg_fail"
echo   "  -------------------------------------------------"
printf "  total:     %4d passed, %4d skipped, %4d failed\n" "$total_pass" "$neg_skip" "$total_fail"
echo "================================================="

[ "$total_fail" -eq 0 ]
