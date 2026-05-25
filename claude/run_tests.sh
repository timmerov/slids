#!/bin/bash
set -e

OPTS="-j 8"

echo "building slids compiler..."
make ${OPTS} -C compiler_v2/

echo "building test code..."
make ${OPTS} -C test_v2/

echo "running positive tests..."
make ${OPTS} -C test_v2/ positives

echo "running negative tests..."
make ${OPTS} -C test_v2/ negatives

echo "done!"
