#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Markdown -> DOCX（技术报告版式），供 CIE-2026 vdot 提交文档使用。

用法：
    python md2docx.py 22-report-stage1.md --stage 阶段一 --score "阶段一 15 分" \
        -o dist/stage1.docx

设计约束：只支持本项目文档用到的 Markdown 子集——
标题(#..####)、段落、项目符号/编号列表、表格、围栏代码块、引用块、
分隔线、图片、**粗体**（内部可嵌 `行内代码`）、`行内代码`、[链接](url)。
之所以不引入第三方 Markdown 库，是因为构建机无网络、且版式需要逐处控制。

生成后用任意 docx→pdf 工具（Word / WPS / LibreOffice）导出 PDF。
"""

import argparse
import os
import re
import sys

from docx import Document
from docx.enum.table import WD_TABLE_ALIGNMENT
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Cm, Pt, RGBColor

BODY_EA = "宋体"
BODY_LATIN = "Times New Roman"
HEAD_EA = "微软雅黑"
HEAD_LATIN = "Segoe UI"
MONO = "Consolas"

C_DARK = RGBColor(0x1F, 0x38, 0x64)
C_HEAD2 = RGBColor(0x2E, 0x53, 0x95)
C_GRAY = RGBColor(0x59, 0x59, 0x59)
C_LIGHT = RGBColor(0x33, 0x33, 0x33)


# ---------------------------------------------------------------- 低层工具
def _set_run_font(run, ea=BODY_EA, latin=BODY_LATIN, size=None, bold=None,
                  color=None, italic=None):
    if size is not None:
        run.font.size = Pt(size)
    if bold is not None:
        run.font.bold = bold
    if italic is not None:
        run.font.italic = italic
    if color is not None:
        run.font.color.rgb = color
    run.font.name = latin
    rPr = run._element.get_or_add_rPr()
    rFonts = rPr.find(qn("w:rFonts"))
    if rFonts is None:
        rFonts = OxmlElement("w:rFonts")
        rPr.append(rFonts)
    rFonts.set(qn("w:ascii"), latin)
    rFonts.set(qn("w:hAnsi"), latin)
    rFonts.set(qn("w:eastAsia"), ea)


def _shade(element, fill):
    """给单元格、段落或 run 加底色。"""
    tag = element.tag
    if tag.endswith("}r"):
        pr = element.get_or_add_rPr()
    elif tag.endswith("}tc"):
        pr = element.get_or_add_tcPr()
    else:
        pr = element.get_or_add_pPr()
    shd = OxmlElement("w:shd")
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), fill)
    pr.append(shd)


def _para_border(p, side="bottom", color="1F3864", sz=8, space=4):
    pPr = p._p.get_or_add_pPr()
    pBdr = pPr.find(qn("w:pBdr"))
    if pBdr is None:
        pBdr = OxmlElement("w:pBdr")
        pPr.append(pBdr)
    el = OxmlElement(f"w:{side}")
    el.set(qn("w:val"), "single")
    el.set(qn("w:sz"), str(sz))
    el.set(qn("w:space"), str(space))
    el.set(qn("w:color"), color)
    pBdr.append(el)


def _cell_borders(cell, color="BFBFBF", sz=4):
    tcPr = cell._tc.get_or_add_tcPr()
    borders = OxmlElement("w:tcBorders")
    for side in ("top", "left", "bottom", "right"):
        el = OxmlElement(f"w:{side}")
        el.set(qn("w:val"), "single")
        el.set(qn("w:sz"), str(sz))
        el.set(qn("w:space"), "0")
        el.set(qn("w:color"), color)
        borders.append(el)
    tcPr.append(borders)


def _no_split(row):
    trPr = row._tr.get_or_add_trPr()
    trPr.append(OxmlElement("w:cantSplit"))


def _repeat_header(row):
    trPr = row._tr.get_or_add_trPr()
    trPr.append(OxmlElement("w:tblHeader"))


def _add_field(paragraph, instr, placeholder="1", size=8.5,
               ea=BODY_EA, latin=BODY_LATIN, color=C_GRAY):
    run = paragraph.add_run()
    _set_run_font(run, ea, latin, size, color=color)
    fld_begin = OxmlElement("w:fldChar")
    fld_begin.set(qn("w:fldCharType"), "begin")
    instr_el = OxmlElement("w:instrText")
    instr_el.set(qn("xml:space"), "preserve")
    instr_el.text = instr
    fld_sep = OxmlElement("w:fldChar")
    fld_sep.set(qn("w:fldCharType"), "separate")
    text_el = OxmlElement("w:t")
    text_el.text = placeholder
    fld_end = OxmlElement("w:fldChar")
    fld_end.set(qn("w:fldCharType"), "end")
    for el in (fld_begin, instr_el, fld_sep, text_el, fld_end):
        run._r.append(el)


# ---------------------------------------------------------------- 行内解析
INLINE_RE = re.compile(
    r"(`[^`]+`)"                   # 行内代码
    r"|(\[([^\]]+)\]\(([^)]+)\))"  # 链接
    r"|(\*[^*\n]+?\*)"             # 斜体
    r"|(__[^_]+__)"                # 粗体兼容写法
)

BOLD_RE = re.compile(r"\*\*(.+?)\*\*")


def _emit_inline(paragraph, text, size, bold=False, color=None, ea=BODY_EA):
    pos = 0
    for m in INLINE_RE.finditer(text):
        if m.start() > pos:
            run = paragraph.add_run(text[pos:m.start()])
            _set_run_font(run, ea, BODY_LATIN, size, bold=bold, color=color)
        if m.group(1):
            run = paragraph.add_run(m.group(1)[1:-1])
            _set_run_font(run, "微软雅黑", MONO, size - 0.5, bold=bold, color=C_LIGHT)
            _shade(run._r, "F2F2F2")
        elif m.group(2):
            label, url = m.group(3), m.group(4)
            run = paragraph.add_run(label)
            _set_run_font(run, ea, BODY_LATIN, size, bold=bold, color=C_HEAD2)
            run.font.underline = True
            if url and not url.startswith("#"):
                run2 = paragraph.add_run(f"（{url}）")
                _set_run_font(run2, ea, BODY_LATIN, size - 1, color=C_GRAY)
        elif m.group(5):
            run = paragraph.add_run(m.group(5)[1:-1])
            _set_run_font(run, ea, BODY_LATIN, size, italic=True, color=color)
        elif m.group(6):
            run = paragraph.add_run(m.group(6)[2:-2])
            _set_run_font(run, ea, BODY_LATIN, size, bold=True, color=color)
        pos = m.end()
    if pos < len(text):
        run = paragraph.add_run(text[pos:])
        _set_run_font(run, ea, BODY_LATIN, size, bold=bold, color=color)


def add_inline(paragraph, text, size, base_bold=False, color=None, ea=BODY_EA):
    """支持 **粗体**，且粗体内部还可以再嵌 `行内代码`。"""
    pos = 0
    for m in BOLD_RE.finditer(text):
        if m.start() > pos:
            _emit_inline(paragraph, text[pos:m.start()], size, base_bold, color, ea)
        _emit_inline(paragraph, m.group(1), size, True, color, ea)
        pos = m.end()
    if pos < len(text):
        _emit_inline(paragraph, text[pos:], size, base_bold, color, ea)


# ---------------------------------------------------------------- 块级解析
def split_row(line):
    line = line.strip()
    if line.startswith("|"):
        line = line[1:]
    if line.endswith("|"):
        line = line[:-1]
    cells = re.split(r"(?<!\\)\|", line)
    return [c.strip().replace("\\|", "|") for c in cells]


def parse_blocks(md):
    lines = md.splitlines()
    blocks = []
    i = 0
    n = len(lines)
    while i < n:
        line = lines[i]
        s = line.strip()

        if not s:
            i += 1
            continue

        # 围栏代码
        if s.startswith("```"):
            i += 1
            buf = []
            while i < n and not lines[i].strip().startswith("```"):
                buf.append(lines[i])
                i += 1
            i += 1
            blocks.append(("code", "\n".join(buf)))
            continue

        # 标题
        m = re.match(r"^(#{1,6})\s+(.*)$", s)
        if m:
            blocks.append(("h", len(m.group(1)), m.group(2).strip()))
            i += 1
            continue

        # 分隔线
        if re.match(r"^-{3,}$", s) or re.match(r"^\*{3,}$", s):
            blocks.append(("hr",))
            i += 1
            continue

        # 表格：当前行含 | 且下一行是分隔行
        if "|" in s and i + 1 < n and re.match(r"^\|?[\s:|-]+\|[\s:|-]*$", lines[i + 1].strip()):
            header = split_row(s)
            i += 2
            rows = []
            while i < n and "|" in lines[i] and lines[i].strip():
                rows.append(split_row(lines[i].strip()))
                i += 1
            blocks.append(("table", header, rows))
            continue

        # 引用块（连续 > 行合并）
        if s.startswith(">"):
            buf = []
            while i < n and lines[i].strip().startswith(">"):
                buf.append(re.sub(r"^\s*>\s?", "", lines[i]))
                i += 1
            blocks.append(("quote", "\n".join(buf).strip()))
            continue

        # 图片单独成行
        m = re.match(r"^!\[([^\]]*)\]\(([^)]+)\)$", s)
        if m:
            blocks.append(("img", m.group(1), m.group(2)))
            i += 1
            continue

        # 无序列表
        m = re.match(r"^(\s*)([-*+])\s+(.*)$", line)
        if m:
            items = []
            while i < n:
                mm = re.match(r"^(\s*)([-*+])\s+(.*)$", lines[i])
                if not mm:
                    if lines[i].startswith("  ") and lines[i].strip() and items:
                        items[-1] = items[-1] + lines[i].strip()
                        i += 1
                        continue
                    break
                items.append(mm.group(3).strip())
                i += 1
            blocks.append(("ul", items))
            continue

        # 有序列表
        m = re.match(r"^(\s*)(\d+)[.)]\s+(.*)$", line)
        if m:
            items = []
            while i < n:
                mm = re.match(r"^(\s*)(\d+)[.)]\s+(.*)$", lines[i])
                if not mm:
                    # 编号项下的嵌套无序子项（如 23 §5）：必须排在"续行"判断之前，
                    # 否则子项会被当成上一项的续行，拼成"…：- require_vector(true)；- …"。
                    sub = re.match(r"^(\s{2,})[-*+]\s+(.*)$", lines[i])
                    if sub and items:
                        subs = []
                        while i < n:
                            ss = re.match(r"^(\s{2,})[-*+]\s+(.*)$", lines[i])
                            if not ss:
                                if lines[i].startswith("      ") and lines[i].strip() and subs:
                                    subs[-1] = subs[-1] + lines[i].strip()
                                    i += 1
                                    continue
                                break
                            subs.append(ss.group(2).strip())
                            i += 1
                        head = items[-1]
                        if isinstance(head, tuple):
                            items[-1] = (head[0], head[1] + subs)
                        else:
                            items[-1] = (head, subs)
                        continue
                    if lines[i].startswith("   ") and lines[i].strip() and items:
                        if isinstance(items[-1], tuple):
                            items[-1] = (items[-1][0] + lines[i].strip(), items[-1][1])
                        else:
                            items[-1] = items[-1] + lines[i].strip()
                        i += 1
                        continue
                    break
                items.append(mm.group(3).strip())
                i += 1
            blocks.append(("ol", items))
            continue

        # 普通段落：连续非空行合并
        buf = [s]
        i += 1
        while i < n:
            nxt = lines[i].strip()
            if (not nxt or nxt.startswith("#") or nxt.startswith(">") or
                    nxt.startswith("```") or nxt.startswith("|") or
                    re.match(r"^([-*+]|\d+[.)])\s+", nxt) or
                    re.match(r"^-{3,}$", nxt)):
                break
            buf.append(nxt)
            i += 1
        blocks.append(("p", " ".join(buf)))
    return blocks


# ---------------------------------------------------------------- 渲染
class Builder:
    def __init__(self, title, stage, info_rows, author, date,
                 plain=False, font_pt=10.5):
        self.doc = Document()
        self.title = title
        self.stage = stage
        self.info_rows = info_rows
        self.author = author
        self.date = date
        self.plain = plain
        self.font_pt = font_pt
        self.base_dir = "."
        self._setup_page()
        self._setup_styles()
        self._footer()

    def _setup_page(self):
        sec = self.doc.sections[0]
        sec.page_width = Cm(21.0)
        sec.page_height = Cm(29.7)
        sec.top_margin = Cm(2.3)
        sec.bottom_margin = Cm(2.0)
        sec.left_margin = Cm(2.3)
        sec.right_margin = Cm(2.3)

    def _setup_styles(self):
        st = self.doc.styles["Normal"]
        st.font.name = BODY_LATIN
        st.font.size = Pt(self.font_pt)
        st.element.rPr.rFonts.set(qn("w:eastAsia"), BODY_EA)
        pf = st.paragraph_format
        # 手机阅读版把行距放宽一点：屏幕窄，行密了容易串行
        pf.line_spacing = 1.55 if self.plain else 1.42
        pf.space_before = Pt(0)
        pf.space_after = Pt(3)

    def _footer(self):
        sec = self.doc.sections[0]
        p = sec.footer.paragraphs[0]
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        if self.plain:
            run = p.add_run(f"{self.title}　　")
        else:
            run = p.add_run(f"CIE-2026 · vdot.vv · {self.stage}　　")
        _set_run_font(run, BODY_EA, BODY_LATIN, 8.5, color=C_GRAY)
        run = p.add_run("第 ")
        _set_run_font(run, BODY_EA, BODY_LATIN, 8.5, color=C_GRAY)
        _add_field(p, " PAGE ")
        run = p.add_run(" 页 / 共 ")
        _set_run_font(run, BODY_EA, BODY_LATIN, 8.5, color=C_GRAY)
        _add_field(p, " NUMPAGES ", placeholder="12")
        run = p.add_run(" 页")
        _set_run_font(run, BODY_EA, BODY_LATIN, 8.5, color=C_GRAY)

    # ---------- 封面 + 目录
    def plain_title(self, text, subtitle=""):
        """手机阅读版：不要封面页，标题直接放在第一页顶上。"""
        p = self.doc.add_paragraph()
        p.paragraph_format.space_before = Pt(6)
        p.paragraph_format.space_after = Pt(2)
        p.paragraph_format.keep_with_next = True
        run = p.add_run(text)
        _set_run_font(run, HEAD_EA, HEAD_LATIN, 21, bold=True, color=C_DARK)
        if subtitle:
            p2 = self.doc.add_paragraph()
            p2.paragraph_format.space_after = Pt(4)
            run = p2.add_run(subtitle)
            _set_run_font(run, BODY_EA, BODY_LATIN, 10.5, color=C_GRAY)
        rule = self.doc.add_paragraph()
        rule.paragraph_format.space_before = Pt(4)
        rule.paragraph_format.space_after = Pt(10)
        _para_border(rule, "bottom", "BFBFBF", 8, 1)

    def cover(self):
        for _ in range(2):
            p = self.doc.add_paragraph()
            p.paragraph_format.space_after = Pt(0)

        p = self.doc.add_paragraph()
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        run = p.add_run("CIE 2026 RISC-V 自定义指令设计赛")
        _set_run_font(run, HEAD_EA, HEAD_LATIN, 13, color=C_HEAD2)

        p = self.doc.add_paragraph()
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        p.paragraph_format.space_before = Pt(2)
        run = p.add_run("参赛作品报告")
        _set_run_font(run, HEAD_EA, HEAD_LATIN, 12, color=C_GRAY)

        p = self.doc.add_paragraph()
        p.paragraph_format.space_before = Pt(46)
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        run = p.add_run(self.stage)
        _set_run_font(run, HEAD_EA, HEAD_LATIN, 30, bold=True, color=C_DARK)

        p = self.doc.add_paragraph()
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        p.paragraph_format.space_before = Pt(10)
        run = p.add_run(self.title)
        _set_run_font(run, HEAD_EA, HEAD_LATIN, 15, bold=True, color=C_LIGHT)
        _para_border(p, "bottom", "2E5395", 6, 10)

        p = self.doc.add_paragraph()
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        p.paragraph_format.space_before = Pt(18)
        run = p.add_run("面向边缘 AI 大语言模型推理的 RISC-V 自定义 vdot 指令设计")
        _set_run_font(run, HEAD_EA, HEAD_LATIN, 10.5, color=C_GRAY)
        p = self.doc.add_paragraph()
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        run = p.add_run("——基于“香山”昆明湖 V2 处理器架构")
        _set_run_font(run, HEAD_EA, HEAD_LATIN, 10.5, color=C_GRAY)

        p = self.doc.add_paragraph()
        p.paragraph_format.space_before = Pt(40)

        rows = ([("参赛团队", self.author)] + self.info_rows +
                [("文档版本", "v1.1"), ("日期", self.date)])
        table = self.doc.add_table(rows=len(rows), cols=2)
        table.alignment = WD_TABLE_ALIGNMENT.CENTER
        table.autofit = False
        for r, (k, v) in enumerate(rows):
            row = table.rows[r]
            _no_split(row)
            c0, c1 = row.cells
            c0.width = Cm(4.2)
            c1.width = Cm(8.4)
            for c in (c0, c1):
                _cell_borders(c, "FFFFFF", 2)
                c.paragraphs[0].paragraph_format.space_after = Pt(4)
            pk = c0.paragraphs[0]
            pk.alignment = WD_ALIGN_PARAGRAPH.RIGHT
            run = pk.add_run(k + "　")
            _set_run_font(run, HEAD_EA, HEAD_LATIN, 9.5, color=C_GRAY)
            pv = c1.paragraphs[0]
            run = pv.add_run(v)
            _set_run_font(run, BODY_EA, BODY_LATIN, 9.5, color=C_LIGHT)

        # 目录页
        self.doc.add_page_break()
        p = self.doc.add_paragraph()
        p.paragraph_format.space_after = Pt(8)
        run = p.add_run("目　录")
        _set_run_font(run, HEAD_EA, HEAD_LATIN, 16, bold=True, color=C_DARK)
        _para_border(p, "bottom", "BFBFBF", 6, 6)

        p = self.doc.add_paragraph()
        run = p.add_run()
        _set_run_font(run, BODY_EA, BODY_LATIN, 10.5)
        fld_begin = OxmlElement("w:fldChar")
        fld_begin.set(qn("w:fldCharType"), "begin")
        instr = OxmlElement("w:instrText")
        instr.set(qn("xml:space"), "preserve")
        instr.text = ' TOC \\o "1-2" \\h \\z \\u '
        fld_sep = OxmlElement("w:fldChar")
        fld_sep.set(qn("w:fldCharType"), "separate")
        t = OxmlElement("w:t")
        t.text = "（导出 PDF 时自动更新目录）"
        fld_end = OxmlElement("w:fldChar")
        fld_end.set(qn("w:fldCharType"), "end")
        for el in (fld_begin, instr, fld_sep, t, fld_end):
            run._r.append(el)
        self.doc.add_page_break()

    # ---------- 块渲染
    def heading(self, level, text):
        size, color, before, after = {
            1: (18, C_DARK, 16, 6),
            2: (14, C_HEAD2, 12, 4),
            3: (11.5, C_HEAD2, 9, 3),
            4: (10.5, C_LIGHT, 6, 2),
        }.get(level, (10.5, C_LIGHT, 6, 2))
        p = self.doc.add_paragraph()
        p.paragraph_format.space_before = Pt(before)
        p.paragraph_format.space_after = Pt(after)
        p.paragraph_format.keep_with_next = True
        add_inline(p, text, size, base_bold=True, color=color, ea=HEAD_EA)
        if level == 1:
            _para_border(p, "bottom", "1F3864", 10, 6)
            p.style = self.doc.styles["Heading 1"]
        elif level == 2:
            p.style = self.doc.styles["Heading 2"]
        elif level == 3:
            p.style = self.doc.styles["Heading 3"]
        else:
            p.style = self.doc.styles["Heading 4"]

    def paragraph(self, text):
        p = self.doc.add_paragraph()
        p.paragraph_format.first_line_indent = Cm(0.74)
        p.alignment = WD_ALIGN_PARAGRAPH.LEFT
        add_inline(p, text, 10.5)

    def bullet(self, items):
        for it in items:
            p = self.doc.add_paragraph()
            pf = p.paragraph_format
            pf.left_indent = Cm(0.75)
            pf.first_line_indent = Cm(-0.44)
            pf.space_after = Pt(2)
            run = p.add_run("•　")
            _set_run_font(run, BODY_EA, BODY_LATIN, 10.5, color=C_HEAD2)
            add_inline(p, it, 10.5)

    def ordered(self, items):
        for k, it in enumerate(items, 1):
            subs = []
            if isinstance(it, tuple):
                it, subs = it
            p = self.doc.add_paragraph()
            pf = p.paragraph_format
            pf.left_indent = Cm(0.85)
            pf.first_line_indent = Cm(-0.55)
            pf.space_after = Pt(2)
            run = p.add_run(f"{k}.　")
            _set_run_font(run, BODY_EA, BODY_LATIN, 10.5, bold=True, color=C_HEAD2)
            add_inline(p, it, 10.5)
            for sub in subs:
                p = self.doc.add_paragraph()
                pf = p.paragraph_format
                pf.left_indent = Cm(1.55)
                pf.first_line_indent = Cm(-0.44)
                pf.space_after = Pt(1)
                run = p.add_run("•　")
                _set_run_font(run, BODY_EA, BODY_LATIN, 10.5, color=C_HEAD2)
                add_inline(p, sub, 10.5)

    def quote(self, text):
        p = self.doc.add_paragraph()
        pf = p.paragraph_format
        pf.left_indent = Cm(0.6)
        pf.right_indent = Cm(0.3)
        pf.space_before = Pt(4)
        pf.space_after = Pt(4)
        _para_border(p, "left", "8EA9DB", 18, 8)
        first = True
        for line in text.split("\n"):
            stripped = line.strip()
            if not first:
                p.add_run().add_break()
            first = False
            if stripped.startswith("- ") or stripped.startswith("* "):
                add_inline(p, "•　" + stripped[2:], 9.5, color=C_GRAY)
            else:
                add_inline(p, stripped, 9.5, color=C_GRAY)

    def code(self, text):
        table = self.doc.add_table(rows=1, cols=1)
        table.alignment = WD_TABLE_ALIGNMENT.CENTER
        cell = table.rows[0].cells[0]
        cell.width = Cm(16.2)
        _cell_borders(cell, "D9D9D9", 4)
        _shade(cell._tc, "F7F7F7")
        tcPr = cell._tc.get_or_add_tcPr()
        mar = OxmlElement("w:tcMar")
        for side, v in (("top", 80), ("left", 120), ("bottom", 80), ("right", 120)):
            el = OxmlElement(f"w:{side}")
            el.set(qn("w:w"), str(v))
            el.set(qn("w:type"), "dxa")
            mar.append(el)
        tcPr.append(mar)

        p = cell.paragraphs[0]
        pf = p.paragraph_format
        pf.line_spacing = 1.08
        pf.space_after = Pt(0)
        lines = text.split("\n")
        for idx, line in enumerate(lines):
            run = p.add_run(line)
            _set_run_font(run, HEAD_EA, MONO, 9.2 if self.plain else 8.2,
                          color=RGBColor(0x33, 0x33, 0x33))
            if idx < len(lines) - 1:
                run.add_break()
        gap = self.doc.add_paragraph()
        gap.paragraph_format.space_after = Pt(0)
        gap.paragraph_format.line_spacing = 0.6

    def image(self, alt, path):
        # 图片按 md 所在目录解析；找不到时回退到当前工作目录（仓库根）。
        # 找不到就**直接失败**：2026-11-03 出现过静默掉图（stage1.docx 140KB->49KB，构建还报成功）。
        real = path
        if not os.path.isabs(real):
            cand = os.path.join(self.base_dir, path)
            if os.path.exists(cand):
                real = cand
            elif os.path.exists(path):
                real = path
            else:
                real = cand
        if not os.path.exists(real):
            raise SystemExit(
                f"[缺图] {path}\n  查过: {os.path.join(self.base_dir, path)}\n"
                f"         {os.path.abspath(path)}\n  图片必须随仓库一起提交。")

        p = self.doc.add_paragraph()
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        p.paragraph_format.space_before = Pt(6)
        p.paragraph_format.space_after = Pt(2)
        p.add_run().add_picture(real, width=Cm(15.2))
        if alt:
            cap = self.doc.add_paragraph()
            cap.alignment = WD_ALIGN_PARAGRAPH.CENTER
            cap.paragraph_format.space_after = Pt(6)
            r = cap.add_run(alt)
            _set_run_font(r, BODY_EA, BODY_LATIN, 8.5, color=C_GRAY)

    def table(self, header, rows):
        ncol = len(header)
        table = self.doc.add_table(rows=1, cols=ncol)
        table.alignment = WD_TABLE_ALIGNMENT.CENTER
        table.autofit = True
        table.style = self.doc.styles["Table Grid"]

        hdr = table.rows[0]
        _no_split(hdr)
        _repeat_header(hdr)
        for j, cell in enumerate(hdr.cells):
            _cell_borders(cell)
            _shade(cell._tc, "E8EEF7")
            p = cell.paragraphs[0]
            p.paragraph_format.space_after = Pt(0)
            p.paragraph_format.line_spacing = 1.12
            add_inline(p, header[j], 8.8, base_bold=True, color=C_DARK)

        for row_data in rows:
            row = table.add_row()
            _no_split(row)
            for j in range(ncol):
                cell = row.cells[j]
                _cell_borders(cell)
                p = cell.paragraphs[0]
                p.paragraph_format.space_after = Pt(0)
                p.paragraph_format.line_spacing = 1.12
                txt = row_data[j] if j < len(row_data) else ""
                add_inline(p, txt, 8.8, color=C_LIGHT)

        # 表头永远不许单独留在页底（表头 + 第一行绑定）；≤4 行的短表整表不跨页。
        # 起因：23 §6.2 的 4 行表被拆成"3 行 + 1 行"；而只加"整表不跨页"又会让
        # 24 §7 的 6 行表把表头孤零零留在上一页页底。
        keep_all = len(rows) <= 4
        for ri in range(len(table.rows) - 1):
            if ri == 0 or keep_all:
                for cell in table.rows[ri].cells:
                    for p in cell.paragraphs:
                        p.paragraph_format.keep_with_next = True

        gap = self.doc.add_paragraph()
        gap.paragraph_format.space_after = Pt(0)
        gap.paragraph_format.line_spacing = 0.6

    def hr(self):
        p = self.doc.add_paragraph()
        p.paragraph_format.space_before = Pt(2)
        p.paragraph_format.space_after = Pt(2)
        _para_border(p, "bottom", "D9D9D9", 4, 1)

    def build(self, md, base_dir):
        self.base_dir = base_dir
        blocks = parse_blocks(md)
        if self.plain:
            first_h1 = ""
            for b in blocks:
                if b[0] == "h" and b[1] == 1:
                    first_h1 = b[2]
                    break
            self.plain_title(first_h1 or self.title)
        else:
            self.cover()
        skip_first_h1 = True
        for b in blocks:
            kind = b[0]
            if kind == "h":
                if b[1] == 1 and skip_first_h1:
                    skip_first_h1 = False
                    continue
                self.heading(b[1], b[2])
            elif kind == "p":
                self.paragraph(b[1])
            elif kind == "ul":
                self.bullet(b[1])
            elif kind == "ol":
                self.ordered(b[1])
            elif kind == "quote":
                self.quote(b[1])
            elif kind == "code":
                self.code(b[1])
            elif kind == "img":
                self.image(b[1], b[2])
            elif kind == "table":
                self.table(b[1], b[2])
            elif kind == "hr":
                self.hr()
        return self.doc


def extract_title(md):
    m = re.search(r"^#\s+(.+)$", md, re.M)
    return m.group(1).strip() if m else "参赛作品报告"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("md")
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--stage", default="")
    ap.add_argument("--stage-num", type=int, default=0, help="1..3，自动生成中文阶段名")
    ap.add_argument("--score", default="")
    ap.add_argument("--score-num", type=int, default=0, help="该阶段分值，如 15")
    ap.add_argument("--author", default="你可知我冷暖")
    ap.add_argument("--date", default="2026-10-31")
    ap.add_argument("--plain", action="store_true",
                    help="两页式阅读版：不要封面/目录，标题放页首，适合手机看")
    ap.add_argument("--font", type=float, default=10.5, help="正文字号（pt）")
    args = ap.parse_args()

    with open(args.md, encoding="utf-8") as f:
        md = f.read()

    title = extract_title(md)
    short = title.split("：", 1)[1] if "：" in title else title

    cn = {1: "一", 2: "二", 3: "三", 4: "四"}
    stage = args.stage
    if args.stage_num in cn:
        stage = f"阶段{cn[args.stage_num]}"
    if not stage:
        m = re.match(r"^(阶段[一二三四])", title)
        stage = m.group(1) if m else ""

    score = args.score
    if not score and args.score_num and stage:
        score = f"{stage} {args.score_num} 分"

    info_rows = []
    if score:
        info_rows.append(("对应评分", score))
    info_rows.append(("指令编码", "vdot.vv = 0xFA20A1D7"))

    b = Builder(title if args.plain else short, stage, info_rows,
                args.author, args.date,
                plain=args.plain, font_pt=args.font)
    doc = b.build(md, os.path.dirname(os.path.abspath(args.md)))

    out = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    doc.save(out)
    print(f"OK  {out}  ({os.path.getsize(out) / 1024:.0f} KB)")


if __name__ == "__main__":
    sys.exit(main())
