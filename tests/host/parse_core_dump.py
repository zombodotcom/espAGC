#!/usr/bin/env python3
"""
Parse a yaAGC core dump (created via --dump-time=N or MakeCoreDump).

Format, as written by agc_engine_init.c:MakeCoreDump:
  - 512 InputChannel entries  (lines 1..512)
  - 8 banks of 256 Erasable cells = 2048 entries  (lines 513..2560)
  - CycleCounter                                  (line 2561)
  - ExtraCode, AllowInterrupt                     (2562..2563)
  - PendFlag, PendDelay, ExtraDelay               (2564..2566)
  - OutputChannel7                                (2567)
  - 16 OutputChannel10 entries                    (2568..2583)
  - IndexValue                                    (2584)
  - 11 InterruptRequests entries (0 unused)       (2585..2595)
  - InIsr, SubstituteInstruction                  (2596..2597)
  - DownruptTimeValid                             (2598)
  - DownruptTime                                  (2599)
  - Downlink                                      (2600)

All numbers ASCII octal. Erasable banks 0..7 each 256 cells.

Usage:
  python3 parse_core_dump.py core.019                 # summary
  python3 parse_core_dump.py core.019 --raw           # dump every field
  python3 parse_core_dump.py core.019 core.020 --diff # diff two dumps
"""
import sys

NUM_CHANNELS = 512
ERASABLE_BANKS = 8
ERASABLE_PER_BANK = 256
NUM_INTERRUPT_TYPES = 10

# Labels for well-known erasable cells (Block II AGC). Bank 0 = unswitched.
ERASABLE_NAMES = {
    (0, 0o00): "A",
    (0, 0o01): "L",
    (0, 0o02): "Q",
    (0, 0o03): "EBANK",
    (0, 0o04): "FBANK",
    (0, 0o05): "Z",
    (0, 0o06): "BBANK",
    (0, 0o07): "ARUPT",
    (0, 0o010): "LRUPT",
    (0, 0o011): "QRUPT",
    (0, 0o017): "ZRUPT",
    (0, 0o020): "BBRUPT",
    (0, 0o021): "BRUPT",
    (0, 0o022): "CYR",
    (0, 0o023): "SR",
    (0, 0o024): "CYL",
    (0, 0o025): "EDOP",
    (0, 0o026): "TIME2",
    (0, 0o027): "TIME1",
    (0, 0o030): "TIME3",
    (0, 0o031): "TIME4",
    (0, 0o032): "TIME5",
    (0, 0o033): "TIME6",
    (0, 0o067): "NEWJOB",
    (0, 0o164): "LOC",       # active job location
    (0, 0o167): "PRIORITY",  # active job priority
    (0, 0o0375): "FAILREG0",
    (0, 0o0376): "FAILREG1",
    (0, 0o0377): "FAILREG2",
}
# Slot 0..7 layout in EXEC tables (per Luminary099):
#   PRIORITY at 0154+slot*12, LOC at 0150+slot*12, BBANK at 0151+slot*12,
#   MPAC0..6 at 0152..0157+slot*12 etc. The classic layout is:
#     LOC table at LOCCTR  (0154..0163)  -- one per slot
#     PRIORITY at PRIO (0140..0147)
#   Skip detailed mapping for now; just dump bank-0 0140..0177 range raw.

CHANNEL_NAMES = {
    0o05: "JET5 (RCS)",
    0o06: "JET6 (RCS)",
    0o07: "SuperBank/Inhibit",
    0o010: "DSKY OUT",
    0o011: "DSKY LAMPS",
    0o012: "IMU/PIPA",
    0o013: "ALT/RR",
    0o014: "GYRO",
    0o015: "MAIN KEYS",
    0o016: "NAV KEYS",
    0o030: "INPUTS (CM/LM ch30)",
    0o031: "STICK",
    0o032: "PRO",
    0o033: "RADAR/UPLINK",
    0o077: "ALARMS",
}


def parse(path):
    with open(path) as f:
        lines = [ln.strip() for ln in f if ln.strip()]

    idx = 0

    def take(n=1):
        nonlocal idx
        if n == 1:
            v = int(lines[idx], 8)
            idx += 1
            return v
        out = [int(lines[idx + i], 8) for i in range(n)]
        idx += n
        return out

    state = {}
    state["InputChannel"] = take(NUM_CHANNELS)
    state["Erasable"] = [
        take(ERASABLE_PER_BANK) for _ in range(ERASABLE_BANKS)
    ]
    state["CycleCounter"] = take()
    state["ExtraCode"] = take()
    state["AllowInterrupt"] = take()
    state["PendFlag"] = take()
    state["PendDelay"] = take()
    state["ExtraDelay"] = take()
    state["OutputChannel7"] = take()
    state["OutputChannel10"] = take(16)
    state["IndexValue"] = take()
    state["InterruptRequests"] = take(1 + NUM_INTERRUPT_TYPES)
    state["InIsr"] = take()
    state["SubstituteInstruction"] = take()
    state["DownruptTimeValid"] = take()
    state["DownruptTime"] = take()
    state["Downlink"] = take()
    state["_remaining_lines"] = len(lines) - idx
    return state


def fmt_oct(n, width=5):
    return f"{n:0{width}o}"


