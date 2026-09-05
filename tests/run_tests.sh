#!/usr/bin/env bash

set -u

CIETO_EXEC="${1:-${CIETO_EXEC:-}}"
TIMEOUT_SEC="${2:-${TIMEOUT_SEC:-5}}"
CIETO_ARGS="${CIETO_ARGS:-}"
CIETO_COMPARE_ARGS="${CIETO_COMPARE_ARGS:-}"
LOG_FILE="${LOG_FILE:-test_output.log}"

if [ -z "$CIETO_EXEC" ]; then
    echo "Error: Cieto executable path was not provided."
    echo "Usage:"
    echo "  bash run_tests.sh /path/to/cieto [timeout_seconds]"
    exit 1
fi

if [ ! -f "$CIETO_EXEC" ]; then
    echo "Error: Cieto executable not found:"
    echo "  $CIETO_EXEC"
    exit 1
fi

export CIETO_EXEC

PASSED=0
FAILED=0
TIMEOUTS=0

rm -f "$LOG_FILE"

run_one() {
    local args="$1"
    local file="$2"
    local stdout_file="$3"
    local stderr_file="$4"

    timeout "$TIMEOUT_SEC" "$CIETO_EXEC" $args "$file" > "$stdout_file" 2> "$stderr_file"
    RUN_EXIT=$?
}

echo "Starting tests..."
echo "CIETO_EXEC: $CIETO_EXEC"
if [ -n "$CIETO_ARGS" ]; then
    echo "CIETO_ARGS: $CIETO_ARGS"
fi
if [ -n "$CIETO_COMPARE_ARGS" ]; then
    echo "CIETO_COMPARE_ARGS: $CIETO_COMPARE_ARGS"
fi
echo "TIMEOUT_SEC: $TIMEOUT_SEC"
echo

for file in test_*.cies; do
    if [ ! -f "$file" ]; then
        continue
    fi

    printf "Running %-35s " "$file"

    BASE_STDOUT="${LOG_FILE}.${file}.base.out"
    BASE_STDERR="${LOG_FILE}.${file}.base.err"
    CMP_STDOUT="${LOG_FILE}.${file}.cmp.out"
    CMP_STDERR="${LOG_FILE}.${file}.cmp.err"

    run_one "$CIETO_ARGS" "$file" "$BASE_STDOUT" "$BASE_STDERR"
    EXIT_CODE=$RUN_EXIT

    if [ "$EXIT_CODE" -eq 0 ]; then
        if [ -n "$CIETO_COMPARE_ARGS" ]; then
            run_one "$CIETO_COMPARE_ARGS" "$file" "$CMP_STDOUT" "$CMP_STDERR"
            CMP_EXIT_CODE=$RUN_EXIT

            if [ "$CMP_EXIT_CODE" -eq 124 ]; then
                echo "[TIMEOUT]"
                echo "Compare run timed out after ${TIMEOUT_SEC}s"
                if [ -s "$CMP_STDOUT" ]; then
                    head -80 "$CMP_STDOUT"
                fi
                if [ -s "$CMP_STDERR" ]; then
                    head -80 "$CMP_STDERR"
                fi
                TIMEOUTS=$((TIMEOUTS + 1))
                FAILED=$((FAILED + 1))
            elif [ "$EXIT_CODE" -ne "$CMP_EXIT_CODE" ] ||
                ! cmp -s "$BASE_STDOUT" "$CMP_STDOUT" ||
                ! cmp -s "$BASE_STDERR" "$CMP_STDERR"; then
                echo "[DIFF]"
                echo "Exit: base=$EXIT_CODE, compare=$CMP_EXIT_CODE"
                if [ -s "$BASE_STDOUT" ] || [ -s "$BASE_STDERR" ]; then
                    echo "Base output:"
                    cat "$BASE_STDOUT" "$BASE_STDERR" | head -120
                fi
                if [ -s "$CMP_STDOUT" ] || [ -s "$CMP_STDERR" ]; then
                    echo "Compare output:"
                    cat "$CMP_STDOUT" "$CMP_STDERR" | head -120
                fi
                FAILED=$((FAILED + 1))
            else
                echo "[PASS]"
                PASSED=$((PASSED + 1))
            fi
        else
            echo "[PASS]"
            PASSED=$((PASSED + 1))
        fi
    elif [ "$EXIT_CODE" -eq 124 ]; then
        echo "[TIMEOUT]"
        echo "Timed out after ${TIMEOUT_SEC}s"
        if [ -s "$BASE_STDOUT" ]; then
            head -80 "$BASE_STDOUT"
        fi
        if [ -s "$BASE_STDERR" ]; then
            head -80 "$BASE_STDERR"
        fi
        TIMEOUTS=$((TIMEOUTS + 1))
        FAILED=$((FAILED + 1))
    elif [ "$EXIT_CODE" -eq 139 ]; then
        echo "[SEGFAULT]"
        if [ -s "$BASE_STDOUT" ] || [ -s "$BASE_STDERR" ]; then
            cat "$BASE_STDOUT" "$BASE_STDERR" | head -120
        else
            echo "Segmentation fault, no output captured."
        fi
        FAILED=$((FAILED + 1))
    else
        echo "[FAIL]"
        if [ -s "$BASE_STDOUT" ] || [ -s "$BASE_STDERR" ]; then
            cat "$BASE_STDOUT" "$BASE_STDERR" | head -120
        else
            echo "No output captured."
        fi
        FAILED=$((FAILED + 1))
    fi

    rm -f "$BASE_STDOUT" "$BASE_STDERR" "$CMP_STDOUT" "$CMP_STDERR"
done

rm -f "$LOG_FILE"

echo
echo "========================================"
echo "Summary: $PASSED Passed, $FAILED Failed ($TIMEOUTS Timeouts)"
echo "========================================"

if [ "$FAILED" -eq 0 ]; then
    exit 0
else
    exit 1
fi
