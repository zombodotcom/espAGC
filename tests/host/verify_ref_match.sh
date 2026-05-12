#!/usr/bin/env bash
# verify_ref_match.sh — fails (exit 1) if our local output diverges from
# the reference golden trace in any "must-match" channel.
#
# Usage: ./verify_ref_match.sh local.log golden/ref_V35E_V37E00E.log

set -e
LOCAL="${1:-local.log}"
REF="${2:-golden/ref_V35E_V37E00E.log}"

tr -d '\r' < "$LOCAL" > /tmp/_local.log
tr -d '\r' < "$REF"   > /tmp/_ref.log

# Channels where our build MUST match the reference exactly (no extra
# values). These are the DSKY output channels — ch010 (digits/lamps),
# ch013 (lamps), ch005/006 (RCS jet enables), ch163 (DSKY HW status).
MUST_MATCH="010 013 005 006"

fails=0
for ch in $MUST_MATCH; do
    pat="OUT ch${ch} = "
    LO=$(comm -23 <(grep -F -- "$pat" /tmp/_local.log | sort -u) \
                  <(grep -F -- "$pat" /tmp/_ref.log   | sort -u))
    if [ -n "$LO" ]; then
        echo "FAIL ch${ch}: local emits values not in reference:"
        echo "$LO" | sed 's/^/    /'
        fails=$((fails+1))
    fi
done

# Channels we expect to be a subset (we may not emit everything, but
# shouldn't emit ANYTHING the reference doesn't). These are "advisory."
ADVISORY="011 012 030 031 032 033 034 035 163"
for ch in $ADVISORY; do
    pat="OUT ch${ch} = "
    LO=$(comm -23 <(grep -F -- "$pat" /tmp/_local.log | sort -u) \
                  <(grep -F -- "$pat" /tmp/_ref.log   | sort -u))
    if [ -n "$LO" ]; then
        cnt=$(echo "$LO" | wc -l)
        echo "WARN ch${ch}: local emits $cnt value(s) not in reference"
    fi
done

if [ $fails -gt 0 ]; then
    echo "VERIFICATION FAILED: $fails must-match channel(s) diverged"
    exit 1
fi

# Required-value check: reference emits ch010 = 55265 (PRG = 00) after the
# second V37E00E. Our build must do the same to call V37E00E "working".
# Detect-but-don't-fail at the channel-set level so we know exactly what's
# missing in plain English.
if grep -qF "OUT ch010 = 55265" /tmp/_ref.log; then
    if grep -qF "OUT ch010 = 55265" /tmp/_local.log; then
        echo "VERIFICATION OK: PRG=00 (ch010=55265) emitted, channel sets align"
    else
        echo "PARTIAL OK: channel-value subsets align, but PRG=00 (ch010=55265) missing"
        echo "  → V37E00E sequence does not complete to P00 in our build."
        exit 2
    fi
else
    echo "VERIFICATION OK: must-match channels (ch$MUST_MATCH) align with reference"
fi
