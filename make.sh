#!/bin/bash
set -e

echo "building slids compiler..."
make -C compiler/

echo "building sample code..."
make -C sample/

echo "done!"
