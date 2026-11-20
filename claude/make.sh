#!/bin/bash
set -e

OPTS="-j 8"

# Every Makefile links into a shared bin/ directory. Two Makefiles naming the
# same binary silently overwrite each other, so list every binary first and
# stop on a duplicate. print-bins builds nothing.
echo "checking binary names..."
bins=$(for d in compiler examples test user; do
    make -s --no-print-directory -C $d print-bins
done)
clashes=$(echo "$bins" | awk '{ n[$1]++; dirs[$1] = dirs[$1] " " $2 }
    END { for (b in n) if (n[b] > 1) print b ":" dirs[b] }')
if [ -n "$clashes" ]; then
    echo "**Error** binary built by more than one Makefile:"
    echo "$clashes"
    exit 1
fi

echo "building slids compiler..."
make ${OPTS} -C compiler/

echo "building library..."
make ${OPTS} -C lib/

echo "building example code..."
make ${OPTS} -C examples/

echo "building test code..."
make ${OPTS} -C test/

echo "building user code..."
make ${OPTS} -C user/

echo "done!"
