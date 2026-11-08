/* vdot 性能：两套口径的实测
 *
 *   口径 A（latency）：vtype 已经就位时，单次"内核"的 mcycle 差值（csrr 本身约 1~2 拍，
 *                      所以这是"含测量开销"的上界，报告里要注明）
 *   口径 B（throughput）：循环 N 次的总周期 / N（含循环开销与必要的 vsetvli）
 *
 * 基线内核：vsetvli(e8) + vwmul.vv + vsetvli(e16) + vwredsum.vs   （4 条，其中 2 条 vsetvli）
 *            循环体另有一条 li（给 vsetvli 传 VL），共 5 条；不再在末尾"还原 vtype"——
 *            下一轮的 vsetvli(e8) 本来就会重设，末尾再设一次是多余的一条指令。
 * vdot 内核：vsetvli(e8) + vdot.vv                                （2 条，其中 1 条 vsetvli）
 *
 * 编译：必须加 -fno-pic -fno-pie -mcmodel=medany，否则 `la` 会生成 GOT 取址，
 *       在没有加载器初始化 GOT 的裸机环境里会把 mtvec 之类写成 0（2026-10-29 踩过）。
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
static void puthex8(unsigned v) {
  const char *d = "0123456789abcdef";
  for (int i = 28; i >= 0; i -= 4) putc_u(d[(v >> i) & 0xF]);
}

static inline unsigned long rd_mcycle(void) {
  unsigned long x;
  __asm__ volatile("csrr %0, mcycle" : "=r"(x));
  return x;
}

static signed char a[16], b[16];
static unsigned char out[16];

#ifndef NITER
#define NITER 100
#endif

/* 只做一次：开 VS、装数据、把 vtype 设成 e8/m1 */
static void setup(void) {
  __asm__ volatile("li t5, 0x600\n\t csrs mstatus, t5" ::: "t5");
  for (int i = 0; i < 16; i++) { a[i] = (signed char)(i + 1); b[i] = (signed char)(i + 1); }
  __asm__ volatile(
      "li t0, 8\n\t"
      "vsetvli t1, t0, e8, m1, ta, ma\n\t"
      "vle8.v v1, (%0)\n\t"
      "vle8.v v2, (%1)\n\t"
      :
      : "r"(a), "r"(b)
      : "t0", "t1", "memory");
}

/* 口径 A：vtype 已就位的单内核
 * vdot 侧刻意把 vsetvli 挪到计时区外，测的就是"一条 vdot 指令"本身的代价；
 * 基线侧的 vwredsum 必须工作在 e16，所以 vsetvli 是它不可分割的一部分。 */
static unsigned long lat_vdot(void) {
  unsigned long t0, t1, sum = 0;
  for (int i = 0; i < NITER; i++) {
    t0 = rd_mcycle();
    __asm__ volatile(".word 0xFA20A1D7" ::: "memory");
    t1 = rd_mcycle();
    sum += (t1 - t0);
  }
  return sum / NITER;
}

static unsigned long lat_baseline(void) {
  unsigned long t0, t1, sum = 0;
  for (int i = 0; i < NITER; i++) {
    t0 = rd_mcycle();
    __asm__ volatile(
        "li t0, 8\n\t"
        "vsetvli t1, t0, e8, m1, ta, ma\n\t"
        "vwmul.vv v4, v1, v2\n\t"
        "vsetvli t1, t0, e16, m1, ta, ma\n\t"
        "vwredsum.vs v3, v4, v0\n\t"
        "li t0, 8\n\t"
        "vsetvli t1, t0, e8, m1, ta, ma\n\t"
        ::: "t0", "t1", "memory");
    t1 = rd_mcycle();
    sum += (t1 - t0);
  }
  return sum / NITER;
}

/* 口径 B：循环 N 次的总周期（内核 + 循环开销） */
static unsigned long thr_vdot(void) {
  unsigned long t0, t1;
  t0 = rd_mcycle();
  for (int i = 0; i < NITER; i++) {
    __asm__ volatile("li t0, 8\n\t vsetvli t1, t0, e8, m1, ta, ma\n\t .word 0xFA20A1D7"
                     ::: "t0", "t1", "memory");
  }
  t1 = rd_mcycle();
  return (t1 - t0) / NITER;
}

static unsigned long thr_baseline(void) {
  unsigned long t0, t1;
  t0 = rd_mcycle();
  for (int i = 0; i < NITER; i++) {
    __asm__ volatile(
        "li t0, 8\n\t"
        "vsetvli t1, t0, e8, m1, ta, ma\n\t"
        "vwmul.vv v4, v1, v2\n\t"
        "vsetvli t1, t0, e16, m1, ta, ma\n\t"
        "vwredsum.vs v3, v4, v0\n\t"
        ::: "t0", "t1", "memory");
  }
  t1 = rd_mcycle();
  return (t1 - t0) / NITER;
}

