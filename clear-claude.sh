#!/bin/bash
# clear files from claude/<dir>/
set -e

FROM="./claude"
DIRS="bugs sample test work"

for sub in $DIRS; do
    rm -fv "$FROM/$sub/Makefile"
    rm -fv "$FROM/$sub/"*.sl
    rm -fv "$FROM/$sub/"*.slh
    rm -fv "$FROM/$sub/"*.expected
done
