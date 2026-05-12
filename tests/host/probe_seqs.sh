#!/usr/bin/env bash
# Probe a bunch of keystroke sequences against the reference yaAGC,
# looking for one that produces PRG=00 (= ch010 row 11 with payload
# encoding "00" digit). Row 11 = octal 54xxx; payload for "00" depends
# on the 5-bit-relay digit code.
#
# We just print every ch010 row-11 (PROG) value emitted per sequence
# and let the human inspect which one shows a non-blank, non-lamp-test
# value (54000 = blank, 55675 = "88" lamp test).
#
# Run after yaAGC is built and Luminary099.bin is present.
set -e

ROM=/mnt/c/Users/zombo/Desktop/Programming/espAGC/build/roms/Luminary099.bin
YAAGC=/mnt/c/Users/zombo/Desktop/Programming/espAGC/third_party/virtualagc/yaAGC/yaAGC
CAPTURE=/mnt/c/Users/zombo/Desktop/Programming/espAGC/tests/host/ref_capture.py

# Each sequence is tokens separated by spaces. Each token is sent as
# a chain of keypresses with delays.
SEQS=(
    "R V35E V37E 00E"             # baseline (we know this doesn't work)
    "R V36E V37E 00E"             # FRESH_START first
    "R V36E V35E V37E 00E"        # FRESH then lamp test
    "R V35E V36E V37E 00E"        # lamp test then FRESH
    "R V35E V37E 00E V37E 00E"    # double V37E00E
    "R V36E V37E 00E V37E 00E"
    "R V35E R V37E 00E"           # RSET between
    "R R R V35E V37E 00E"         # multiple RSETs
)

for seq in "${SEQS[@]}"; do
    pkill -9 -f yaAGC.bin 2>/dev/null || pkill -9 yaAGC 2>/dev/null || true
    sleep 0.5
    "$YAAGC" --core="$ROM" --port=19797 --quiet --no-resume >/tmp/yaagc.out 2>&1 &
    AGCPID=$!
    sleep 1
    python3 "$CAPTURE" "$seq" > /tmp/probe.out 2>&1 &
    CAPPID=$!
    sleep 30
    kill $CAPPID $AGCPID 2>/dev/null
    wait 2>/dev/null

    # Look at PROG row (ch010 row 11 = 54xxx) values
    prog_vals=$(grep 'OUT ch010 = 5[4567]' /tmp/probe.out | awk '{print $NF}' | sort -u | tr '\n' ' ')
    echo "seq='$seq'"
    echo "  ch010 row-11 (PROG) values: $prog_vals"
done