def summary(state, path):
    print(f"=== {path} ===")
    print(f"CycleCounter    = {state['CycleCounter']:o} oct  "
          f"({state['CycleCounter']:,} dec)")
    print(f"ExtraCode={state['ExtraCode']}  "
          f"AllowInterrupt={state['AllowInterrupt']}  "
          f"PendFlag={state['PendFlag']}  "
          f"PendDelay={state['PendDelay']}  "
          f"ExtraDelay={state['ExtraDelay']}  "
          f"InIsr={state['InIsr']}")
    print(f"OutputChannel7  = {fmt_oct(state['OutputChannel7'])}")
    print(f"IndexValue      = {fmt_oct(state['IndexValue'])}")
    print(f"InterruptRequests = "
          f"{[fmt_oct(x,2) for x in state['InterruptRequests']]}")
    print(f"SubstituteInstruction = {fmt_oct(state['SubstituteInstruction'])}")
    print(f"DownruptTime    = {state['DownruptTime']:o}  "
          f"Valid={state['DownruptTimeValid']}")
    print(f"Downlink        = {fmt_oct(state['Downlink'])}")
    print()

    print("--- Named registers (bank 0) ---")
    for (b, off), nm in sorted(ERASABLE_NAMES.items()):
        v = state["Erasable"][b][off]
        print(f"  {nm:<10}  E[{b}][{off:04o}] = {fmt_oct(v)}")
    print()

    print("--- Output channels of interest ---")
    for ch, nm in CHANNEL_NAMES.items():
        print(f"  ch{ch:03o}  {nm:<20} = {fmt_oct(state['InputChannel'][ch])}")
    print()

    print("--- DSPTAB (Erasable[2][023..036]) ---")
    for off in range(0o23, 0o37):
        v = state["Erasable"][2][off]
        print(f"  E[2][{off:04o}] = {fmt_oct(v)}")
    print()

    print("--- Bank 0 exec table region (0140..0177) ---")
    for off in range(0o140, 0o200, 8):
        row = " ".join(fmt_oct(state["Erasable"][0][off + i])
                       for i in range(8))
        print(f"  E[0][{off:04o}..]  {row}")
    print()


def raw(state, path):
    print(f"=== {path} (raw) ===")
    for i, v in enumerate(state["InputChannel"]):
        if v != 0:
            print(f"  ch{i:03o} = {fmt_oct(v)}")
    for b in range(ERASABLE_BANKS):
        for off in range(ERASABLE_PER_BANK):
            v = state["Erasable"][b][off]
            if v != 0:
                print(f"  E[{b}][{off:04o}] = {fmt_oct(v)}")


def diff(a, b, pa, pb):
    print(f"=== diff {pa} -> {pb} ===")
    print(f"  CycleCounter  {a['CycleCounter']:o} -> {b['CycleCounter']:o}  "
          f"(Δ {b['CycleCounter']-a['CycleCounter']})")
    diffs = []
    for ch in range(NUM_CHANNELS):
        if a["InputChannel"][ch] != b["InputChannel"][ch]:
            diffs.append(("CH", ch, a["InputChannel"][ch],
                         b["InputChannel"][ch]))
    for bank in range(ERASABLE_BANKS):
        for off in range(ERASABLE_PER_BANK):
            if a["Erasable"][bank][off] != b["Erasable"][bank][off]:
                diffs.append(("E", (bank, off),
                             a["Erasable"][bank][off],
                             b["Erasable"][bank][off]))
    for fld in ("ExtraCode", "AllowInterrupt", "PendFlag", "PendDelay",
                "ExtraDelay", "OutputChannel7", "IndexValue", "InIsr",
                "SubstituteInstruction", "DownruptTime", "Downlink"):
        if a[fld] != b[fld]:
            diffs.append(("F", fld, a[fld], b[fld]))
    if a["OutputChannel10"] != b["OutputChannel10"]:
        for i in range(16):
            if a["OutputChannel10"][i] != b["OutputChannel10"][i]:
                diffs.append(("OC10", i,
                              a["OutputChannel10"][i],
                              b["OutputChannel10"][i]))
    if a["InterruptRequests"] != b["InterruptRequests"]:
        for i in range(1 + NUM_INTERRUPT_TYPES):
            if a["InterruptRequests"][i] != b["InterruptRequests"][i]:
                diffs.append(("IR", i,
                              a["InterruptRequests"][i],
                              b["InterruptRequests"][i]))
    print(f"  {len(diffs)} cells differ\n")
    for kind, key, va, vb in diffs[:200]:
        if kind == "CH":
            nm = CHANNEL_NAMES.get(key, "")
            print(f"  ch{key:03o} {nm:<20} {fmt_oct(va)} -> {fmt_oct(vb)}")
        elif kind == "E":
            bank, off = key
            nm = ERASABLE_NAMES.get((bank, off), "")
            print(f"  E[{bank}][{off:04o}] {nm:<10} "
                  f"{fmt_oct(va)} -> {fmt_oct(vb)}")
        elif kind == "OC10":
            print(f"  OC10[{key}]  {fmt_oct(va)} -> {fmt_oct(vb)}")
        elif kind == "IR":
            print(f"  IR[{key}]    {fmt_oct(va,2)} -> {fmt_oct(vb,2)}")
        else:
            print(f"  {key}  {va} -> {vb}")
    if len(diffs) > 200:
        print(f"  ... {len(diffs)-200} more")


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        sys.exit(2)
    do_raw = "--raw" in args
    do_diff = "--diff" in args
    files = [a for a in args if not a.startswith("--")]
    if do_diff:
        if len(files) != 2:
            sys.exit("--diff requires two dump files")
        a = parse(files[0]); b = parse(files[1])
        diff(a, b, files[0], files[1])
        return
    for f in files:
        st = parse(f)
        if do_raw:
            raw(st, f)
        else:
            summary(st, f)


if __name__ == "__main__":
    main()
