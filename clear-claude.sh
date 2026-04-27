#!/bin/bash
# clear files from claude/<dir>/
set -e

FROM="./claude"
DIRS="bugs sample test work"

for sub in $DIRS; do
    rm -f "$FROM/$sub/Makefile"
    rm -f "$FROM/$sub/*.sl"
    rm -f "$FROM/$sub/*.slh"
    rm -f "$FROM/$sub/*.expected"
done
