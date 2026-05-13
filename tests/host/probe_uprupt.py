#!/usr/bin/env python3
"""Probe UPRUPT (uplink) interrupt path. Send V35E via ch0173 with CCC
encoding, then poll /state separately. Tolerates HTTP flakiness."""
import json, socket, sys, threading, time, urllib.request

HOST, PORT = '192.168.1.23', 19850
if len(sys.argv) > 1 and ':' in sys.argv[1]:
    HOST, p = sys.argv[1].split(':'); PORT = int(p)

K = {'V':17,'N':31,'+':26,'-':27,'R':18,'E':28,'C':30,'P':25,
     '0':16,'1':1,'2':2,'3':3,'4':4,'5':5,'6':6,'7':7,'8':8,'9':9}

def pkt(ch, val, ub=0):
    s0 = 0x10 if ub else 0x00
    return bytes([s0|(ch>>3), 0x40|((ch<<3)&0x38)|((val>>12)&7),
                  0x80|((val>>6)&0x3F), 0xC0|(val&0x3F)])

def ccc_encode(code):
    c5 = code & 0x1F
    mid5 = (~c5) & 0x1F
    return (c5 << 10) | (mid5 << 5) | c5

def get_state(retries=5):
    for _ in range(retries):
        try:
            return json.loads(urllib.request.urlopen(f'http://{HOST}/state', timeout=2).read())
        except Exception as e:
            time.sleep(0.5)
    return None

def reader(s):
    try:
        while True:
            d = s.recv(4096)
            if not d: return
    except OSError: return

def main():
    s = socket.create_connection((HOST, PORT), timeout=3)
    s.settimeout(0.2)
    threading.Thread(target=reader, args=(s,), daemon=True).start()

    # RSET via canonical keystroke first
    s.sendall(pkt(0o15, K['R']))
    time.sleep(2.0)
    pre = get_state()
    print(f'pre:  {pre}')

    seq = 'V35E'
    print(f'Uplinking "{seq}" via ch0173 CCC:')
    for c in seq:
        code = K[c]
        word = ccc_encode(code)
        print(f'  {c!r} code={code:02o} -> word=0o{word:05o}')
        s.sendall(pkt(0o173, word, 0))
        time.sleep(0.3)

    time.sleep(3.0)
    post = get_state()
    print(f'post: {post}')
    if post and (post.get('verb') == '35' or
                 post.get('r1', '_____') != '_____' or
                 post.get('up') == 1):
        print('OK -- UPRUPT delivered V35E')
    else:
        print('FAIL -- V35E did not register via UPRUPT')

    s.close()

if __name__ == '__main__':
    main()
