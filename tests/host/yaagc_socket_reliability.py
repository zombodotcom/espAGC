#!/usr/bin/env python3
"""Reliability test for test_yaagc_socket_host.exe.

Same protocol / sequence as tests/host/windows_yaagc_test.py (Python driver
sending canonical 4-byte packets over TCP). The difference is the binary
launched: this points at OUR test_yaagc_socket_host.exe instead of upstream
yaAGC.exe, so it validates the components/yaagc_socket port end-to-end.

If this prints 5/5, the architecture port is sufficient and we move on to
QEMU + hardware. If <5/5, the bug isn't in packet plumbing alone.

Usage:
  py tests/host/yaagc_socket_reliability.py [runs=5] [SEQ]
"""
import os, socket, struct, subprocess, sys, threading, time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
EXE  = REPO / 'tests' / 'host' / 'test_yaagc_socket_host.exe'
ROM  = REPO / 'build' / 'roms' / 'Luminary099.bin'

K = {'V':17,'N':31,'+':26,'-':27,'R':18,'E':28,'C':30,'P':25,
     '0':16,'1':1,'2':2,'3':3,'4':4,'5':5,'6':6,'7':7,'8':8,'9':9}
LM_INI = [(0o16,0,0o00174),(0o30,0o36331,0o77777),(0o31,0o77777,0o77777),
          (0o32,0o22777,0o77777),(0o33,0o57776,0o77776)]

def pkt(ch, val, ub=0):
    s0 = 0x10 if ub else 0x00
    return bytes([s0|(ch>>3), 0x40|((ch<<3)&0x38)|((val>>12)&7),
                  0x80|((val>>6)&0x3F), 0xC0|(val&0x3F)])

def parse(b):
    if (b[0]&0xC0)!=0 or (b[1]&0xC0)!=0x40 or (b[2]&0xC0)!=0x80 or (b[3]&0xC0)!=0xC0:
        return None
    return (((b[0]&0x3F)<<3)|((b[1]>>3)&7),
            ((b[1]&7)<<12)|((b[2]&0x3F)<<6)|(b[3]&0x3F))

def run_once(idx, seq):
    port = 19850 + (os.getpid() % 100) + idx
    env = os.environ.copy()
    env['ROM'] = str(ROM)
    proc = subprocess.Popen([str(EXE), str(port)], env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5)
    try:
        s = socket.create_connection(('127.0.0.1', port), timeout=3.0)
    except OSError as e:
        proc.kill(); return False, f"connect: {e}"
    s.settimeout(0.2)
    found = {'55265': False}
    def reader(sock):
        buf = b''
        try:
            while True:
                d = sock.recv(4096)
                if not d: return
                buf += d
                while len(buf) >= 4:
                    p, buf = buf[:4], buf[4:]
                    r = parse(p)
                    if r and r[0] == 0o10 and r[1] == 0o55265:
                        found['55265'] = True
        except OSError:
            return
    t = threading.Thread(target=reader, args=(s,), daemon=True)
    t.start()
    time.sleep(0.5)
    for ch, val, m in LM_INI:
        s.sendall(pkt(ch, m, 1))
        s.sendall(pkt(ch, val, 0))
    time.sleep(2.0)
    for tok in seq.split():
        for k in tok:
            s.sendall(pkt(0o15, K[k]))
            time.sleep(0.1)
        time.sleep(3.0)
    time.sleep(2.0)
    try: s.close()
    except OSError: pass
    proc.kill()
    try: proc.wait(timeout=2)
    except subprocess.TimeoutExpired: pass
    return found['55265'], 'PRG=00 emitted' if found['55265'] else 'PRG=00 NOT seen'

def main():
    runs = int(sys.argv[1]) if len(sys.argv) > 1 else 5
    seq  = sys.argv[2] if len(sys.argv) > 2 else 'R V36E V37E 00E V37E 00E'
    if not EXE.exists(): sys.exit(f"missing {EXE} — run `mingw32-make test_yaagc_socket_host.exe` first")
    if not ROM.exists(): sys.exit(f"missing {ROM} — run `idf.py build` first")
    print(f"=== test_yaagc_socket_host ×{runs}  seq='{seq}' ===")
    ok = 0
    for i in range(runs):
        success, msg = run_once(i, seq)
        print(f"  run {i+1}/{runs}: {'OK' if success else 'FAIL'}  ({msg})")
        if success: ok += 1
    print()
    print(f"{ok}/{runs} runs reached PRG=00")
    return 0 if ok == runs else 1

if __name__ == '__main__':
    sys.exit(main())
