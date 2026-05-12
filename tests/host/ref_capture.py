#!/usr/bin/env python3
"""
Connects to a running yaAGC over its socket, plays the role of
LM_Simulator + yaDSKY combined, sends:
  - LM_Sim ini channel writes (ch16, 30, 31, 32, 33) per lm_simulator.tcl:566-573
  - DSKY keypress sequence: RSET, V35E, then V37E00E
Logs all channel I/O with cycle context to stdout.

The point: capture what the REFERENCE Pi/Linux yaAGC does when it
receives the documented V37E00E sequence. We've never had this
ground truth before; everything else in this codebase is guessing.
"""
import socket, struct, sys, time, threading

PORT = 19797

# LM_Simulator's set_ini_values, decoded to octal (see lm_simulator.tcl:566-573):
LM_INI = [
    (0o16, 0o0, 0o00174),                         # ch16: zero, mask=lower bits
    (0o30, 0o36331, 0o77777),                     # ch30: LM healthy
    (0o31, 0o77777, 0o77777),                     # ch31: stick centered
    (0o32, 0o22777, 0o77777),                     # ch32: PRO not pressed
    (0o33, 0o57776, 0o77776),                     # ch33: radar/uplink inactive
]

# DSKY keycodes (decimal, as sent on ch015):
K = {'V': 17, 'N': 31, '+': 26, '-': 27, 'R': 18, 'E': 28, 'C': 30, 'P': 25, 'K': 25,
     '0': 16, '1': 1, '2': 2, '3': 3, '4': 4, '5': 5, '6': 6, '7': 7, '8': 8, '9': 9}

def form_packet(channel, value, ubit=0):
    """4-byte yaAGC IO packet. ubit=1 for mask-write, 0 for data-write."""
    sig0 = 0x10 if ubit else 0x00
    p0 = sig0 | (channel >> 3)
    p1 = 0x40 | ((channel << 3) & 0x38) | ((value >> 12) & 0x07)
    p2 = 0x80 | ((value >> 6) & 0x3F)
    p3 = 0xC0 | (value & 0x3F)
    return bytes([p0, p1, p2, p3])

def parse_packet(b):
    """Returns (channel, value) for a 4-byte packet."""
    if len(b) != 4: return None
    if (b[0] & 0xC0) != 0x00 or (b[1] & 0xC0) != 0x40 or \
       (b[2] & 0xC0) != 0x80 or (b[3] & 0xC0) != 0xC0:
        return None
    ch = ((b[0] & 0x3F) << 3) | ((b[1] >> 3) & 7)
    val = ((b[1] & 7) << 12) | ((b[2] & 0x3F) << 6) | (b[3] & 0x3F)
    return (ch, val)

def reader(sock, log):
    """Drain output channels from yaAGC, print human-readable."""
    buf = b''
    while True:
        try:
            data = sock.recv(4096)
        except Exception:
            return
        if not data: return
        buf += data
        while len(buf) >= 4:
            pkt, buf = buf[:4], buf[4:]
            p = parse_packet(pkt)
            if p is None:
                log.write(f"BAD PACKET {pkt.hex()}\n")
                continue
            ch, val = p
            log.write(f"  OUT ch{ch:03o} = {val:05o}\n")
            log.flush()

def send_channel(sock, ch, val, ubit=0):
    sock.sendall(form_packet(ch, val, ubit))

def send_key(sock, key):
    send_channel(sock, 0o15, K[key])

def main():
    log = sys.stdout
    log.write(f"connecting to yaAGC on port {PORT}\n")
    s = socket.create_connection(('127.0.0.1', PORT))
    s.settimeout(0.1)
    t = threading.Thread(target=reader, args=(s, log), daemon=True)
    t.start()

    # Give yaAGC a moment to send connect snapshot
    time.sleep(0.5)

    log.write("--- sending LM_Sim ini values ---\n")
    for ch, val, mask in LM_INI:
        send_channel(s, ch, mask, ubit=1)  # mask first
        send_channel(s, ch, val, ubit=0)   # then data
        log.write(f"  WR ch{ch:03o} = {val:05o} (mask {mask:05o})\n")
    log.flush()

    # Settle
    time.sleep(2.0)
    log.write("--- cold-boot settle (2s) ---\n")
    log.flush()

    # RSET
    log.write("--- RSET ---\n")
    send_key(s, 'R'); time.sleep(0.5)

    # V35E
    log.write("--- V35E (lamp test) ---\n")
    for k in 'V35E':
        send_key(s, k); time.sleep(0.1)
    time.sleep(3.0)

    # V36E (REQUEST FRESH START)
    log.write("--- V36E (FRESH START) ---\n")
    for k in 'V36E':
        send_key(s, k); time.sleep(0.1)
    time.sleep(3.0)

    # V37E00E retry loop — tutorial says "repeat a couple of times"
    for attempt in range(6):
        log.write(f"--- attempt {attempt+1}: V37E00E ---\n")
        for k in 'V37E':
            send_key(s, k); time.sleep(0.1)
        time.sleep(1.0)
        for k in '00E':
            send_key(s, k); time.sleep(0.1)
        time.sleep(4.0)

    log.write("--- DONE ---\n")
    s.close()

if __name__ == '__main__':
    main()
