#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN_DIR="$SCRIPT_DIR/bin"
TEST_DIR="$SCRIPT_DIR/test"

SAMPLES=(
    blocks
    cast
    class
    comments
    constructor
    for
    function1
    incomplete
    indexop
    inheritance
    lvalues
    macros
    math1 math2
    move
    mutable
    nested
    new_delete
    operators
    parsing
    pointer
    ranges
    returnslid
    sizeof
    strlitcat
    shadowing
    swap
    switch
    tuple
    type_conv
    type_infer
    virtual
)

UPDATE=0
if [[ "${1:-}" == "--update" ]]; then
    UPDATE=1
fi

normalize() {
    sed -e 's/intptr=[0-9][0-9]*/intptr=ADDR/g' \
        -e 's/##date=[A-Za-z]\{3\} [ 0-9][0-9] [0-9]\{4\}/##date=DATE/' \
        -e 's/##time=[0-9]\{2\}:[0-9]\{2\}:[0-9]\{2\}/##time=TIME/'
}

pass=0
fail=0
errors=()

for t in "${SAMPLES[@]}"; do
    bin="$BIN_DIR/$t"
    expected="$TEST_DIR/$t.expected"

    if [[ ! -x "$bin" ]]; then
        echo "SKIP $t (binary not found)"
        continue
    fi

    actual=$("$bin" 2>&1 | normalize)

    if [[ $UPDATE -eq 1 ]]; then
        printf '%s\n' "$actual" > "$expected"
        echo "UPDATE $t"
        continue
    fi

    if [[ ! -f "$expected" ]]; then
        echo "FAIL $t (no .expected file)"
        ((fail++)) || true
        errors+=("$t")
        continue
    fi

    expected_content=$(normalize < "$expected")

    if [[ "$actual" == "$expected_content" ]]; then
        echo "PASS $t"
        ((pass++)) || true
    else
        echo "FAIL $t"
        diff <(printf '%s\n' "$expected_content") <(printf '%s\n' "$actual") | head -20
        ((fail++)) || true
        errors+=("$t")
    fi
done

if [[ $UPDATE -eq 1 ]]; then
    echo "Updated ${#SAMPLES[@]} expected files."
    exit 0
fi

echo ""
echo "$pass passed, $fail failed"
[[ $fail -eq 0 ]]
