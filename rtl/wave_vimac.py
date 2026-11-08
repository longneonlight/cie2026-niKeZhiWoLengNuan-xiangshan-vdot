#!/usr/bin/env python3
"""列出 Vimac 实例下的信号名，并跟踪 in_valid / out_valid / data / fuOp。"""
import sys

PATH = sys.argv[1]
TARGET_SUFFIX = ".exus.Vimac"
cur = []
ids = {}
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
            if full.endswith(TARGET_SUFFIX):
                ids[p[3]] = (p[4], int(p[2]))

print("signals under Vimac:", len(ids))
for nm in sorted({v[0] for v in ids.values()}):
    if any(k in nm.lower() for k in ("valid", "fuop", "data", "pdest", "sew", "lmul", "vm")):
        print("  ", nm)

want = [k for k, v in ids.items()
        if v[0] in ("io_in_valid", "io_out_valid", "io_out_bits_data_0",
                    "io_in_bits_outCtrl_fuOpType", "io_in_bits_ctrl_fuOpType",
                    "io_in_bits_fuOpType", "io_in_bits_vpu_vsew",
                    "io_in_bits_vpu_vlmul", "io_in_bits_vpu_vm")]
state = {}
t = None
prev = {"in": "0"}
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
            if i in ids:
                state[i] = v[1:]
            continue
        i = line[1:]
        if i not in ids:
            continue
        state[i] = line[0]
        nm = ids[i][0]

        def g(target):
            for k, (n, w) in ids.items():
                if n == target:
                    return state.get(k)
            return None

        if nm == "io_in_valid":
            now = line[0]
            if now == "1" and prev["in"] != "1":
                print("IN  t=%s fuOp=%s vsew=%s vlmul=%s vm=%s" % (
                    t,
                    g("io_in_bits_outCtrl_fuOpType") or g("io_in_bits_ctrl_fuOpType") or g("io_in_bits_fuOpType"),
                    g("io_in_bits_vpu_vsew"), g("io_in_bits_vpu_vlmul"), g("io_in_bits_vpu_vm")))
            prev["in"] = now
        elif nm == "io_out_valid" and line[0] == "1":
            print("OUT t=%s data=%s pdest=%s" % (t, g("io_out_bits_res_data"), g("io_out_bits_ctrl_pdest")))
