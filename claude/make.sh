#!/bin/bash
set -e

OPTS="-j 8"

echo "building slids compiler..."
make ${OPTS} -C compiler/

echo "building library..."
make ${OPTS} -C lib/

echo "building test code..."
make ${OPTS} -C test/

echo "done!"
