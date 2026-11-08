#!/usr/bin/env python3
"""在 VCD 里定位 vdot（fuOpType=0x67）并测 FU in_valid -> out_valid 拍数。

与 wave_vdot.py 的区别：按阶段一的做法，扫描 `inner_vfExuBlock.exus*` 这一层
（FU 包装层）的 io_in_valid / io_in_bits_fuOpType，而不是内层 Vimac 模块。
"""
import re
import sys

PATH = sys.argv[1]
UNIT_RE = re.compile(r"inner_vfExuBlock\.(exus(_\d+)?)$")
WANT = ("io_in_valid", "io_in_bits_fuOpType", "io_out_valid",
        "io_out_bits_data_0", "io_out_bits_pdest", "io_in_bits_vpu_vsew",
        "io_in_bits_vpu_vlmul", "io_in_bits_vpu_vm", "io_in_bits_robIdx_value")

cur = []
sel = {}   # id -> (unit, name, width)
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
            m = UNIT_RE.search(full)
            if m and p[4] in WANT:
                sel[p[3]] = (m.group(1), p[4], int(p[2]))

units = sorted({v[0] for v in sel.values()})
print("FU wrapper units:", units, " signals:", len(sel))

state = {}
t = None
prev = {u: "0" for u in units}
events = []
outs = {}
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
            if i in sel:
                state[i] = v[1:]
            continue
        i = line[1:]
        if i not in sel:
            continue
        unit, name, width = sel[i]
        state[i] = line[0]
        if name == "io_out_valid" and line[0] == "1":
            for k, (u, nm, w) in sel.items():
                if u == unit and nm == "io_out_bits_data_0":
                    outs[(t, unit)] = state.get(k, "")
        if name != "io_in_valid":
            continue
        if line[0] == "1" and prev.get(unit) != "1":
            def g(nm):
                for k, (u, n, w) in sel.items():
                    if u == unit and n == nm:
                        return state.get(k)
                return None
            events.append((t, unit, g("io_in_bits_fuOpType"),
                           g("io_in_bits_vpu_vsew"), g("io_in_bits_vpu_vlmul"),
                           g("io_in_bits_vpu_vm")))
        prev[unit] = line[0]

print("in_valid pulses:", len(events))
hits = []
for t, unit, fu, sew, lmul, vm in events:
    fv = int(fu, 2) if fu and set(fu) <= {"0", "1"} else -1
    if fv == 0x67:
        hits.append((t, unit, sew, lmul, vm))
print("vdot (fuOp=0x67) pulses:", len(hits))
for t, unit, sew, lmul, vm in hits:
    cand = sorted(k[0] for k in outs if k[1] == unit and k[0] >= t)
    d = cand[0] if cand else None
    data = outs.get((d, unit)) if d is not None else None
    print("  in@%s out@%s latency=%s cyc data=%s vsew=%s vlmul=%s vm=%s"
          % (t, d, (d - t) if d is not None else "?", data, sew, lmul, vm))
