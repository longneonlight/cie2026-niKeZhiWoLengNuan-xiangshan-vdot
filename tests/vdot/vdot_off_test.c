/* hasVdot = false 的关断路径测试
 *
 * 目的：证明把 XSCoreParameters.hasVdot 置 false 之后，
 *   (1) 0xFA20A1D7 (vdot.vv) 在 e8/m1/vm=1（正常合法 vtype）下**报非法指令**，
 *       而不是"静默退化成一条 vmul 并写错 vd"；
 *   (2) 目的寄存器 v3 没有被写（无部分写回）；
 *   (3) 核仍然活着（紧接着的 vadd.vv 结果正确）；
 *   (4) vm=0 的编码 0xF820A1D7 同样报非法。
 *
 * 判定：mcause 必须等于 2 (illegal instruction)。
 *
 * 编译（与 run-all.sh 同参数）：
 *   riscv64-linux-gnu-gcc -march=rv64gcv -mabi=lp64d -nostdlib -nostartfiles \
 *       -ffreestanding -O2 -fno-pic -fno-pie -mcmodel=medany -T link.ld \
 *       -o vdot_off.elf vdot_off_test.c
 *
 * 运行（必须 --no-diff：NEMU 里 vdot 是合法的，difftest 必然失配）：
 *   emu -i vdot_off.bin --no-diff
 */
#define UART_TX (*(volatile unsigned char *)0x40600004UL)

volatile unsigned long g_trapped;
volatile unsigned long g_cause;
volatile unsigned long g_mepc;

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
    "  la t1, g_mepc\n"
    "  sw t0, 0(t1)\n"
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

static void reset_trap(void) { g_trapped = 0; g_cause = 0; g_mepc = 0; }

/* 读 v3 的第 0 个元素（需要先 vsetvli e32） */
static unsigned read_v3(void) {
  unsigned r;
  __asm__ volatile("vmv.x.s %0, v3" : "=r"(r));
  return r;
}

void kmain(void) {
  /* mstatus.FS = 11（向量/浮点需要） */
  __asm__ volatile("li t5, 0x600\n\t csrs mstatus, t5" ::: "t5");

  /* 打开 mnstatus.NMIE（同 vdot_illegal_tests.c 的理由） */
  {
    unsigned long mn = 0;
    __asm__ volatile("csrr %0, 0x744" : "=r"(mn));
    mn |= 0x8;
    __asm__ volatile("csrw 0x744, %0" ::"r"(mn));
  }
  reset_trap();

  puts_u("vdot-off test (hasVdot=false)\n");

  /* 先把 v3 写成一个哨兵值，供后面验"没被写" */
  __asm__ volatile(
      "li t0, 8\n\t"
      "vsetvli t1, t0, e32, m1, ta, ma\n\t"
      "li t2, 0x0BADF00D\n\t"
      "vmv.v.x v3, t2\n\t"
      :
      :
      : "t0", "t1", "t2");

  /* --- case 1: 合法 vtype 下的 vdot.vv 必须非法 --- */
  reset_trap();
  __asm__ volatile(
      "li t0, 8\n\t"
      "vsetvli t1, t0, e8, m1, ta, ma\n\t"
      "li t2, 0x11\n\t"
      "vmv.v.x v1, t2\n\t"
      "vmv.v.x v2, t2\n\t"
      ".word 0xFA20A1D7\n\t"
      :
      :
      : "t0", "t1", "t2");
  {
    int ok = (g_trapped == 1) && (g_cause == 2);
    puts_u("  case1 vdot@e8m1        ");
    if (ok) {
      puts_u("PASS (trapped=1 cause=00000002)\n");
    } else {
      puts_u("FAIL (trapped=");
      puthex8((unsigned)g_trapped);
      puts_u(" cause=");
      puthex8((unsigned)g_cause);
      puts_u(" want cause=00000002)\n");
      fails++;
    }
  }

  /* --- case 2: v3 未被写（无静默写回 / 无部分写回） --- */
  {
    unsigned r;
    __asm__ volatile("li t0, 8\n\t vsetvli t1, t0, e32, m1, ta, ma" ::: "t0", "t1");
    r = read_v3();
    puts_u("  case2 v3-unmodified    ");
    if (r == 0x0BADF00DUL) {
      puts_u("PASS (v3[0]=0badf00d)\n");
    } else {
      puts_u("FAIL (v3[0]=");
      puthex8(r);
      puts_u(" want=0badf00d)\n");
      fails++;
    }
  }

  /* --- case 3: 核仍然可用：vadd.vv 结果正确 --- */
  {
    unsigned r;
    __asm__ volatile(
        "li t0, 8\n\t"
        "vsetvli t1, t0, e32, m1, ta, ma\n\t"
        "li t2, 0x11\n\t"
        "vmv.v.x v1, t2\n\t"
        "vmv.v.x v2, t2\n\t"
        "vadd.vv v5, v1, v2\n\t"
        "vmv.x.s %0, v5\n\t"
        : "=r"(r)
        :
        : "t0", "t1", "t2");
    puts_u("  case3 core-alive      ");
    if (r == 0x22UL) {
      puts_u("PASS (v5[0]=00000022)\n");
    } else {
      puts_u("FAIL (v5[0]=");
      puthex8(r);
      puts_u(" want=00000022)\n");
      fails++;
    }
  }

  /* --- case 4: vm=0 的编码同样非法 --- */
  reset_trap();
  __asm__ volatile(
      "li t0, 8\n\t"
      "vsetvli t1, t0, e8, m1, ta, ma\n\t"
      ".word 0xF820A1D7\n\t"
      :
      :
      : "t0", "t1");
  {
    int ok = (g_trapped == 1) && (g_cause == 2);
    puts_u("  case4 vm=0             ");
    if (ok) {
      puts_u("PASS (trapped=1 cause=00000002)\n");
    } else {
      puts_u("FAIL (trapped=");
      puthex8((unsigned)g_trapped);
      puts_u(" cause=");
      puthex8((unsigned)g_cause);
      puts_u(" want cause=00000002)\n");
      fails++;
    }
  }

  puts_u(fails == 0 ? "RESULT: PASS\n" : "RESULT: FAIL\n");
}
