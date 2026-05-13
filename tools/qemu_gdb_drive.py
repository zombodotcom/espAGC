#!/usr/bin/env python3
"""End-to-end QEMU + GDB driver — reliable on Windows where stdio is flaky.

Launches QEMU with the gdb server on :1234, then runs gdb in batch mode
with an inline Python script that drives the engine: continue, sleep,
interrupt, inject DSKY keycodes via `call channel_router_post_key(N)`,
continue more, dump state, quit.

Usage:
  py tools/qemu_gdb_drive.py                          # default: RV35E
  py tools/qemu_gdb_drive.py --seq RV37E00EV37E00E    # V37E00E*2

Pre-req: a build that exposes channel_router_post_key (always) and
serial_input_task (for hardware co-deploy):
  idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.qemu" build qemu
"""
import argparse
import os
import re
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

KEYMAP = {
    'V': 17, 'N': 31, '+': 26, '-': 27,
    'E': 28, 'C': 30, 'P': 63, 'R': 18, 'K': 25,
    '0': 16, '1': 1, '2': 2, '3': 3, '4': 4,
    '5': 5,  '6': 6, '7': 7, '8': 8, '9': 9,
}

def find_qemu():
    base = Path(os.environ['USERPROFILE']) / '.espressif' / 'tools' / 'qemu-xtensa'
    hits = sorted(base.rglob('qemu-system-xtensa.exe'), key=lambda p: p.stat().st_mtime)
    if not hits: sys.exit("qemu-system-xtensa.exe not installed")
    return hits[-1]

def find_gdb():
    base = Path(os.environ['USERPROFILE']) / '.espressif' / 'tools' / 'xtensa-esp-elf-gdb'
    hits = sorted(base.rglob('xtensa-esp32-elf-gdb.exe'), key=lambda p: p.stat().st_mtime)
    if not hits: sys.exit("xtensa-esp32-elf-gdb not installed")
    return hits[-1]

