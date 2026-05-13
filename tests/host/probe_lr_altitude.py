#!/usr/bin/env python3
"""Validate the Option-A hypothesis: clearing ch033 bit 6 ("LR antenna in
position 1") + pulsing RNRAD (counter 046) should make V16N68's R1 show
descending altitude even without an initial state vector loaded.

Drives a live device over TCP and polls /state via HTTP to see the result.

Usage:
  py tests/host/probe_lr_altitude.py [host:port]
"""
import os, socket, struct, sys, time, urllib.request, json, threading

HOST, PORT = '192.168.1.23', 19850
if len(sys.argv) > 1 and ':' in sys.argv[1]:
    HOST, p = sys.argv[1].split(':'); PORT = int(p)

K = {'V':17,'N':31,'+':26,'-':27,'R':18,'E':28,'C':30,'P':25,
     '0':16,'1':1,'2':2,'3':3,'4':4,'5':5,'6':6,'7':7,'8':8,'9':9}

def pkt(ch, val, ub=0):
    s0 = 0x10 if ub else 0x00
    return bytes([s0|(ch>>3), 0x40|((ch<<3)&0x38)|((val>>12)&7),
                  0x80|((val>>6)&0x3F), 0xC0|(val&0x3F)])

def state():
    return json.loads(urllib.request.urlopen(f'http://{HOST}/state', timeout=2).read())

def stop_reader(sock):
    try:
        while True:
            d = sock.recv(4096)
            if not d: return
    except OSError: return

s = socket.create_connection((HOST, PORT), timeout=3)
s.settimeout(0.2)
threading.Thread(target=stop_reader, args=(s,), daemon=True).start()
print(f'state pre-test: {state()}')

# Step 1: get to a clean P63-selected state.
# RSET then V37E63E.
for c in 'R V37E63E':
    if c == ' ': continue
    s.sendall(pkt(0o15, K[c]))
    time.sleep(0.15)
time.sleep(3.0)
print(f'state after V37E63E: {state()}')

# Step 2: clear ch033 BIT6 (LR antenna in position 1).
# mask=0o40 (only BIT6 writable), value=0 (cleared) — SocketAPI flow:
#   Value &= 0o40 -> 0
#   Value |= ReadIO(033) & ~0o40 -> preserves all other bits
#   WriteIO(033, that) -> bit 6 cleared
s.sendall(pkt(0o33, 0o40, 1))   # mask packet
s.sendall(pkt(0o33, 0,    0))   # value packet
print('ch033 BIT6 cleared via canonical mask+value')
time.sleep(2.0)
print(f'state after bit6 clear: {state()}')

# Step 3: select V16N68 monitor (LR altitude / forward velocity / altitude rate).
for c in 'V16N68E':
    s.sendall(pkt(0o15, K[c]))
    time.sleep(0.15)
time.sleep(3.0)
print(f'state after V16N68E: {state()}')

# Step 4: pump RNRAD pulses simulating descent (50000 ft @ 300 ft/s).
# Each pulse = 9.38 ft. 30 ft per "step" = 3 pulses. Pump for 20 seconds.
print('pulsing RNRAD for 20s...')
for tick in range(200):     # 10 Hz x 20s
    for _ in range(3):
        # Counter increment packets: channel has BIT8 (0x80) set,
        # IncType in value (0 = PINC).
        s.sendall(pkt(0x80 | 0o46, 0, 0))   # PINC on counter 046 (RNRAD)
    time.sleep(0.1)
    if tick % 50 == 49:
        st = state()
        print(f'  t={tick/10:5.1f}s  prog={st["prog"]} verb={st["verb"]} '
              f'noun={st["noun"]} r1={st["r1"]} r2={st["r2"]} r3={st["r3"]} '
              f'ca={st["ca"]} pa={st["pa"]} oe={st["oe"]}')

print(f'final: {state()}')
s.close()
