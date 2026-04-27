#!/bin/bash
# Copy files from smoke/<dir>/ to ./<dir>/ when content differs.
# Pass --dry-run to preview without writing.
set -e

FROM="./claude"
DIRS="bugs sample test work"

for sub in $DIRS; do
    rsync -ac --itemize-changes "$@" "$FROM/$sub/" "$sub/"
done
