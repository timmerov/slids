#!/bin/bash
set -e

OPTS="-j 8"

echo "building slids compiler..."
make ${OPTS} -C compiler/

echo "building bugs code..."
make ${OPTS} -C bugs/

echo "building sample code..."
make ${OPTS} -C sample/

echo "building test code..."
make ${OPTS} -C test/

echo "building work directory..."
make ${OPTS} -C work/

echo "done!"
