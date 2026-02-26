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

# 1. Missing arg fails safely
$BIN >/dev/null 2>&1 && check "requires args" "fail" || check "requires args" "ok"

# 2. --help exits 0
$BIN --help >/dev/null 2>&1 && check "--help exits 0" "ok" || check "--help exits 0" "fail"

# 3. Happy path with vendored font
FONT=tests/fonts/proggy_clean.ttf
[ -f "$FONT" ] && check "font fixture present" "ok" || check "font fixture present" "fail"
if [ -f "$FONT" ]; then
    $BIN "$FONT" 16 "$TMPDIR/atlas.png" > "$TMPDIR/atlas.json"

    # PNG magic check
    MAGIC=$(od -An -t x1 -N 8 "$TMPDIR/atlas.png" | tr -d ' \n')
    [ "$MAGIC" = "89504e470d0a1a0a" ] && check "happy path PNG magic" "ok" || check "happy path PNG magic" "fail"

    # JSON key checks (no python dependency)
    grep -q '"glyphs"' "$TMPDIR/atlas.json" && check "JSON has glyphs key" "ok" || check "JSON has glyphs key" "fail"
    grep -q '"65"' "$TMPDIR/atlas.json" && check "JSON has codepoint 65" "ok" || check "JSON has codepoint 65" "fail"

    # texture path is JSON-escaped
    QUOTED_OUT="$TMPDIR/q\".png"
    $BIN "$FONT" 16 "$QUOTED_OUT" > "$TMPDIR/atlas-quote.json"
    grep -q 'q\\\".png"' "$TMPDIR/atlas-quote.json" && check "texture path is escaped in JSON" "ok" || check "texture path is escaped in JSON" "fail"

    # oversize font should fail instead of returning partial garbage metrics
    check_error "oversize atlas fails safely" "not all characters fit" $BIN "$FONT" 5000 "$TMPDIR/too-big.png"
else
    echo "FAIL: $FONT not found"
    FAIL=$((FAIL + 1))
fi

echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
