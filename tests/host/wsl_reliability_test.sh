#!/usr/bin/env bash
# Run the canonical Pi/Linux yaAGC + Python-DSKY-driver V37E00E*2 sequence
# N times and report how many reach PRG=00 (ch010=55265). Answers the
# question: is the canonical setup deterministic, or is V37E00E racy
# even there (as tutorial.txt claims)?
#
# Run inside WSL Ubuntu-24.04:
#   bash tests/host/wsl_reliability_test.sh 5
set -e

ROOT=/mnt/c/Users/zombo/Desktop/Programming/espAGC
YAAGC=$ROOT/third_party/virtualagc/yaAGC/yaAGC
ROM=$ROOT/build/roms/Luminary099.bin
N=${1:-5}
SEQ="${2:-R V36E V37E 00E V37E 00E}"

results=()
for i in $(seq 1 $N); do
    echo "=== run $i/$N ==="
    PORT=$((19850 + RANDOM % 100))
    pkill -9 yaAGC 2>/dev/null || true
    sleep 0.3
    LOG=/tmp/wsl_run_${i}.log
    "$YAAGC" --core="$ROM" --port=$PORT --quiet --no-resume > /dev/null 2>&1 &
    AGC=$!
    sleep 1

    PORT=$PORT python3 - <<PYEOF "$SEQ" > $LOG
import os, sys, socket, time, threading
P = int(os.environ['PORT'])
LM_INI = [(0o16,0,0o00174),(0o30,0o36331,0o77777),(0o31,0o77777,0o77777),
          (0o32,0o22777,0o77777),(0o33,0o57776,0o77776)]
K = {'V':17,'N':31,'+':26,'-':27,'R':18,'E':28,'C':30,'P':25,
     '0':16,'1':1,'2':2,'3':3,'4':4,'5':5,'6':6,'7':7,'8':8,'9':9}
def pkt(ch,val,ub=0):
    s0=0x10 if ub else 0x00
    return bytes([s0|(ch>>3), 0x40|((ch<<3)&0x38)|((val>>12)&7),
                  0x80|((val>>6)&0x3F), 0xC0|(val&0x3F)])
def parse(b):
    if (b[0]&0xC0)!=0 or (b[1]&0xC0)!=0x40 or (b[2]&0xC0)!=0x80 or (b[3]&0xC0)!=0xC0:
        return None
    return (((b[0]&0x3F)<<3)|((b[1]>>3)&7),
            ((b[1]&7)<<12)|((b[2]&0x3F)<<6)|(b[3]&0x3F))
def reader(s):
    buf=b''
    try:
        while True:
            d=s.recv(4096)
            if not d: return
            buf+=d
            while len(buf)>=4:
                p,buf=buf[:4],buf[4:]
                r=parse(p)
                if r:
                    print(f"  OUT ch{r[0]:03o} = {r[1]:05o}", flush=True)
    except: return

s=socket.create_connection(('127.0.0.1',P)); s.settimeout(0.2)
threading.Thread(target=reader,args=(s,),daemon=True).start()
time.sleep(0.5)
for ch,val,m in LM_INI:
    s.sendall(pkt(ch,m,1)); s.sendall(pkt(ch,val,0))
time.sleep(2.0)
for tok in sys.argv[1].split():
    for k in tok:
        s.sendall(pkt(0o15,K[k])); time.sleep(0.1)
    time.sleep(3.0)
time.sleep(2.0)
s.close()
PYEOF
    kill $AGC 2>/dev/null || true
    wait $AGC 2>/dev/null || true

    if grep -q "ch010 = 55265" $LOG; then
        echo "  -> PRG=00 SUCCESS"
        results+=("$i:OK")
    else
        echo "  -> PRG=00 NOT REACHED"
        results+=("$i:FAIL")
    fi
done

echo ""
echo "=== summary ==="
ok=0; fail=0
for r in "${results[@]}"; do
    echo "  $r"
    [[ $r == *:OK ]] && ok=$((ok+1)) || fail=$((fail+1))
done
echo ""
echo "$ok/$N runs reached PRG=00 ; $fail failed"
