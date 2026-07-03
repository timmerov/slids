#!/bin/bash
set -e

make -C test/$1/ $2 && ./bin/$2
