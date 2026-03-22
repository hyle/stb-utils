#!/usr/bin/env sh
set -e

BIN=${BIN:-./build/stb-atlas}
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

$BIN >/dev/null 2>&1 && check "requires args" "fail" || check "requires args" "ok"
$BIN --help >/dev/null 2>&1 && check "--help exits 0" "ok" || check "--help exits 0" "fail"

FONT=tests/fonts/proggy_clean.ttf
[ -f "$FONT" ] && check "font fixture present" "ok" || check "font fixture present" "fail"
if [ -f "$FONT" ]; then
    $BIN "$FONT" 16 "$TMPDIR/atlas.png" > "$TMPDIR/atlas.json"

    MAGIC=$(od -An -t x1 -N 8 "$TMPDIR/atlas.png" | tr -d ' \n')
    [ "$MAGIC" = "89504e470d0a1a0a" ] && check "happy path PNG magic" "ok" || check "happy path PNG magic" "fail"

    grep -q '"glyphs"' "$TMPDIR/atlas.json" && check "JSON has glyphs key" "ok" || check "JSON has glyphs key" "fail"
    grep -q '"65"' "$TMPDIR/atlas.json" && check "JSON has codepoint 65" "ok" || check "JSON has codepoint 65" "fail"

    QUOTED_OUT="$TMPDIR/q\".png"
    $BIN "$FONT" 16 "$QUOTED_OUT" > "$TMPDIR/atlas-quote.json"
    grep -q 'q\\\".png"' "$TMPDIR/atlas-quote.json" && check "texture path is escaped in JSON" "ok" || check "texture path is escaped in JSON" "fail"

    UTF8_BASENAME=$(printf 'caf\303\251')
    UTF8_OUT="$TMPDIR/$UTF8_BASENAME.png"
    $BIN "$FONT" 16 "$UTF8_OUT" > "$TMPDIR/atlas-utf8.json"
    grep -q -- "$UTF8_BASENAME.png\"" "$TMPDIR/atlas-utf8.json" && check "texture path valid UTF-8 is preserved in JSON" "ok" || check "texture path valid UTF-8 is preserved in JSON" "fail"

    INVALID_UTF8_OUT="$TMPDIR/$(printf 'bad\377').png"
    if $BIN "$FONT" 16 "$INVALID_UTF8_OUT" > "$TMPDIR/atlas-invalid-utf8.json" 2>/dev/null; then
        grep -q 'bad\\u00ff.png"' "$TMPDIR/atlas-invalid-utf8.json" && check "texture path non-UTF8 bytes are escaped" "ok" || check "texture path non-UTF8 bytes are escaped" "fail"
    else
        echo "SKIP: texture path non-UTF8 bytes are escaped (host rejected non-UTF8 path)"
    fi

    check_error "oversize atlas fails safely" "not all characters fit" $BIN "$FONT" 5000 "$TMPDIR/too-big.png"

    printf '\000\001\000\000' > "$TMPDIR/truncated.ttf"
    check_error "truncated sfnt rejected safely" "valid or supported TTF/OTF" $BIN "$TMPDIR/truncated.ttf" 16 "$TMPDIR/truncated.png"
else
    echo "FAIL: $FONT not found"
    FAIL=$((FAIL + 1))
fi

echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
