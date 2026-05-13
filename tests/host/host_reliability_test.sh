#!/usr/bin/env bash
# Mirror of wsl_reliability_test.sh but running our HOST build via
# test_state_compare. Runs the same V36E V37E 00E V37E 00E sequence
# N times and reports how often PRG=00 (OutputChannel10[11]=55265)
# emits — i.e. how often the verb completes.
set -e

cd "$(dirname "$0")"
ROM=../../build/roms/Luminary099.bin
N=${1:-5}

results=()
for i in $(seq 1 $N); do
    echo "=== run $i/$N ==="
    DUMPDIR=wsl_dumps/host_rel_$i ROM=$ROM ./test_state_compare.exe > /dev/null 2>&1 || true
    # Check core.024 (the last dump). OutputChannel10[11] is the 12th entry
    # in the OutputChannel10 array, which starts at line 2568.
    # OC10[11] line number = 2568 + 11 = 2579.
    val=$(sed -n '2579p' wsl_dumps/host_rel_$i/core.024 2>/dev/null)
    # Also check any core.NNN for the PRG=00 emission
    found_55265=0
    for f in wsl_dumps/host_rel_$i/core.*; do
        v=$(sed -n '2579p' "$f")
        if [ "$v" = "55265" ]; then found_55265=1; break; fi
    done
    if [ "$found_55265" = "1" ]; then
        echo "  -> PRG=00 SUCCESS (final OC10[11]=$val)"
        results+=("$i:OK")
    else
        echo "  -> PRG=00 NOT REACHED (final OC10[11]=$val)"
        results+=("$i:FAIL")
    fi
done

echo ""
echo "=== summary ==="
ok=0; fail=0
for r in "${results[@]}"; do
    echo "  $r"
    [[ $r == *:OK ]] && ok=$((ok+1)) || fail=$((fail+1))
done
echo ""
echo "$ok/$N host runs reached PRG=00 ; $fail failed"
