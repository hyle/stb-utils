#!/usr/bin/env sh
set -e

BIN=${BIN:-./build/stb-noise}
GOLDEN=tests/golden/noise_64x64_s4.png
PASS=0
FAIL=0
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT INT TERM

check() {
    if [ "$2" = "ok" ]; then
        echo "PASS: $1"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $1"
        FAIL=$((FAIL + 1))
    fi
}

check_error() {
    label="$1"
    expected_msg="$2"
    shift 2

    set +e
    stderr=$("$@" 2>&1 >/dev/null)
    code=$?
    set -e

    if [ "$code" -ne 0 ] && [ "$code" -lt 128 ] && echo "$stderr" | grep -qF "$expected_msg"; then
        check "$label" "ok"
    else
        echo "FAIL: $label (exit=$code, stderr='$stderr')"
        FAIL=$((FAIL + 1))
    fi
}

png_magic() {
    file_path="$1"
    if command -v od >/dev/null 2>&1; then
        od -An -t x1 -N 8 "$file_path" | tr -d ' \n'
        return 0
    fi
    if command -v xxd >/dev/null 2>&1; then
        xxd -p -l 8 "$file_path" | tr -d '\n'
        return 0
    fi
    return 127
}

# safety boundary tests
check_error "P1 huge dims" "dimensions out of range" $BIN 99999x99999 "$TMPDIR/overflow.png"
check_error "P2 nan scale" "positive finite number" $BIN 64x64 nan "$TMPDIR/nan.png"
check_error "P2 inf scale" "positive finite number" $BIN 64x64 inf "$TMPDIR/inf.png"
check_error "P2 negative scale" "positive finite number" $BIN 64x64 -1.0 "$TMPDIR/neg.png"
check_error "P3 invalid octaves rejected" "invalid --octaves value" $BIN 64x64 4.0 "$TMPDIR/bad-octaves.png" --octaves 0

# happy path
$BIN 64x64 4.0 "$TMPDIR/ok.png" >/dev/null
set +e
MAGIC=$(png_magic "$TMPDIR/ok.png")
magic_status=$?
set -e
if [ "$magic_status" -ne 0 ]; then
    echo "FAIL: happy path PNG magic (no hex tool available)"
    FAIL=$((FAIL + 1))
elif [ "$MAGIC" = "89504e470d0a1a0a" ]; then
    check "happy path PNG magic" "ok"
else
    check "happy path PNG magic" "fail"
fi

if cmp -s "$TMPDIR/ok.png" "$GOLDEN"; then
    check "happy path matches golden" "ok"
else
    check "happy path matches golden" "fail"
fi

# --help exits 0
$BIN --help >/dev/null 2>&1 && check "--help exits 0" "ok" || check "--help exits 0" "fail"

# --octaves happy path
$BIN 64x64 4.0 "$TMPDIR/octaves.png" --octaves 4 >/dev/null && check "--octaves valid value works" "ok" || check "--octaves valid value works" "fail"

echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
