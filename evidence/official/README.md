# 官方依据归档（题面语义图 + RVV 0.7.1 原文）

> 用途：给"语义从哪来""与 RVV 0.7.1 同名指令差在哪"提供可离线核对的原文，
> 不再只靠转述。抓取日期 **2026-10-31**（VM 经代理访问外网）。

---

## 1. 题面伪代码图（本题语义的唯一依据）

| 项 | 内容 |
|---|---|
| 文件 | `official-vdot-semantics.png` |
| 来源 | `https://rv.cie.org.cn/direction?id=8` → `rule_content` 中紧跟"`vdot.vv` 指令语义（SEW = 8, VLEN = 128）"的配图 |
| 原始 URL | `https://api.rv.cie.org.cn/storage/ueditor/20260615/bc72c5ea-d0b4-4f962e3cdd5feb916af2db486c8ec14c4353eb302.png` |
| 实际格式 | 服务器返回的是 **JPEG**（扩展名写 `.png`）；已用 Pillow 无损转存为标准 PNG 便于查看 |
| 大小 / SHA-256 | 65,198 B / `47e1263b8639d4bed94886870c6899e014130767e7d710b1a1f39004a446042f` |

图中伪代码（逐字转录，与 `01-semantics.md` §0 一致）：

```python
def vdot_vv(vs1, vs2, sew=8):
    # 根据SEW设定，从两个源向量寄存器中各取8个int8数据
    result = 0
    for i in range(8):
        result += sign_extend(vs1[i]) * sign_extend(vs2[i])
    # 结果写入目标向量寄存器vd的第一个元素
    vd[0] = result
    vd[1..VLEN-1] = 0  # 其他通道清零
```

**它证明了什么**：`range(8)` 固定 8 个元素、结果写 `vd[0]`、其余通道清零——这是本题语义的原始出处，
不依赖任何二手描述。

---

## 2. RVV 0.7.1 的 `vdot.vv` 原文摘录

| 项 | 内容 |
|---|---|
| 文件 | `rvv-0.7.1-vdot.adoc` |
| 来源 | `https://raw.githubusercontent.com/riscv/riscv-v-spec/0.7.1/v-spec.adoc` |
| 规范版本头 | `Version 0.7.1-20190610-Workshop-Release` |
| 摘录范围 | `=== Vector Integer Dot-Product Instruction` 一节（至浮点点积前） |
| SHA-256 | `fc713e7935de27d2ea8d665234926a3c9cfa20787db13a767d18e82b249945e3` |

**它证明了什么**（也是本次复核纠正的一处重要表述）：

- RVV 0.7.1 **确实有** `vdot.vv` / `vdotu.vv`（支持 `vm`，属 RVV 通用 `vstart` 语义）；
- 但它是**逐元素**归约：每个 `vd[i]` 累加该元素内 `EDIV` 个子元素的乘积
  （原文示例：SEW=32、EDIV=4 时是 4 个 8-bit 乘积），结果仍是**向量**，且 `vd` 作为累加器**要读旧值**；
- 本题是**整寄存器归约**：`vs1[0..7] × vs2[0..7]` 累加成**一个 32 位标量**写 `vd[31:0]`、其余清零，
  **不读旧 `vd`**（`vdRen = F`），`vm=0` 与 `vstart≠0` 均报 illegal。

所以两者的根本差别是"**归约形状**"（逐元素 vs 整寄存器→标量），不是"少支持几个开关"。
报告 `01` §2.2、`02` §2.3、`21` §3、`23` §1.2、PPT 第 3 页都按这一口径写。

---

## 3. 复现抓取（可选）

```bash
# VM 上配置了 http(s)_proxy=192.168.157.1:7891
curl -sSL -o semantics.jpg "https://api.rv.cie.org.cn/storage/ueditor/20260615/bc72c5ea-d0b4-4f962e3cdd5feb916af2db486c8ec14c4353eb302.png"
curl -sSL -o v-spec-0.7.1.adoc "https://raw.githubusercontent.com/riscv/riscv-v-spec/0.7.1/v-spec.adoc"
```

> 注：0.7.1 的 `.adoc` 全文约 169 KB；仓库只归档与本题相关的这一节，避免把整份规范塞进提交仓库。
