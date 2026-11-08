/* vdot.vv 对 vstart 的处理
 *
 * 题面没有 vstart 概念，但香山对**所有向量算术指令**都有一条既有约束：
 *   VecExceptionGen.scala:275  vstartIllegal = isVArith && (io.vstart =/= 0)
 * 实测 vmul.vv 在 vstart=4 时同样报 illegal，因此 vdot 沿用该行为：
 *
 *   vstart == 0 → 正常执行
 *   vstart != 0 → illegal instruction（mcause = 2）
 *
 * 这个用例同时验证 DUT 与 NEMU 的一致性（NEMU 侧已按同一规则实现）。
 */
#define UART_TX (*(volatile unsigned char *)0x40600004UL)

volatile unsigned long g_trapped;
volatile unsigned long g_cause;

__asm__(
    ".section .text\n"
    ".globl _start\n"
    "_start:\n"
    "  li sp, 0x80100000\n"
    "  la t0, trap_entry\n"
    "  csrw mtvec, t0\n"
    "  call kmain\n"
    "1:\n"
    "  wfi\n"
    "  j 1b\n"
    "  .align 4\n"
    "trap_entry:\n"
    "  addi sp, sp, -16\n"
    "  sw t0, 0(sp)\n"
    "  sw t1, 4(sp)\n"
    "  sw t2, 8(sp)\n"
    "  csrr t0, mcause\n"
    "  la t1, g_trapped\n"
    "  li t2, 1\n"
    "  sw t2, 0(t1)\n"
    "  la t1, g_cause\n"
    "  sw t0, 0(t1)\n"
    "  csrr t0, mepc\n"
    "  addi t0, t0, 4\n"
    "  csrw mepc, t0\n"
    "  csrw 0x008, zero\n"      /* 清 vstart，避免后续指令继续触发 */
    "  lw t0, 0(sp)\n"
    "  lw t1, 4(sp)\n"
    "  lw t2, 8(sp)\n"
    "  addi sp, sp, 16\n"
    "  mret\n");

static void putc_u(char c) { UART_TX = (unsigned char)c; }
static void puts_u(const char *s) { while (*s) putc_u(*s++); }
static void puthex8(unsigned v) {
  const char *d = "0123456789abcdef";
  for (int i = 28; i >= 0; i -= 4) putc_u(d[(v >> i) & 0xF]);
}
static void putdec(unsigned long v) {
  char buf[24]; int n = 0;
  if (v == 0) { putc_u('0'); return; }
  while (v > 0) { buf[n++] = (char)('0' + (v % 10)); v /= 10; }
  while (n > 0) putc_u(buf[--n]);
}

static signed char a[16], b[16];
static unsigned char out[16];
static int fails = 0;

static unsigned run_vstart(int vstart) {
  for (int i = 0; i < 16; i++) out[i] = 0xA5;
  __asm__ volatile(
      "li t0, 16\n\t"
      "vsetvli t1, t0, e8, m1, ta, ma\n\t"
      "vle8.v v1, (%0)\n\t"
      "vle8.v v2, (%1)\n\t"
      "csrw 0x008, %3\n\t"        /* vstart = vstart */
      ".word 0xFA20A1D7\n\t"       /* vdot v3, v1, v2 */
      "csrw 0x008, zero\n\t"
      "li t0, 16\n\t"
      "vsetvli t1, t0, e8, m1, ta, ma\n\t"
      "vse8.v v3, (%2)\n\t"
      :
      : "r"(a), "r"(b), "r"(out), "r"(vstart)
      : "t0", "t1", "memory");
  return (unsigned)out[0] | ((unsigned)out[1] << 8) |
         ((unsigned)out[2] << 16) | ((unsigned)out[3] << 24);
}

/* expect_trap = 1 表示该用例应当触发 illegal */
static void check(const char *name, int vstart, int expect_trap, unsigned expect_val) {
  g_trapped = 0;
  g_cause = 0;
  unsigned got = run_vstart(vstart);
  puts_u("  "); puts_u(name);
  puts_u("  vstart="); putdec((unsigned long)vstart);
  puts_u("  trapped="); putdec(g_trapped);
  puts_u("  cause="); puthex8((unsigned)g_cause);
  if (expect_trap) {
    if (g_trapped == 1 && g_cause == 2) {
      puts_u("  PASS (illegal as expected)\n");
    } else {
      puts_u("  FAIL (expected illegal)\n");
      fails++;
    }
  } else {
    puts_u("  got="); puthex8(got);
    if (!g_trapped && got == expect_val) {
      puts_u("  PASS\n");
    } else {
      puts_u("  FAIL\n");
      fails++;
    }
  }
}

void kmain(void) {
  __asm__ volatile("li t5, 0x600\n\t csrs mstatus, t5" ::: "t5");
  for (int i = 0; i < 16; i++) { a[i] = (signed char)(i + 1); b[i] = (signed char)(i + 1); }

  puts_u("vdot.vv vstart handling (XiangShan house rule: vstart!=0 -> illegal)\n");
  check("[0] vstart=0 ", 0, 0, 204);
  check("[1] vstart=4 ", 4, 1, 0);
  check("[2] vstart=16", 16, 1, 0);
  puts_u(fails == 0 ? "RESULT: PASS\n" : "RESULT: FAIL\n");
}