def wait_for_port(port, host='127.0.0.1', timeout=10.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection((host, port), timeout=0.5): return True
        except OSError: time.sleep(0.2)
    return False

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--seq', default='RV35E')
    ap.add_argument('--settle', type=float, default=5.0)
    ap.add_argument('--post-wall', type=float, default=10.0)
    ap.add_argument('--key-gap', type=float, default=0.5)
    ap.add_argument('--log', default='qemu_gdb.log')
    ap.add_argument('--gdb-port', type=int, default=1234)
    args = ap.parse_args()

    flash = REPO / 'build' / 'qemu_flash.bin'
    efuse = REPO / 'build' / 'qemu_efuse.bin'
    elf   = REPO / 'build' / 'espAGC.elf'
    for p in (flash, efuse, elf):
        if not p.exists(): sys.exit(f"missing {p} — run `idf.py qemu` once to generate")

    qemu = find_qemu()
    gdb = find_gdb()
    print(f"==> qemu: {qemu}")
    print(f"==> gdb:  {gdb}")

    qemu_cmd = [
        str(qemu),
        '-M', 'esp32', '-m', '4M',
        '-drive', f'file={flash},if=mtd,format=raw',
        '-drive', f'file={efuse},if=none,format=raw,id=efuse',
        '-global', 'driver=nvram.esp32.efuse,property=drive,value=efuse',
        '-global', 'driver=timer.esp32.timg,property=wdt_disable,value=true',
        '-nic', 'user,model=open_eth',
        '-nographic',
        '-serial', 'stdio',
        '-gdb', f'tcp::{args.gdb_port}',
        '-S',
        '-no-reboot',
    ]

    log_fp = open(args.log, 'w', encoding='utf-8', errors='replace')
    hits = []
    pat = re.compile(r'agc|chrouter|pstub|serial:|FAILREG|Z=\d|force_dispatch|rescue_|ch01[015]|loading ROM|app:|GDB-INJECT')

    print(f"==> launching qemu with -gdb tcp::{args.gdb_port} -S")
    qproc = subprocess.Popen(qemu_cmd, stdin=subprocess.DEVNULL,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              bufsize=0)

    def pump():
        for line_bytes in iter(qproc.stdout.readline, b''):
            line = line_bytes.decode('utf-8', errors='replace').rstrip()
            log_fp.write(line + '\n'); log_fp.flush()
            if pat.search(line):
                hits.append(line)
                print(f"  QEMU| {line}", flush=True)
    threading.Thread(target=pump, daemon=True).start()

    if not wait_for_port(args.gdb_port):
        qproc.kill(); sys.exit("qemu gdb port never opened")
    print(f"==> qemu gdb server listening on :{args.gdb_port}")

    # Build the inline gdb Python script. We use gdb's embedded Python so
    # we can mix `gdb.execute(...)` with `time.sleep()` — that's the
    # cleanest way to coordinate "continue for N seconds, then call X".
    keycodes = [KEYMAP[c.upper()] for c in args.seq if c.upper() in KEYMAP]
    # Synchronous gdb mode. `gdb.execute('continue')` blocks until the
    # inferior stops; we arrange the stop by sending an interrupt from a
    # helper thread after a delay. This is the canonical pattern for
    # scripted gdb because it guarantees the call_function() target is
    # stopped before we issue `call`.
    py_script = f"""
import gdb, threading, time, sys

def delayed_interrupt(delay):
    time.sleep(delay)
    try:
        gdb.post_event(lambda: gdb.execute('interrupt'))
    except Exception as e:
        print(f'GDB-INJECT: interrupt-event failed: {{e}}'); sys.stdout.flush()

def run_for(secs):
    threading.Thread(target=delayed_interrupt, args=(secs,), daemon=True).start()
    try:
        gdb.execute('continue')  # blocks until interrupt fires
    except gdb.error as e:
        print(f'GDB-INJECT: continue raised: {{e}}'); sys.stdout.flush()

def run():
    gdb.execute('target remote :{args.gdb_port}')
    gdb.execute('set pagination off')
    print('GDB-INJECT: attached, booting for {args.settle}s')
    sys.stdout.flush()
    run_for({args.settle})
    print('GDB-INJECT: stopped at PC={{}}; injecting keys'.format(gdb.parse_and_eval('$pc')))
    sys.stdout.flush()
    for code in {keycodes}:
        try:
            gdb.execute(f'call (void) channel_router_post_key({{code}})')
            print(f'GDB-INJECT: posted keycode {{code}}'); sys.stdout.flush()
        except gdb.error as e:
            print(f'GDB-INJECT: call({{code}}) failed: {{e}}'); sys.stdout.flush()
    print('GDB-INJECT: all keys posted; running for {args.post_wall}s')
    sys.stdout.flush()
    run_for({args.post_wall})
    z = int(gdb.parse_and_eval('g_state.Erasable[0][5]&077777'))
    fb = int(gdb.parse_and_eval('g_state.Erasable[0][4]&077777'))
    prio = int(gdb.parse_and_eval('g_state.Erasable[0][0167]&077777'))
    loc = int(gdb.parse_and_eval('g_state.Erasable[0][0164]&077777'))
    cyc = int(gdb.parse_and_eval('g_state.CycleCounter'))
    print(f'GDB-INJECT: FINAL Z={{z:o}} FB={{fb:o}} prio={{prio:o}} LOC={{loc:o}} cyc={{cyc:o}}')
    sys.stdout.flush()
    gdb.execute('detach')
    gdb.execute('quit')

run()
"""
    py_path = REPO / 'build' / 'qemu_gdb_inject.py'
    py_path.write_text(py_script, encoding='utf-8')

    # Pass the ELF as a positional arg — gdb's canonical way to load
    # symbols before any -ex / -x commands run. Avoids quoting issues
    # with `-ex 'file <path>'` on Windows paths.
    # Note: we explicitly DON'T enable target-async because synchronous
    # `gdb.execute('continue')` + a threaded interrupt is more reliable
    # than async + wait_for_stop polling across gdb's Python bindings.
    gdb_cmd = [
        str(gdb), '-q', '-batch',
        str(elf),
        '-ex', 'set confirm off',
        '-x', str(py_path),
    ]
    print(f"==> launching gdb -batch with inline Python driver")
    gproc = subprocess.Popen(gdb_cmd, stdin=subprocess.DEVNULL,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              bufsize=0, text=True)

    def gdb_pump():
        for line in iter(gproc.stdout.readline, ''):
            line = line.rstrip()
            log_fp.write('[gdb] ' + line + '\n'); log_fp.flush()
            if line.startswith('GDB-INJECT') or 'error' in line.lower():
                hits.append(line)
                print(f"  GDB | {line}", flush=True)
    threading.Thread(target=gdb_pump, daemon=True).start()

    try:
        gproc.wait(timeout=args.settle + args.post_wall + 3 * len(keycodes) + 30)
    except subprocess.TimeoutExpired:
        print("==> gdb timeout — killing")
        gproc.kill()

    # Give QEMU a moment to flush, then kill.
    time.sleep(1.0)
    qproc.kill()
    try: qproc.wait(timeout=3)
    except subprocess.TimeoutExpired: pass

    log_fp.close()
    print()
    print(f"==> {len(hits)} relevant lines captured to {args.log}")
    print("==> last 30 relevant lines:")
    for line in hits[-30:]: print(f"  {line}")

if __name__ == '__main__':
    main()
