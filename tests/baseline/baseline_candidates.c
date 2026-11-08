/* A8：基线加固 —— 三条候选实现的同机对比（2026-10-31，红队第二轮驱动）
 *
 * 回答"2.33× 的分母是不是挑出来的"。三条实现算同一个 8 元素 int8 点积：
 *   K1  vdot.vv                    （vtype 每轮设一次）
 *   K2  vwmul.vv + vwredsum.vs     （现有基线；vtype 在 e8/e16 之间来回切）
 *   K3  标量展开                    （volatile 读，禁止编译器把循环提到外面）
 *
 * 口径与 perf_latency.c 的 [B] 完全一致：mcycle 夹住整个 NITER 循环再 ÷ NITER
 * （不是每轮各读一次 mcycle —— 那样测到的是发射间隔，不是循环代价）。
 *
 * 编译：riscv64-linux-gnu-gcc -march=rv64gcv -mabi=lp64d -nostdlib -nostartfiles \
 *        -ffreestanding -O2 -fno-pic -fno-pie -mcmodel=medany -fno-tree-vectorize -T link.ld
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
static void putdec(unsigned long v) {
  char buf[24]; int n = 0;
  if (v == 0) { putc_u('0'); return; }
  while (v > 0) { buf[n++] = (char)('0' + (v % 10)); v /= 10; }
  while (n > 0) putc_u(buf[--n]);
}
static void pad(const char *s, int w) { int n = 0; while (s[n]) n++; puts_u(s); while (n++ < w) putc_u(' '); }

static signed char a[16] __attribute__((aligned(16)));
static signed char b[16] __attribute__((aligned(16)));

#ifndef NITER
#define NITER 100
#endif

static inline unsigned long rd_mcycle(void) {
  unsigned long x;
  __asm__ volatile("csrr %0, mcycle" : "=r"(x));
  return x;
}

static void setup(void) {
  __asm__ volatile("li t5, 0x600\n\t csrs mstatus, t5" ::: "t5");
  for (int i = 0; i < 16; i++) { a[i] = (signed char)(i + 1); b[i] = (signed char)(i + 1); }
  long n = 8;
  __asm__ volatile("vsetvli t1, %0, e8, m1, ta, ma\n\t"
                   "vle8.v v1, (%1)\n\t"
                   "vle8.v v2, (%2)\n\t"
                   "vmv.v.i v0, 0"
                   :
                   : "r"(n), "r"(a), "r"(b)
                   : "t1", "memory");
}

/* K1：vdot（每轮一次 vsetvli） */
static unsigned long k_vdot(unsigned long *res) {
  long n = 8;
  unsigned long t0, t1;
  t0 = rd_mcycle();
  for (int i = 0; i < NITER; i++) {
    __asm__ volatile("li t0, 8\n\t"
                     "vsetvli t1, t0, e8, m1, ta, ma\n\t"
                     ".word 0xFA20A1D7"
                     :::
                     "t0", "t1", "memory");
  }
  t1 = rd_mcycle();
  __asm__ volatile("vsetvli t1, x0, e32, m1, ta, ma\n\t vmv.x.s %0, v3" : "=r"(*res) : : "t1");
  return (t1 - t0) / NITER;
}

/* K2：现有基线 vwmul + vwredsum（序列与 perf_latency.c 的 thr_baseline 完全一致） */
static unsigned long k_wmul(unsigned long *res) {
  unsigned long t0, t1;
  t0 = rd_mcycle();
  for (int i = 0; i < NITER; i++) {
    __asm__ volatile("li t0, 8\n\t"
                     "vsetvli t1, t0, e8, m1, ta, ma\n\t"
                     "vwmul.vv v4, v1, v2\n\t"
                     "vsetvli t1, t0, e16, m1, ta, ma\n\t"
                     "vwredsum.vs v3, v4, v0\n\t"
                     "li t0, 8\n\t"
                     "vsetvli t1, t0, e8, m1, ta, ma"
                     :
                     :
                     : "t0", "t1", "memory");
  }
  t1 = rd_mcycle();
  __asm__ volatile("vsetvli t1, x0, e32, m1, ta, ma\n\t vmv.x.s %0, v3" : "=r"(*res) : : "t1");
  return (t1 - t0) / NITER;
}

/* K3：标量展开。volatile 读防止 GCC 把 8 次乘加提到循环外（第一版就这样被优化掉了） */
static volatile signed char *pa = a;
static volatile signed char *pb = b;
static unsigned long k_scalar(unsigned long *res) {
  unsigned long t0, t1;
  int s = 0;
  t0 = rd_mcycle();
  for (int i = 0; i < NITER; i++) {
    s = 0;
    s += (int)pa[0] * (int)pb[0]; s += (int)pa[1] * (int)pb[1];
    s += (int)pa[2] * (int)pb[2]; s += (int)pa[3] * (int)pb[3];
    s += (int)pa[4] * (int)pb[4]; s += (int)pa[5] * (int)pb[5];
    s += (int)pa[6] * (int)pb[6]; s += (int)pa[7] * (int)pb[7];
  }
  t1 = rd_mcycle();
  *res = (unsigned long)s;
  return (t1 - t0) / NITER;
}

int kmain(void) {
  unsigned long r1 = 0, r2 = 0, r3 = 0;
  unsigned long c1, c2, c3;
  long n = 8;

  setup();
  puts_u("step:setup\n");

  /* 预热：避免把首次取指/译码的冷开销算进第一轮 */
  __asm__ volatile("vsetvli t1, %0, e8, m1, ta, ma\n\t .word 0xFA20A1D7" : : "r"(n) : "t1", "memory");

  c1 = k_vdot(&r1);    puts_u("step:k1\n");
  c2 = k_wmul(&r2);    puts_u("step:k2\n");
  c3 = k_scalar(&r3);  puts_u("step:k3\n");

  puts_u("=== A8 baseline candidates (NITER="); putdec(NITER); puts_u(", vl=8, VLEN=128) ===\n");
  pad("K1 vdot", 22);             puts_u("cyc/iter="); putdec(c1); puts_u("  result="); putdec(r1); puts_u("\n");
  pad("K2 vwmul+vwredsum", 22);   puts_u("cyc/iter="); putdec(c2); puts_u("  result="); putdec(r2); puts_u("\n");
  pad("K3 scalar unrolled", 22);  puts_u("cyc/iter="); putdec(c3); puts_u("  result="); putdec(r3); puts_u("\n");
  puts_u("all_results_equal="); puts_u((r1 == 204 && r2 == 204 && r3 == 204) ? "1" : "0"); puts_u("\n");
  puts_u("ratio K2/K1(x100)="); putdec(c2 * 100 / (c1 ? c1 : 1)); puts_u("\n");
  puts_u("RESULT: DONE\n");
  return 0;
}

