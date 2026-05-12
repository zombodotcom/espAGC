#!/usr/bin/env python3
"""Quick comparison helper for wsl_dumps/host vs wsl_dumps/ref."""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from parse_core_dump import parse

ROOT = os.path.dirname(__file__) + "/wsl_dumps"

def fmt(s, name):
    return (f"{name:14s} OC10[10]={s['OutputChannel10'][10]:05o} "
            f"OC10[11]={s['OutputChannel10'][11]:05o} "
            f"ch007={s['InputChannel'][0o7]:05o} "
            f"ch011={s['InputChannel'][0o11]:05o} "
            f"FB={s['Erasable'][0][0o4]:05o} "
            f"Z={s['Erasable'][0][0o5]:05o} "
            f"NEWJOB={s['Erasable'][0][0o67]:05o} "
            f"cyc={s['CycleCounter']:o}")

tags = sys.argv[1:] or ["host", "ref"]
for tag in tags:
    print(f"=== {tag} ===")
    for i in range(0, 25):
        p = f"{ROOT}/{tag}/core.{i:03d}"
        if not os.path.exists(p): continue
        s = parse(p)
        print(fmt(s, f"core.{i:03d}"))
    print()
