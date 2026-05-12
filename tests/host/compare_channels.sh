#!/usr/bin/env bash
# Compare distinct output-channel values between local capture and the
# golden reference trace. Reports per-channel:
#   local = #distinct values emitted by our build
#   ref   = #distinct values emitted by upstream yaAGC + Luminary099
#   diff  = values present in one but not the other
#
# Usage: ./compare_channels.sh local.log golden/ref_V35E_V37E00E.log

set -e
LOCAL="${1:-local.log}"
REF="${2:-golden/ref_V35E_V37E00E.log}"

tr -d '\r' < "$LOCAL" > /tmp/_local.log
tr -d '\r' < "$REF"   > /tmp/_ref.log

for ch in 005 006 010 011 012 013 014 030 031 032 033 034 035 163; do
    pat="OUT ch${ch} = "
    L=$(grep -F -- "$pat" /tmp/_local.log | sort -u | wc -l)
    R=$(grep -F -- "$pat" /tmp/_ref.log   | sort -u | wc -l)
    LO=$(comm -23 <(grep -F -- "$pat" /tmp/_local.log | sort -u) \
                  <(grep -F -- "$pat" /tmp/_ref.log   | sort -u) | wc -l)
    RO=$(comm -13 <(grep -F -- "$pat" /tmp/_local.log | sort -u) \
                  <(grep -F -- "$pat" /tmp/_ref.log   | sort -u) | wc -l)
    printf 'ch%s  local=%2d  ref=%2d  local-only=%2d  ref-only=%2d\n' \
        "$ch" "$L" "$R" "$LO" "$RO"
done
