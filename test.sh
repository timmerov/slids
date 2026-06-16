#!/bin/bash
set -e

make -C test_v2/$1/ $2 && ./bin/$2
