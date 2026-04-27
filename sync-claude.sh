#!/bin/bash
# Copy files from smoke/<dir>/ to ./<dir>/ when content differs.
# Pass --dry-run to preview without writing.
set -e

FROM="./claude"
DIRS="bugs sample test work"

for sub in $DIRS; do
    rsync -ac --itemize-changes "$@" "$FROM/$sub/" "$sub/"
    rm -f "$FROM/$sub/Makefile"
    rm -f "$FROM/$sub/*.sl"
    rm -f "$FROM/$sub/*.slh"
    rm -f "$FROM/$sub/*.expected"
done
