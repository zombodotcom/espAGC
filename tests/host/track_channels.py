#!/usr/bin/env python3
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from parse_core_dump import parse

ROOT = os.path.dirname(__file__) + "/wsl_dumps"
for tag in ("ref", "host"):
    print(f"=== {tag} ===")
    for i in (0, 1, 2, 5, 10, 15, 18, 20, 22, 24):
        p = f"{ROOT}/{tag}/core.{i:03d}"
        if not os.path.exists(p): continue
        s = parse(p)
        ic = s["InputChannel"]
        print(f"  core.{i:03d}  ch16={ic[0o16]:o}  ch30={ic[0o30]:o}  "
              f"ch31={ic[0o31]:o}  ch32={ic[0o32]:o}  ch33={ic[0o33]:o}  "
              f"ch77={ic[0o77]:o}")
