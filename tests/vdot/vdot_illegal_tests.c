/* vdot.vv 非法指令条件测试
 *
 * 语义约定（见 docs/01-semantics.md）：
 *   SEW=e8、LMUL=1、vm=1 才是合法；其余组合必须报 illegal instruction。
 *
 * 做法：装一个最小 trap handler（记录 mcause、跳过 4 字节后 mret），
 * 逐个用例检查"该 trap 的 trap 了、该正常的正常了"。
 *
 * 编译：
 *   riscv64-linux-gnu-gcc -march=rv64gcv -mabi=lp64d -nostdlib -nostartfiles \
 *       -ffreestanding -O2 -T link.ld -o vdot_illegal.elf vdot_illegal_tests.c
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

static int fails = 0;

static void reset_trap(void) { g_trapped = 0; g_cause = 0; }

static void check(const char *name, int expect_trap) {
  int got = (int)g_trapped;
  puts_u("  ");
  puts_u(name);
  if (got == expect_trap) {
    puts_u(" PASS (trapped=");
    puthex8((unsigned)got);
    puts_u(" cause=");
    puthex8((unsigned)g_cause);
    puts_u(")\n");
  } else {
    puts_u(" FAIL: trapped=");
    puthex8((unsigned)got);
    puts_u(" want=");
    puthex8((unsigned)expect_trap);
    puts_u("\n");
    fails++;
  }
}

void kmain(void) {
  __asm__ volatile("li t5, 0x600\n\t csrs mstatus, t5" ::: "t5");

  /* 打开 mnstatus.NMIE（bit3）：NEMU 在 NMIE=0 时会把任何 trap 当 double trap 直接中止，
   * 导致 trap handler 根本跑不到。若该 CSR 不存在，csrw 会陷到 handler 里被跳过，无副作用。 */
  {
    unsigned long mn = 0;
    __asm__ volatile("csrr %0, 0x744" : "=r"(mn));
    mn |= 0x8;
    __asm__ volatile("csrw 0x744, %0" ::"r"(mn));
    g_trapped = 0;
    g_cause = 0;
  }

  puts_u("vdot.vv illegal-condition tests\n");

  /* 合法基线：e8/m1/vm=1 */
  reset_trap();
  __asm__ volatile(
      "li t0, 8\n\t"
      "vsetvli t1, t0, e8, m1, ta, ma\n\t"
      ".word 0xFA20A1D7\n\t"
      :
      :
      : "t0", "t1");
  check("e8/m1/vm=1   ", 0);

  /* 1) vm=0（0xFA20A1D7 清 bit25 → 0xF820A1D7）*/
  reset_trap();
  __asm__ volatile(
      "li t0, 8\n\t"
      "vsetvli t1, t0, e8, m1, ta, ma\n\t"
      ".word 0xF820A1D7\n\t"
      :
      :
      : "t0", "t1");
  check("vm=0        ", 1);

  /* 2) vsew=e16 */
  reset_trap();
  __asm__ volatile(
      "li t0, 8\n\t"
      "vsetvli t1, t0, e16, m1, ta, ma\n\t"
      ".word 0xFA20A1D7\n\t"
      :
      :
      : "t0", "t1");
  check("vsew=e16    ", 1);

  /* 3) vlmul=m2 */
  reset_trap();
  __asm__ volatile(
      "li t0, 8\n\t"
      "vsetvli t1, t0, e8, m2, ta, ma\n\t"
      ".word 0xFA20A1D7\n\t"
      :
      :
      : "t0", "t1");
  check("vlmul=m2    ", 1);

  puts_u(fails == 0 ? "RESULT: PASS\n" : "RESULT: FAIL\n");
}