/* 口径 C：背靠背吞吐（把 N 条内核指令展开在两次 mcycle 读取之间，无循环开销）
 * 这是最能反映"功能单元发射间隔"的数字：vdot 侧就是 1 条指令；
 * 基线侧是它不可分割的 4 条序列。 */
static unsigned long b2b_vdot(void) {
  unsigned long t0, t1;
  t0 = rd_mcycle();
  __asm__ volatile(
      "li t0, 8\n\t"
      "vsetvli t1, t0, e8, m1, ta, ma\n\t"   /* 必须先把 vtype 设回 e8，否则 vdot 非法 */
      ".rept 100\n\t"
      ".word 0xFA20A1D7\n\t"
      ".endr\n\t" ::: "t0", "t1", "memory");
  t1 = rd_mcycle();
  return (t1 - t0) / 100;
}

static unsigned long b2b_baseline(void) {
  unsigned long t0, t1;
  t0 = rd_mcycle();
  __asm__ volatile(
      "li t0, 8\n\t"                     /* VL 基准，放在重复块外面 */
      ".rept 100\n\t"
      "vsetvli t1, t0, e8, m1, ta, ma\n\t"
      "vwmul.vv v4, v1, v2\n\t"
      "vsetvli t1, t0, e16, m1, ta, ma\n\t"
      "vwredsum.vs v3, v4, v0\n\t"
      ".endr\n\t"
      "vsetvli t1, t0, e8, m1, ta, ma\n\t"   /* 还原 e8，避免影响后续测量 */
      ::: "t0", "t1", "memory");
  t1 = rd_mcycle();
  return (t1 - t0) / 100;
}

/* 候选基线 3（2026-10-31 补）：标量展开。
 * 用 volatile 指针读，防止 GCC 发现循环不变式后把 8 次乘加整个提到循环外
 * （第一版没加 volatile，实测只剩 2 拍/次，完全是假象）。 */
static volatile signed char *sp_a = a;
static volatile signed char *sp_b = b;
static unsigned long thr_scalar(void) {
  unsigned long t0, t1;
  int s = 0;
  t0 = rd_mcycle();
  for (int i = 0; i < NITER; i++) {
    s  = (int)sp_a[0] * (int)sp_b[0];
    s += (int)sp_a[1] * (int)sp_b[1];
    s += (int)sp_a[2] * (int)sp_b[2];
    s += (int)sp_a[3] * (int)sp_b[3];
    s += (int)sp_a[4] * (int)sp_b[4];
    s += (int)sp_a[5] * (int)sp_b[5];
    s += (int)sp_a[6] * (int)sp_b[6];
    s += (int)sp_a[7] * (int)sp_b[7];
  }
  t1 = rd_mcycle();
  (void)s;
  return (t1 - t0) / NITER;
}

void kmain(void) {
  setup();
  puts_u("vdot perf (NITER="); putdec(NITER); puts_u(")\n");

  /* 先验算一次，确认两条路径结果一致 */
  __asm__ volatile("li t0, 8\n\t vsetvli t1, t0, e8, m1, ta, ma\n\t .word 0xFA20A1D7\n\t vse8.v v3, (%0)"
                   :: "r"(out) : "t0", "t1", "memory");
  puts_u("vdot result = ");
  puthex8((unsigned)out[0] | ((unsigned)out[1] << 8) | ((unsigned)out[2] << 16) | ((unsigned)out[3] << 24));
  puts_u("\n");

  unsigned long la = lat_baseline(), lb = lat_vdot();
  unsigned long ta = thr_baseline(), tb = thr_vdot();

  puts_u("[A] latency  : baseline="); putdec(la); puts_u("  vdot="); putdec(lb);
  puts_u("  cyc/次  速度比(x100)="); putdec(la * 100 / (lb ? lb : 1)); puts_u("\n");
  puts_u("[B] throughput: baseline="); putdec(ta); puts_u("  vdot="); putdec(tb);
  puts_u("  cyc/次  速度比(x100)="); putdec(ta * 100 / (tb ? tb : 1)); puts_u("\n");

  unsigned long ts = thr_scalar();
  puts_u("[B'] scalar   : "); putdec(ts); puts_u(" cyc/次");
  puts_u("  相对 vdot(x100)="); putdec(ts * 100 / (tb ? tb : 1));
  puts_u("  相对 baseline(x100)="); putdec(ts * 100 / (ta ? ta : 1)); puts_u("\n");

  unsigned long ca = b2b_baseline(), cb = b2b_vdot();
  puts_u("[C] b2b      : baseline="); putdec(ca); puts_u("  vdot="); putdec(cb);
  puts_u("  cyc/次  速度比(x100)="); putdec(ca * 100 / (cb ? cb : 1)); puts_u("\n");
  puts_u("RESULT: DONE\n");
}
