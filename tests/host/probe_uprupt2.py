#!/usr/bin/env python3
"""Stronger UPRUPT validation: reset to a known state, then uplink
V37E00E and verify PROG transitions to 00 — proves UPRUPT keystrokes
reach the engine identically to KEYRUPT1."""
import json, socket, sys, threading, time, urllib.request

HOST, PORT = '192.168.1.23', 19850
K = {'V':17,'N':31,'+':26,'-':27,'R':18,'E':28,'C':30,'P':25,
     '0':16,'1':1,'2':2,'3':3,'4':4,'5':5,'6':6,'7':7,'8':8,'9':9}

def pkt(ch, val, ub=0):
    s0 = 0x10 if ub else 0x00
    return bytes([s0|(ch>>3), 0x40|((ch<<3)&0x38)|((val>>12)&7),
                  0x80|((val>>6)&0x3F), 0xC0|(val&0x3F)])

def ccc(code):
    c5 = code & 0x1F
    return (c5 << 10) | ((~c5 & 0x1F) << 5) | c5

def st(retries=5):
    for _ in range(retries):
        try:
            return json.loads(urllib.request.urlopen(f'http://{HOST}/state', timeout=2).read())
        except: time.sleep(0.5)
    return None

def reader(s):
    try:
        while True:
            d = s.recv(4096)
            if not d: return
    except OSError: return

s = socket.create_connection((HOST, PORT), timeout=3)
s.settimeout(0.2)
threading.Thread(target=reader, args=(s,), daemon=True).start()

# Hammer RSET via canonical keypress to escape lamp test
for _ in range(3):
    s.sendall(pkt(0o15, K['R']))
    time.sleep(0.3)
time.sleep(2)
print(f'after RSET cleanup: {st()}')

print('\nNow uplinking "V37E00E" via UPRUPT (ch0173 CCC):')
for c in 'V37E00E':
    code = K[c]
    word = ccc(code)
    print(f'  {c!r:3} code={code:02o} word=0o{word:05o}')
    s.sendall(pkt(0o173, word, 0))
    time.sleep(0.4)

time.sleep(3)
post = st()
print(f'\npost-UPRUPT: {post}')
if post and post.get('prog') == '00' and post.get('up') == 1:
    print('SUCCESS: V37E00E via UPRUPT set PROG=00 and lit UPLINK ACTY')
elif post and post.get('up') == 1:
    print(f'PARTIAL: UPRUPT delivered (up=1) but prog={post.get("prog")}')
else:
    print('FAIL: UPRUPT not registering')

s.close()
