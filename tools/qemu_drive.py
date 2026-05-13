#!/usr/bin/env python3
"""Drive an espAGC QEMU run: launch qemu, wait for cold-boot to settle,
type a DSKY sequence onto UART0, capture output.

The firmware's serial_input_task (main/app_main.c) maps ASCII chars to
DSKY keycodes via channel_router_post_key. So we can drive V35E lamp
test, V37E00E etc. exactly like pressing keys on the hardware DSKY.

Usage:
  py tools/qemu_drive.py                          # default: RSET + V35E
  py tools/qemu_drive.py --seq "RV37E00E"         # V37E00E
  py tools/qemu_drive.py --seq "RV35E" --wall 30  # 30 wall-seconds

Build first:
  idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.qemu" build
"""
import argparse
import os
import re
import subprocess
import sys
import threading
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

def find_qemu() -> Path:
    base = Path(os.environ['USERPROFILE']) / '.espressif' / 'tools' / 'qemu-xtensa'
    hits = list(base.rglob('qemu-system-xtensa.exe'))
    if not hits:
        sys.exit("qemu-system-xtensa.exe not found; run `python idf_tools.py install qemu-xtensa` first")
    # Prefer the newest version dir.
    return sorted(hits, key=lambda p: p.stat().st_mtime)[-1]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--seq', default='RV35E',
                    help='ASCII sequence; V N + - E C P R K map to DSKY keys, 0-9 are digits')
    ap.add_argument('--settle', type=int, default=8,
                    help='wait this many seconds after boot before sending the sequence')
    ap.add_argument('--gap-ms', type=int, default=200,
                    help='ms between successive keypresses')
    ap.add_argument('--wall', type=int, default=25,
                    help='total wall-clock seconds to let QEMU run')
    ap.add_argument('--log', default='qemu.log', help='path to write full QEMU stdout')
    args = ap.parse_args()

    qemu = find_qemu()
    flash = REPO / 'build' / 'qemu_flash.bin'
    efuse = REPO / 'build' / 'qemu_efuse.bin'
    if not flash.exists():
        sys.exit(f"missing {flash} — run `idf.py qemu` once to generate, then quit")

    cmd = [
        str(qemu),
        '-M', 'esp32', '-m', '4M',
        '-drive', f'file={flash},if=mtd,format=raw',
        '-drive', f'file={efuse},if=none,format=raw,id=efuse',
        '-global', 'driver=nvram.esp32.efuse,property=drive,value=efuse',
        '-global', 'driver=timer.esp32.timg,property=wdt_disable,value=true',
        '-nic', 'user,model=open_eth',
        '-nographic', '-serial', 'stdio', '-no-reboot',
    ]

    print(f"==> launching qemu seq='{args.seq}' settle={args.settle}s gap={args.gap_ms}ms wall={args.wall}s")
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, bufsize=0)
    log_fp = open(args.log, 'w', encoding='utf-8', errors='replace')

    # Pattern matches AGC-relevant log lines we care about.
    pat = re.compile(r'agc|chrouter|pstub|serial:|FAILREG|Z=\d|force_dispatch|rescue_|ch01[015]|loading ROM|app:')
    hits = []

    def pump():
        for line_bytes in iter(proc.stdout.readline, b''):
            try:
                line = line_bytes.decode('utf-8', errors='replace').rstrip()
            except Exception:
                continue
            log_fp.write(line + '\n')
            log_fp.flush()
            if pat.search(line):
                hits.append(line)
                print(f"  {line}", flush=True)

    t = threading.Thread(target=pump, daemon=True)
    t.start()

    t0 = time.time()
    time.sleep(args.settle)
    print(f"==> {time.time()-t0:.1f}s elapsed; sending '{args.seq}'")
    # Use raw bytes via the binary file descriptor; subprocess.PIPE on
    # Windows otherwise wraps stdin in a text-mode CRLF-translating layer
    # that can mangle bytes and lose chars between writes. Also flush
    # AGGRESSIVELY (the pipe has internal buffering even with bufsize=0
    # for the *stderr* side; stdin needs explicit flushes per char).
    # QEMU's `-serial stdio` chardev under Windows hosts seems to drop
    # bytes on writes-without-flush followed by closes of the underlying
    # pipe. Os.write through the fd, plus an explicit fsync on the buffer,
    # is more reliable than proc.stdin.write+flush. Also send a CR after
    # each char — serial_ascii_to_keycode ignores '\r' so it's a no-op
    # input, but it pokes the chardev to deliver the previous byte.
    fd = proc.stdin.fileno()
    for c in args.seq:
        os.write(fd, c.encode('ascii'))
        os.write(fd, b'\r')   # flush poke
        time.sleep(max(args.gap_ms, 250) / 1000.0)
        print(f"   sent {c!r}")
    print(f"==> sequence sent; running until wall={args.wall}s")

    remaining = args.wall - (time.time() - t0)
    if remaining > 0:
        time.sleep(remaining)

    proc.kill()
    try: proc.wait(timeout=3)
    except subprocess.TimeoutExpired: pass

    log_fp.close()
    print()
    print(f"==> wrote {args.log} ({Path(args.log).stat().st_size} bytes), {len(hits)} relevant lines")
    print("==> last 30 relevant lines:")
    for line in hits[-30:]:
        print(f"  {line}")

if __name__ == '__main__':
    main()
