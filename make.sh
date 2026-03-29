#!/bin/bash
set -e

# directories
SLIDSC_DIR=compiler
BUILD_DIR=build
BIN_DIR=bin

mkdir -p "$BUILD_DIR" "$BIN_DIR"

# build the slidsc compiler
echo "--- building slidsc compiler ---"
g++ -std=c++17 -Wall \
    "$SLIDSC_DIR/main.cpp" \
    "$SLIDSC_DIR/lexer.cpp" \
    "$SLIDSC_DIR/parser.cpp" \
    "$SLIDSC_DIR/codegen.cpp" \
    -o "$BIN_DIR/slidsc"
echo "slidsc compiler built: $BIN_DIR/slidsc"

# compile hello.sl -> build/hello.ll
echo "--- compiling hello.sl ---"
"$BIN_DIR/slidsc" "$SLIDSC_DIR/hello.sl" -o "$BUILD_DIR/hello.ll"

# assemble hello.ll -> build/hello.o
echo "--- assembling hello.ll ---"
llc "$BUILD_DIR/hello.ll" -o "$BUILD_DIR/hello.o" --filetype=obj --relocation-model=pic

# link hello.o -> bin/hello
echo "--- linking hello ---"
g++ "$BUILD_DIR/hello.o" -o "$BIN_DIR/hello"

echo "--- running hello ---"
"$BIN_DIR/hello"
