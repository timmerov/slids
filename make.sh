#!/bin/bash
set -e

OPTS="-j 4"

echo "building slids compiler..."
make ${OPTS} -C compiler/

echo "building bugs code..."
make -C bugs/

echo "building sample code..."
make -C sample/

echo "building test code..."
make -C test/

echo "building work directory..."
make -C work/

echo "done!"
