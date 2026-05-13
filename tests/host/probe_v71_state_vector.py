#!/usr/bin/env python3
"""Probe Apollo 11 V71 state-vector load via UPRUPT against a live
espAGC node. Sends:

  V37E00E                       # ensure P00 (V71 only accepted in P00)
  V71E 21E AAAAE IIIIIE ...     # 21-component contiguous block update
  V33E                          # commit

Polls /state at intervals and prints what the AGC does as the load
progresses. Useful for iterating on Apollo 11 PAD values without a
firmware flash cycle in between.

Usage:
  py tests/host/probe_v71_state_vector.py [host:port]
"""
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

def ccc(code):
    c5 = code & 0x1F
    return (c5 << 10) | ((~c5 & 0x1F) << 5) | c5

def st(retries=8):
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

def send_key_via_uprupt(sock, code):
    sock.sendall(pkt(0o173, ccc(code), 0))

def send_chars(sock, label, chars, gap_s=0.3):
    print(f'  [{label}] {chars!r}')
    for c in chars:
        send_key_via_uprupt(sock, K[c])
        time.sleep(gap_s)

def send_octal(sock, label, octal_str, gap_s=0.3):
    print(f'  [{label}] {octal_str}E')
    for d in octal_str:
        send_key_via_uprupt(sock, K[d])
        time.sleep(gap_s)
    send_key_via_uprupt(sock, K['E'])
    time.sleep(gap_s)

def main():
    s = socket.create_connection((HOST, PORT), timeout=3)
    s.settimeout(0.2)
    threading.Thread(target=reader, args=(s,), daemon=True).start()
    print(f'connected to {HOST}:{PORT}')
    print(f'pre: {st()}')

    # Step 1: get to P00 via canonical keystrokes (clean baseline)
    for c in 'RV37E00E':
        s.sendall(pkt(0o15, K[c]))
        time.sleep(0.2)
    time.sleep(2.5)
    print(f'after V37E00E (KEYRUPT): {st()}')

    # Step 2: V71E 21E
    send_chars(s, 'V71E',  'V71E')
    send_chars(s, '21E',   '21E')
    # Step 3: ECADR of UPSVFLAG = 01501
    send_octal(s, 'UPSVFLAG ECADR', '01501')
    # Step 4: identifier = 77775 (LEM lunar SOI)
    send_octal(s, 'LEM-lunar identifier', '77775')
    time.sleep(1)
    print(f'after header: {st()}')

    # Step 5: position X (DP) — Apollo 11 PDI ~1,752,640 m at perilune
    # In B-29 lunar scaling, full scale ~ 2^29 = 537 Mm. So position
    # frac = 1.752640e6 / 5.369e8 = 0.00326. DP encoding ≈ 0o00065,40000
    send_octal(s, 'X_hi', '00065')
    send_octal(s, 'X_lo', '00000')
    # Y, Z = 0 (perilune in plane)
    send_octal(s, 'Y_hi', '00000'); send_octal(s, 'Y_lo', '00000')
    send_octal(s, 'Z_hi', '00000'); send_octal(s, 'Z_lo', '00000')
    # Velocity Y = ~1,688 m/s. B-7 m/cs scaling — m/s * 100 / 2^7 = ratio
    # 1688*100/128 = 1318.75 cs-units. Very rough placeholder.
    send_octal(s, 'Vx_hi', '00000'); send_octal(s, 'Vx_lo', '00000')
    send_octal(s, 'Vy_hi', '02446'); send_octal(s, 'Vy_lo', '00000')  # 1318 dec = 0o2446
    send_octal(s, 'Vz_hi', '00000'); send_octal(s, 'Vz_lo', '00000')
    # Time = 0
    send_octal(s, 'T_hi', '00000'); send_octal(s, 'T_lo', '00000')
    time.sleep(1)
    print(f'after data: {st()}')

    # Step 6: V33E to commit
    send_chars(s, 'V33E commit', 'V33E')
    time.sleep(3)
    print(f'after V33 commit: {st()}')

    # Step 7: try V37E63E and see what P63 displays
    print('\nNow firing V37E63E + V16N63E to see if state vector took effect...')
    for c in 'V37E63E':
        s.sendall(pkt(0o15, K[c]))
        time.sleep(0.2)
    time.sleep(3)
    print(f'after V37E63E: {st()}')
    for c in 'V16N63E':
        s.sendall(pkt(0o15, K[c]))
        time.sleep(0.2)
    time.sleep(3)
    print(f'after V16N63E: {st()}')

    s.close()

if __name__ == '__main__':
    main()
