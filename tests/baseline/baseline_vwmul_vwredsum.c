/* vdot 基线实现 + 与 vdot 指令的周期对比
 *
 * 基线：用 RISC-V 已有向量指令组合实现与 vdot 相同的功能
 *
 *   vs1, vs2 (各 8 个 int8)
 *     │
 *     ├─ vwmul.vv   v4, v1, v2      # 8 个 int8 放大相乘 → 8 个 int16 乘积
 *     └─ vwredsum.vs v3, v4, v0     # 8 个 int16 归约 → 32 位标量（v0[0] 为初值 0）
 *
 * 即两条向量指令 + 两次 vsetvli，共 4 条。
 *
 * 编译：
 *   riscv64-linux-gnu-gcc -march=rv64gcv -mabi=lp64d -nostdlib -nostartfiles \
 *       -ffreestanding -T link.ld -o baseline.elf baseline_vwmul_vwredsum.c
 *   加 -DWITH_VDOT=1 可同时测量 vdot 指令（需要 RTL/NEMU 支持）
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

static void putdec(unsigned long v) {
  char buf[24];
  int n = 0;
  if (v == 0) { putc_u('0'); return; }
  while (v > 0) { buf[n++] = (char)('0' + (v % 10)); v /= 10; }
  while (n > 0) putc_u(buf[--n]);
}

static inline unsigned long rd_mcycle(void) {
  unsigned long x;
  __asm__ volatile("csrr %0, mcycle" : "=r"(x));
  return x;
}

/* ---------------- 数据 ---------------- */
static signed char a[16], b[16];
static unsigned char out[16];

/* 把 a/b 装进 v1/v2（只做一次，不计入被测周期） */
static void load_vectors(void) {
  __asm__ volatile(
      "li t0, 8\n\t"
      "vsetvli t1, t0, e8, m1, ta, ma\n\t"
      "vle8.v v1, (%0)\n\t"
      "vle8.v v2, (%1)\n\t"
      "vmv.v.i v0, 0\n\t"          /* vwredsum 的初值 = 0 */
      :
      : "r"(a), "r"(b)
      : "t0", "t1", "memory");
}

/* 基线内核：4 条指令 */
static void kernel_baseline(void) {
  __asm__ volatile(
      "li t0, 8\n\t"
      "vsetvli t1, t0, e8, m1, ta, ma\n\t"
      "vwmul.vv v4, v1, v2\n\t"
      "vsetvli t1, t0, e16, m1, ta, ma\n\t"
      "vwredsum.vs v3, v4, v0\n\t"
      :
      :
      : "t0", "t1", "memory");
}

/* 把 v3 的结果取出来（不计入被测周期） */
static unsigned read_v3(void) {
  unsigned long r;
  __asm__ volatile(
      "li t0, 8\n\t"
      "vsetvli t1, t0, e32, m1, ta, ma\n\t"
      "vmv.x.s %0, v3\n\t"
      : "=r"(r)
      :
      : "t0", "t1");
  return (unsigned)r;
}

#ifdef WITH_VDOT
/* vdot 内核：1 条指令（vdot v3, v1, v2） */
static void kernel_vdot(void) {
  __asm__ volatile(
      "li t0, 8\n\t"
      "vsetvli t1, t0, e8, m1, ta, ma\n\t"
      ".word 0xFA20A1D7\n\t"       /* vdot v3, v1, v2 */
      :
      :
      : "t0", "t1", "memory");
}
#endif

/* 迭代次数：RTL 仿真很慢，可用 -DNITER=50 覆盖 */
#ifndef NITER
#define NITER 1000
#endif

static unsigned long time_kernel(void (*kern)(void)) {
  unsigned long t0, t1;
  t0 = rd_mcycle();
  for (int i = 0; i < NITER; i++) kern();
  t1 = rd_mcycle();
  return t1 - t0;
}

void kmain(void) {
  /* 裸机必踩的坑：不开 mstatus.VS，第一条向量指令就非法指令 trap */
  __asm__ volatile("li t5, 0x600\n\t csrs mstatus, t5" ::: "t5");

  for (int i = 0; i < 16; i++) { a[i] = (signed char)(i + 1); b[i] = (signed char)(i + 1); }
  load_vectors();

  puts_u("vdot baseline test (8 x int8 dot product)\n");
  puts_u("kernel: vsetvli + vwmul.vv + vsetvli + vwredsum.vs\n");

  /* ---- 正确性 ---- */
  kernel_baseline();
  unsigned r = read_v3();
  puts_u("baseline result = "); puthex8(r);
  puts_u("  (want 000000cc)\n");

#ifdef WITH_VDOT
  kernel_vdot();
  unsigned rv = read_v3();
  puts_u("vdot     result = "); puthex8(rv);
  puts_u("  (want 000000cc)\n");
  if (rv != r) { puts_u("MISMATCH between baseline and vdot!\n"); }
#endif

  /* ---- 周期 ---- */
  unsigned long c_base = time_kernel(kernel_baseline);
  puts_u("baseline: ");
  putdec(c_base); puts_u(" cycles / "); putdec(NITER);
  puts_u(" iters = ");
  putdec(c_base / NITER); puts_u(" cyc/iter\n");

#ifdef WITH_VDOT
  unsigned long c_vdot = time_kernel(kernel_vdot);
  puts_u("vdot    : ");
  putdec(c_vdot); puts_u(" cycles / "); putdec(NITER);
  puts_u(" iters = ");
  putdec(c_vdot / NITER); puts_u(" cyc/iter\n");

  puts_u("speedup(x100) = ");
  putdec((c_base * 100) / (c_vdot ? c_vdot : 1));
  puts_u("  (= ");
  putdec((c_base * 100) / (c_vdot ? c_vdot : 1) / 100);
  puts_u(".");
  putdec(((c_base * 100) / (c_vdot ? c_vdot : 1)) % 100);
  puts_u("x)\n");
#endif

  puts_u("RESULT: DONE\n");
}
