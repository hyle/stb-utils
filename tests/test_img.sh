#!/usr/bin/env sh
set -e

BIN=${BIN:-./build/stb-img}
NOISE_BIN=${NOISE_BIN:-./build/stb-noise}
SKIP_RESIZE_TEST=${SKIP_RESIZE_TEST:-0}
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

    if [ "$code" -ne 0 ] && [ "$code" -lt 128 ] && echo "$stderr" | grep -qF -- "$expected_msg"; then
        check "$label" "ok"
    else
        echo "FAIL: $label (exit=$code, stderr='$stderr')"
        FAIL=$((FAIL + 1))
    fi
}

hex_bytes() {
    file_path="$1"
    offset="$2"
    length="$3"
    if command -v od >/dev/null 2>&1; then
        od -An -t x1 -j "$offset" -N "$length" "$file_path" | tr -d ' \n'
        return 0
    fi
    if command -v xxd >/dev/null 2>&1; then
        xxd -p -s "$offset" -l "$length" "$file_path" | tr -d '\n'
        return 0
    fi
    return 127
}

$NOISE_BIN 40x30 4.0 "$TMPDIR/source.png" >/dev/null

$BIN --help >/dev/null 2>&1 && check "--help exits 0" "ok" || check "--help exits 0" "fail"
check_error "missing args rejected" "missing input or output path" $BIN
check_error "bad resize rejected" "invalid --resize value" $BIN "$TMPDIR/source.png" "$TMPDIR/bad.png" --resize nope
check_error "bad filter rejected" "invalid --filter value" $BIN "$TMPDIR/source.png" "$TMPDIR/bad.png" --resize 16x12 --filter nope
check_error "filter without resize rejected" "--filter requires --resize" $BIN "$TMPDIR/source.png" "$TMPDIR/out.png" --filter mitchell
check_error "bad extension rejected" "unsupported output format" $BIN "$TMPDIR/source.png" "$TMPDIR/out.webp"

$BIN "$TMPDIR/source.png" "$TMPDIR/out.bmp" >/dev/null
BMP_MAGIC=$(hex_bytes "$TMPDIR/out.bmp" 0 2 || true)
[ "$BMP_MAGIC" = "424d" ] && check "png to bmp writes bmp" "ok" || check "png to bmp writes bmp" "fail"

if [ "$SKIP_RESIZE_TEST" -eq 0 ]; then
    $BIN "$TMPDIR/source.png" "$TMPDIR/resized.png" --resize 16x12 >/dev/null
    PNG_MAGIC=$(hex_bytes "$TMPDIR/resized.png" 0 8 || true)
    PNG_DIMS=$(hex_bytes "$TMPDIR/resized.png" 16 8 || true)
    [ "$PNG_MAGIC" = "89504e470d0a1a0a" ] && check "resized png magic" "ok" || check "resized png magic" "fail"
    [ "$PNG_DIMS" = "000000100000000c" ] && check "resize dimensions encoded" "ok" || check "resize dimensions encoded" "fail"

    $BIN "$TMPDIR/source.png" "$TMPDIR/filter-point.png" --resize 9x7 --filter point >/dev/null
    FILTER_MAGIC=$(hex_bytes "$TMPDIR/filter-point.png" 0 8 || true)
    FILTER_DIMS=$(hex_bytes "$TMPDIR/filter-point.png" 16 8 || true)
    [ "$FILTER_MAGIC" = "89504e470d0a1a0a" ] && check "filtered resize png magic" "ok" || check "filtered resize png magic" "fail"
    [ "$FILTER_DIMS" = "0000000900000007" ] && check "filtered resize dimensions encoded" "ok" || check "filtered resize dimensions encoded" "fail"

    $BIN "$TMPDIR/source.png" "$TMPDIR/filter-point.bmp" --resize 9x7 --filter point >/dev/null
    $BIN "$TMPDIR/source.png" "$TMPDIR/filter-triangle.bmp" --resize 9x7 --filter triangle >/dev/null
    if cmp -s "$TMPDIR/filter-point.bmp" "$TMPDIR/filter-triangle.bmp"; then
        check "point and triangle filters differ" "fail"
    else
        check "point and triangle filters differ" "ok"
    fi
else
    echo "SKIP: resize path not exercised in this run"
fi

echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
