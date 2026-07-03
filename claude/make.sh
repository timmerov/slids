#!/bin/bash
set -e

OPTS="-j 8"

echo "building slids compiler..."
make ${OPTS} -C compiler/

echo "building test code..."
make ${OPTS} -C test/

echo "done!"
