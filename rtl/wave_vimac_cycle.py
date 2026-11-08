#!/usr/bin/env python3
"""按拍打印 Vimac 的 in/out valid 与 res_data（默认 8312..8326）。"""
import sys

PATH = sys.argv[1]
LO = int(sys.argv[2]) if len(sys.argv) > 2 else 8312
HI = int(sys.argv[3]) if len(sys.argv) > 3 else 8326
SUF = ".exus.Vimac"
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
            if ".".join(cur).endswith(SUF) and p[4] in (
                    "io_in_valid", "io_out_valid", "io_out_bits_res_data",
                    "io_out_bits_ctrl_pdest", "io_in_bits_ctrl_fuOpType"):
                ids[p[3]] = p[4]

state = {}
t = None
rows = {}
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
            if LO <= t <= HI:
                rows[t] = dict(state)
            continue
        if line[0] == "b":
            v, _, i = line.partition(" ")
            if i in ids:
                state[i] = v[1:]
            continue
        i = line[1:]
        if i in ids:
            state[i] = line[0]

def val(row, name):
    for k, n in ids.items():
        if n == name:
            return row.get(k)
    return None

for t in sorted(rows):
    row = rows[t]
    data = val(row, "io_out_bits_res_data")
    if data and set(data) <= {"0", "1"} and len(data) >= 32:
        low = int(data[-32:], 2)
        data_s = "0x%08x" % low
    else:
        data_s = str(data)
    print("t=%d in=%s out=%s data=%s pdest=%s fuOp=%s" % (
        t, val(row, "io_in_valid"), val(row, "io_out_valid"), data_s,
        val(row, "io_out_bits_ctrl_pdest"), val(row, "io_in_bits_ctrl_fuOpType")))
