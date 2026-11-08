#!/usr/bin/env python3
"""调试：列出所有带 io_in_valid 的向量 FU 包装实例，并打印每个 in_valid 上升沿的 fuOp。"""
import sys

PATH = sys.argv[1]
cur = []
id2sig = {}   # id -> (scope, name)
scopes_with_valid = set()
with open(PATH, errors="ignore") as f:
    for line in f:
        if line.startswith("$enddefinitions"):
            break
        s = line.strip()
        if s.startswith("$scope"):
            p = s.split()
            cur.append(p[2] if len(p) > 2 else "?")
        elif s.startswith("$upscope"):
            if cur:
                cur.pop()
        elif s.startswith("$var"):
            p = s.split()
            if len(p) < 5:
                continue
            full = ".".join(cur)
            nm = p[4]
            if nm in ("io_in_valid", "io_in_bits_fuOpType") and "vfExuBlock" in full:
                id2sig[p[3]] = (full, nm)
                if nm == "io_in_valid":
                    scopes_with_valid.add(full)
print("scopes with io_in_valid:")
for s in sorted(scopes_with_valid):
    print("  ", s)

state = {}
t = None
prev = {s: "0" for s in scopes_with_valid}
with open(PATH, errors="ignore") as f:
    for line in f:
        if line.startswith("$enddefinitions"):
            break
    for line in f:
        line = line.rstrip("\n")
        if not line:
            continue
        if line[0] == "#":
            t = int(line[1:])
            continue
        if line[0] == "b":
            v, _, i = line.partition(" ")
            if i in id2sig:
                state[i] = v[1:]
            continue
        i = line[1:]
        if i not in id2sig:
            continue
        scope, nm = id2sig[i]
        state[i] = line[0]
        if nm != "io_in_valid":
            continue
        if line[0] == "1" and prev.get(scope) != "1":
            fu = None
            for k, (sc, n2) in id2sig.items():
                if sc == scope and n2 == "io_in_bits_fuOpType":
                    fu = state.get(k)
            hexv = ("0x%x" % int(fu, 2)) if fu and set(fu) <= {"0", "1"} else str(fu)
            print("  t=%-6d %-55s fuOp=%s" % (t, scope.split("vfExuBlock.")[-1], hexv))
        prev[scope] = line[0]
