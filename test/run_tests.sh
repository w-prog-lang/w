#!/usr/bin/env bash
set -u

WLANGC=./build/wlangc
CASES_DIR=test/cases
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

pass=0
fail=0

for src in "$CASES_DIR"/*.w; do
    name=$(basename "$src" .w)
    expect_file="$CASES_DIR/$name.expect"

    if [ ! -f "$expect_file" ]; then
        echo "SKIP $name (no .expect file)"
        continue
    fi

    expect=$(cat "$expect_file")
    out_c="$TMPDIR/$name.c"
    out_bin="$TMPDIR/$name"

    compile_log=$("$WLANGC" "$src" "$out_c" 2>&1)
    compile_status=$?

    case "$expect" in
    parse_fail)
        if [ $compile_status -ne 0 ] && echo "$compile_log" | grep -q "parse error"; then
            echo "PASS $name (parse_fail)"
            pass=$((pass + 1))
        else
            echo "FAIL $name: expected parse_fail, got status=$compile_status"
            echo "$compile_log"
            fail=$((fail + 1))
        fi
        ;;

    sema_fail)
        if [ $compile_status -ne 0 ] && echo "$compile_log" | grep -q "sema error"; then
            echo "PASS $name (sema_fail)"
            pass=$((pass + 1))
        else
            echo "FAIL $name: expected sema_fail, got status=$compile_status"
            echo "$compile_log"
            fail=$((fail + 1))
        fi
        ;;

    exit\ *)
        want_code=$(echo "$expect" | awk '{print $2}')
        if [ $compile_status -ne 0 ]; then
            echo "FAIL $name: expected exit $want_code, but wlangc failed"
            echo "$compile_log"
            fail=$((fail + 1))
            continue
        fi

        if ! gcc "$out_c" -o "$out_bin" 2>"$TMPDIR/$name.gcc.log"; then
            echo "FAIL $name: generated C failed to compile"
            cat "$TMPDIR/$name.gcc.log"
            fail=$((fail + 1))
            continue
        fi

        "$out_bin"
        got_code=$?

        if [ "$got_code" -eq "$want_code" ]; then
            echo "PASS $name (exit $got_code)"
            pass=$((pass + 1))
        else
            echo "FAIL $name: expected exit $want_code, got $got_code"
            fail=$((fail + 1))
        fi
        ;;

    *)
        echo "SKIP $name (unrecognized expect format: '$expect')"
        ;;
    esac
done

echo ""
echo "-----------------------------"
echo "passed: $pass, failed: $fail"
echo "-----------------------------"

if [ $fail -ne 0 ]; then
    exit 1
fi
