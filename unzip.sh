#!/bin/bash
set -e

cd compiler

echo "unzip compiler/files.zip..."
unzip -o files.zip

echo "touching files..."
touch *

echo "changing permission..."
chmod ug+rw,o+r *

echo "removing files..."
rm files.zip *.sl

echo "done!"
