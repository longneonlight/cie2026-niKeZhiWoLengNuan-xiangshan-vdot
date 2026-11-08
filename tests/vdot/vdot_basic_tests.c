/* vdot.vv 基础功能测试（正数 / 负数 / 边界 / 混合 / vl 变体）
 *
 * 编码：funct6=111110 vm=1 | vs2 | vs1 | funct3=010 | vd | opcode=1010111
 *       vdot v3, v1, v2 = 0xFA20A1D7
 *
 * 语义：acc = Σ_{i<8} (int8)vs1[i] * (int8)vs2[i]（32 位回绕，题面 range(8)）
 *       vd[31:0] = acc，vd[127:32] = 0
 *
 * 每个用例都检查：
 *   1) 低 4 字节 == 期望值
 *   2) 高 12 字节 == 0（tail 清零语义）
 *
 * 说明：vdot 之后会重新 vsetvli 到 vl=16 再 vse8，这样 vl<16 的用例也能把
 *       整个寄存器搬出来看尾部位，不受计算用 vl 影响。
 */
#define UART_TX (*(volatile unsigned char *)0x40600004UL)

__asm__(
    ".section .text\n"
    ".globl _start\n"
    "_start:\n"
    "  li sp, 0x80100000\n"
    "  call kmain\n"
    "1:\n"
    "  wfi\n"
    "  j 1b\n");

static void putc_u(char c) { UART_TX = (unsigned char)c; }
static void puts_u(const char *s) { while (*s) putc_u(*s++); }

static void puthex8(unsigned v) {
  const char *d = "0123456789abcdef";
  for (int i = 28; i >= 0; i -= 4) putc_u(d[(v >> i) & 0xF]);
}

static signed char a[16], b[16];
static unsigned char out[16];

/* 以给定 vl 跑一次 vdot，返回低 32 位；同时检查高 12 字节是否为 0
 *
 * 注意装载与计算用**不同的 vsetvli**：
 *   - 装载用 vl=16，保证 v1/v2 的 16 个字节都是程序显式写过的值；
 *   - 计算用用例给的 vl。题面是 `for i in range(8)`，vl 不参与计算，
 *     所以 vl=4/8/0 必须得到与 vl=16 相同的结果（这正是本组用例要证的事）。
 * 若用 vl=4 装载，v1[4..7] 会落在 tail-agnostic 的未定义区，结果不可判定。 */
static unsigned run_one(int vl, int *tail_ok) {
  for (int i = 0; i < 16; i++) out[i] = 0xA5;   /* 先污染，才能看出是否被清零 */

  asm volatile(
      "li t5, 0x600\n\t"
      "csrs mstatus, t5\n\t"
      "li t0, 16\n\t"
      "vsetvli t1, t0, e8, m1, ta, ma\n\t"   /* 全寄存器装载 */
      "vle8.v v1, (%0)\n\t"
      "vle8.v v2, (%1)\n\t"
      "vsetvli t1, %3, e8, m1, ta, ma\n\t"   /* 只影响紧随其后的 vdot（而 vdot 不看 vl） */
      ".word 0xFA20A1D7\n\t"        /* vdot v3, v1, v2 */
      "li t0, 16\n\t"
      "vsetvli t1, t0, e8, m1, ta, ma\n\t"
      "vse8.v v3, (%2)\n\t"
      :
      : "r"(a), "r"(b), "r"(out), "r"(vl)
      : "t0", "t1", "t5", "memory");

  *tail_ok = 1;
  for (int i = 4; i < 16; i++) if (out[i] != 0) { *tail_ok = 0; break; }

  return (unsigned)out[0] | ((unsigned)out[1] << 8) |
         ((unsigned)out[2] << 16) | ((unsigned)out[3] << 24);
}

static int fails = 0;

static void case_run(const char *name, int mode, int vl, int expect) {
  for (int i = 0; i < 16; i++) {
    switch (mode) {
      case 0: a[i] = (signed char)(i + 1);   b[i] = (signed char)(i + 1);  break; /* 正数递增 */
      case 1: a[i] = (signed char)(-(i + 1)); b[i] = (signed char)(i + 1); break; /* 负数 */
      case 2: a[i] = (signed char)-128;      b[i] = (signed char)-128;     break; /* 边界：最小值 */
      case 3: a[i] = (signed char)127;       b[i] = (signed char)127;      break; /* 边界：最大值 */
      case 5: a[i] = (signed char)((i & 1) ? -1 : 1); b[i] = (signed char)1; break; /* 正负混合 */
      default: a[i] = 0;                     b[i] = (signed char)(i + 1); break; /* 全零 */
    }
  }
  int tail_ok = 0;
  unsigned got = run_one(vl, &tail_ok);

  puts_u("  "); puts_u(name);
  puts_u("  got="); puthex8(got);
  puts_u("  want="); puthex8((unsigned)expect);
  if ((int)got == expect && tail_ok) {
    puts_u("  PASS\n");
  } else {
    puts_u("  FAIL");
    if (!tail_ok) puts_u(" (tail not zeroed)");
    puts_u("\n");
    fails++;
  }
}

void kmain(void) {
  puts_u("vdot.vv basic tests (SEW=8, VLEN=128)\n");
  case_run("[1] positive   vl=16", 0, 16, 204);      /* Σ_{i=1..8} i²      */
  case_run("[2] negative   vl=16", 1, 16, -204);     /* -Σ_{i=1..8} i²     */
  case_run("[3] bound-min  vl=16", 2, 16, 131072);   /* 8 * (-128 * -128)  */
  case_run("[3] bound-max  vl=16", 3, 16, 129032);   /* 8 * (127 * 127)    */
  case_run("[4] zero       vl=16", 4, 16, 0);
  case_run("[5] mixed ±1   vl=16", 5, 16, 0);
  case_run("[6] positive   vl=8 ", 0, 8, 204);       /* 与 [1] 相同：固定 8 个 */
  case_run("[7] positive   vl=4 ", 0, 4, 204);       /* 与 [1] 相同：vl 不参与计算 */
  case_run("[8] vl=0            ", 0, 0, 204);       /* 与 [1] 相同：vl 不参与计算 */
  /* 注：vl=0 曾因 NEMU 的 vstart 处理路径挂死而被临时关闭；2026-10-29 移除 NEMU 侧
   *     vstart 逻辑（题面无 vstart 概念）后恢复正常，已重新启用。
   *     2026-11-01 语义收紧：vl 不再参与计算，[7]/[8] 的期望值从 30/0 改为 204。 */
  puts_u(fails == 0 ? "RESULT: PASS\n" : "RESULT: FAIL\n");
}
