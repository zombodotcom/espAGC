# Golden traces — reference yaAGC + Luminary099 output

Captured by `ref_capture.py` connecting to a real `yaAGC` process built
from `third_party/virtualagc/yaAGC` on WSL/Linux. This is the actual
behavior of the Pi/Linux reference implementation we are trying to match.

## `ref_V35E_V37E00E.log`

Sequence:
1. Connect to yaAGC on port 19797
2. Send LM_Simulator's `set_ini_values` channel writes (ch16, 30, 31, 32, 33)
3. Wait 2s for cold boot
4. Send DSKY keypresses: RSET, V35E, V37E, 00E

**Findings (the actual ground truth, not guesses):**

- **PROG row (ch010 row 11 = 54xxx) ONLY ever fires with payload `0` (blank) or `5675` (lamp-test "88").**
  No `PROG = 00` event. The reference yaAGC + Luminary099 with this exact
  keystroke sequence on cold boot **does NOT produce PRG=00**. Quote from
  `third_party/virtualagc/Contributed/LM_Simulator/doc/tutorial.txt:40`:

      "to go into the idle loop it is necessary to change the program by
      V37E 00E. Probably that has to be repeated a couple of times until
      PROG shows 00."

  Empirically, repeats alone don't help on cold-boot Luminary without
  additional state (IMU init sequence, V36E REQUEST FRESH START, etc.).
  Anyone who claims "V37E00E produces PRG=00" without that setup is wrong
  — including past sessions of this conversation.

- **`ch010 = 60400` is PROG ALARM lamp, NOT PRG=00.**
  Row 12 (60xxx) is the flag-word for caution lights; bit `0o400` is
  PROG ALARM. So when V37E00E "fails" (because AGC isn't receptive), the
  AGC lights PROG ALARM via this channel.

- **DOWNLINK (ch034/ch035) is continuously written by the reference yaAGC.**
  Hundreds of pairs in 20s of runtime. Our build does not produce these.
  This is the DOWNRUPT-driven telemetry stream.

- **NIGHT WATCHMAN (ch035 = 01107) fires once during cold boot in the
  reference too.** This is normal Luminary cold-boot behavior, not a bug
  unique to our integration.

## Implications for our build

- Tests that assert `PRG=[0,0]` after V37E00E cold-boot are asserting
  something the REFERENCE doesn't satisfy. The assertion is wrong.
- A correct test would compare cycle-accurate output channel sequences
  against this golden trace, not assert specific DSKY state.
- Our `peripheral_stub` SHOULD produce a DOWNLINK stream like the
  reference does. The absence of ch034/ch035 writes in our build is a
  divergence.

## Reproduction

```bash
# In WSL/Linux:
cd third_party/virtualagc/yaAGC
make cc=gcc
./yaAGC --core=../../build/roms/Luminary099.bin --port=19797 --quiet --no-resume &
python3 ../../tests/host/ref_capture.py > golden/ref_V35E_V37E00E.log
```
