#!/usr/bin/env python3
"""Drive a live ESP32 espAGC node over TCP using the canonical 4-byte
protocol. Mirror of yaagc_socket_reliability.py / windows_yaagc_test.py
but no subprocess: the device is presumed already flashed and running
with CONFIG_AGC_YAAGC_SOCKET=y.

Usage:
  py tests/host/hardware_reliability_test.py [host:port] [runs=1] [SEQ]

The device only has one engine instance, so back-to-back runs reuse
the same state. Default to 1 run (= a single end-to-end test against
the device's existing engine session). For a clean run, power-cycle
the dongle between attempts.
"""
import os, socket, struct, sys, threading, time

HOST, PORT = '192.168.1.23', 19850
if len(sys.argv) > 1 and ':' in sys.argv[1]:
    HOST, p = sys.argv[1].split(':')
    PORT = int(p)
RUNS = int(sys.argv[2]) if len(sys.argv) > 2 else 1
SEQ  = sys.argv[3] if len(sys.argv) > 3 else 'R V36E V37E 00E V37E 00E'

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

def run_once(idx):
    print(f"=== run {idx+1}/{RUNS}: connect to {HOST}:{PORT} ===")
    s = socket.create_connection((HOST, PORT), timeout=3.0)
    s.settimeout(0.2)
    found_55265 = [False]
    saw_outputs = [0]
    seen_progs  = set()
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
                    if r is None: continue
                    saw_outputs[0] += 1
                    ch, val = r
                    if ch == 0o10:
                        row = (val >> 11) & 0xF
                        pay = val & 0x7FF
                        if row == 11:
                            seen_progs.add(f"{pay:04o}")
                        if val == 0o55265:
                            found_55265[0] = True
        except OSError:
            return
    threading.Thread(target=reader, args=(s,), daemon=True).start()

    # LM_INI is already injected by peripheral_stub on the device; sending
    # it again is harmless because Luminary's ch030..033 handling is
    # latched. We still send to validate the mask+value round-trip works
    # over the wire.
    time.sleep(0.5)
    for ch, val, m in LM_INI:
        s.sendall(pkt(ch, m, 1))
        s.sendall(pkt(ch, val, 0))
    time.sleep(2.0)
    for tok in SEQ.split():
        for k in tok:
            s.sendall(pkt(0o15, K[k]))
            time.sleep(0.1)
        time.sleep(3.0)
    time.sleep(2.0)
    try: s.close()
    except OSError: pass

    print(f"  outputs received    : {saw_outputs[0]}")
    print(f"  ch010 row=11 payloads: {sorted(seen_progs)}")
    print(f"  PRG=00 (ch010=55265): {'YES' if found_55265[0] else 'no'}")
    return found_55265[0]

def main():
    print(f"target: {HOST}:{PORT}   seq='{SEQ}'   runs={RUNS}")
    ok = 0
    for i in range(RUNS):
        if run_once(i): ok += 1
    print()
    print(f"{ok}/{RUNS} runs reached PRG=00")
    return 0 if ok == RUNS else 1

if __name__ == '__main__':
    sys.exit(main())
