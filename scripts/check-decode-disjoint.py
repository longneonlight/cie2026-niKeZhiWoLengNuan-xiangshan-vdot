#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check-decode-disjoint.py —— 译码表"两两互斥"的穷举核对

背景：本作品往香山的 `VecDecoder.decode_table` 里加了一条 `VDOT_VV`。
"加一条指令不会改变任何既有指令的行为"这个结论，依赖两件事：
  (1) 新 BitPat 与表里其它 466 条**没有任何重叠**（否则谁先匹配谁赢，行为会变）；
  (2) 表里原有的 466 条彼此本来就互斥（香山的 ListLookup 是优先级 mux，重叠就意味着
      某条指令的实际匹配结果取决于它排在第几个）。

这个脚本把两件事都算一遍，输出可复核的计数。

用法：
  python3 scripts/check-decode-disjoint.py                 # 自动找 $XIANGSHAN_DIR / $NOOP_HOME / $XS_PROJECT_ROOT
  python3 scripts/check-decode-disjoint.py --xs /path/to/XiangShan
  python3 scripts/check-decode-disjoint.py --xs ... --verbose

退出码：0 = 全部互斥；1 = 发现重叠（会打印前若干对）；2 = 找不到源码/解析失败

依赖：只读源码文本，不需要 mill、不需要编译。
"""
import argparse
import os
import re
import sys

# VecDecoder.scala 的 import 顺序决定同名符号的优先级（后面覆盖前面）
SOURCES = [
    ("rocket", "rocket-chip/src/main/scala/rocket/Instructions.scala"),
    ("Zvbb",   "src/main/scala/xiangshan/backend/decode/Instructions.scala"),
    ("VecDecoderLocal", "src/main/scala/xiangshan/backend/decode/VecDecoder.scala"),
]

DEF_RE = re.compile(r'def\s+([A-Za-z0-9_]+)\s*=\s*BitPat\("([bB][01?]+)"\)')
KEY_RE = re.compile(r'^\s*([A-Z][A-Z0-9_]*)\s*->')


def load_patterns(xs):
    """返回 {name: (pattern, source)}，按 SOURCES 顺序后者覆盖前者。"""
    table = {}
    seen_in = {}
    for tag, rel in SOURCES:
        path = os.path.join(xs, rel)
        if not os.path.isfile(path):
            print(f"  [warn] 缺少 {path}（跳过 {tag}）")
            continue
        n = 0
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                m = DEF_RE.search(line)
                if not m:
                    continue
                name, pat = m.group(1), m.group(2)[1:].lower()
                if len(pat) != 32:
                    print(f"  [warn] {tag}:{name} 位宽 {len(pat)} != 32，忽略")
                    continue
                seen_in.setdefault(name, []).append(tag)
                table[name] = (pat, tag)
                n += 1
        print(f"  {tag:16s} {n:4d} 条  ({rel})")
    dup = {k: v for k, v in seen_in.items() if len(v) > 1}
    if dup:
        print(f"  [info] 同名符号 {len(dup)} 个（按 import 顺序取后者）："
              + ", ".join(sorted(dup)[:8]) + (" ..." if len(dup) > 8 else ""))
    return table


def load_keys(xs):
    path = os.path.join(xs, "src/main/scala/xiangshan/backend/decode/VecDecoder.scala")
    keys = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = KEY_RE.match(line)
            if m:
                keys.append(m.group(1))
    return keys


def overlap(p, q):
    for a, b in zip(p, q):
        if (a == '0' and b == '1') or (a == '1' and b == '0'):
            return False
    return True


def main():
    ap = argparse.ArgumentParser()
    env = os.environ
    guess = (env.get("XIANGSHAN_DIR") or env.get("NOOP_HOME")
             or (os.path.join(env["XS_PROJECT_ROOT"], "XiangShan") if env.get("XS_PROJECT_ROOT") else None)
             or os.path.expanduser("~/XiangShan"))
    ap.add_argument("--xs", default=guess)
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--max-report", type=int, default=15)
    args = ap.parse_args()

    xs = args.xs
    if not os.path.isdir(xs):
        print(f"找不到 XiangShan 源码树：{xs}")
        return 2
    print(f"=== check-decode-disjoint  xs={xs} ===")

    print("\n[1] 收集 BitPat 定义")
    pats = load_patterns(xs)
    print(f"  合计可用模式: {len(pats)}")

    print("\n[2] 读取 VecDecoder.decode_table 的表项")
    keys = load_keys(xs)
    print(f"  表项数: {len(keys)}  去重后: {len(set(keys))}")

    print("\n[3] 解析表项 → 位模式")
    unresolved, resolved = [], []
    for k in keys:
        if k in pats:
            resolved.append((k, pats[k][0], pats[k][1]))
        else:
            unresolved.append(k)
    print(f"  可解析: {len(resolved)}   未解析: {len(unresolved)}")
    if unresolved:
        print("  未解析样例: " + ", ".join(unresolved[:12]))
        print("  （未解析通常是表项用了别处定义的复合模式，需要人工补）")
    if not resolved:
        return 2

    print("\n[4] 两两互斥穷举")
    n = len(resolved)
    pairs = n * (n - 1) // 2
    print(f"  检查 {n} 条、{pairs} 对、每条 32 位")
    conflicts = []
    for i in range(n):
        ki, pi, si = resolved[i]
        for j in range(i + 1, n):
            kj, pj, sj = resolved[j]
            if overlap(pi, pj):
                conflicts.append((ki, kj, pi, pj, si, sj))
    print(f"  重叠对数: {len(conflicts)}")

    # VDOT_VV 单列（本作品新增的那条）
    vdot = [(k, p) for k, p, s in resolved if k == "VDOT_VV"]
    if vdot:
        k0, p0 = vdot[0]
        bad = [c for c in conflicts if k0 in (c[0], c[1])]
        print(f"\n[5] {k0} 专项：与 {n - 1} 条比对，重叠 {len(bad)} 处")
        print(f"  模式 = {p0}")

    if conflicts:
        print("\n[!] 发现重叠（前 %d 对）：" % min(len(conflicts), args.max_report))
        for c in conflicts[: args.max_report]:
            print(f"  {c[0]:20s} <-> {c[1]:20s}")
            print(f"     {c[2]}  [{c[4]}]")
            print(f"     {c[3]}  [{c[5]}]")
        print("\nRESULT: FAIL")
        return 1

    print("\nRESULT: PASS（%d 条两两互斥，0 处重叠）" % n)
    return 0


if __name__ == "__main__":
    sys.exit(main())
