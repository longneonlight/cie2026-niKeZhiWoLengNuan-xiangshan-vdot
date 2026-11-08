/* vdot.vv 随机对拍：100 组随机 int8 输入，覆盖 vl = 1..16
 *
 * 期望值在**软件里独立算**（不复用 RTL/NEMU 的任何逻辑）：
 *   acc = Σ_{i<min(vl,8)} (int8)a[i] * (int8)b[i]   （32 位模运算）
 *   vd[31:0] = acc，vd[127:32] = 0
 *
 * 三个平台跑同一个二进制，比对 **fails 与 checksum** 即可：
 *   - NEMU standalone
 *   - RTL --no-diff
 *   - RTL --diff（difftest）
 *
 * 随机数用固定种子的 xorshift32，保证可复现。
 */
#define UART_TX (*(volatile unsigned char *)0x40600004UL)

#define NCASE 100

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
    /* 直接用 UART MMIO 打印 "T<cause8><epc8>"，不调用 C 函数、不写 .bss */
    "  csrr s0, mcause\n"
    "  csrr s1, mepc\n"
    "  csrw 0x008, zero\n"
    "  li s2, 0x40600004\n"
    "  li s3, 84\n"              /* 'T' */
    "  sb s3, 0(s2)\n"
    "  li s3, 61\n"              /* '=' */
    "  sb s3, 0(s2)\n"
    "  li t0, 28\n"
    "2:\n"
    "  srl t1, s0, t0\n"
    "  andi t1, t1, 0xF\n"
    "  li t2, 10\n"
    "  blt t1, t2, 3f\n"
    "  addi t1, t1, 87\n"        /* 'a'-10 */
    "  j 4f\n"
    "3:\n"
    "  addi t1, t1, 48\n"        /* '0' */
    "4:\n"
    "  sb t1, 0(s2)\n"
    "  addi t0, t0, -4\n"
    "  bge t0, zero, 2b\n"
    "  li s3, 32\n"
    "  sb s3, 0(s2)\n"
    "  li t0, 28\n"
    "5:\n"
    "  srl t1, s1, t0\n"
    "  andi t1, t1, 0xF\n"
    "  li t2, 10\n"
    "  blt t1, t2, 6f\n"
    "  addi t1, t1, 87\n"
    "  j 7f\n"
    "6:\n"
    "  addi t1, t1, 48\n"
    "7:\n"
    "  sb t1, 0(s2)\n"
    "  addi t0, t0, -4\n"
    "  bge t0, zero, 5b\n"
    "  li s3, 10\n"
    "  sb s3, 0(s2)\n"
    "  addi s1, s1, 4\n"
    "  csrw mepc, s1\n"
    "  mret\n");

volatile unsigned g_trapped, g_cause, g_epc;

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

static unsigned rng_state = 0x12345678u;
static unsigned xorshift32(void) {
  unsigned x = rng_state;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  rng_state = x;
  return x;
}

static signed char a[16], b[16];
static unsigned char out[16];
static unsigned fails = 0;
static unsigned checksum = 0x811c9dc5u;   /* FNV-1a 初值 */

/* 用一条 vdot 处理当前 a/b，返回 vd[31:0]；同时把 vd 存到 out[] */
static unsigned do_vdot(unsigned vl) {
  for (int i = 0; i < 16; i++) out[i] = 0xA5;
  __asm__ volatile(
      /* 先用 vl=16 把整个寄存器装满：vdot 取前 8 个字节，这 8 个必须都是「程序
       * 显式写过」的值，否则 vl<8 时结果会依赖 tail-agnostic 的未定义字节。 */
      "li t0, 16\n\t"
      "vsetvli t1, t0, e8, m1, ta, ma\n\t"
      "vle8.v v1, (%0)\n\t"
      "vle8.v v2, (%1)\n\t"
      /* 再按参数设 vl —— vdot 不看 vl，这一步纯粹是为了验证「vl 不影响结果」 */
      "vsetvli t1, %3, e8, m1, ta, ma\n\t"
      ".word 0xFA20A1D7\n\t"          /* vdot v3, v1, v2 */
      "li t0, 16\n\t"
      "vsetvli t1, t0, e8, m1, ta, ma\n\t"
      "vse8.v v3, (%2)\n\t"
      :
      : "r"(a), "r"(b), "r"(out), "r"(vl)
      : "t0", "t1", "memory");
  return (unsigned)out[0] | ((unsigned)out[1] << 8) |
         ((unsigned)out[2] << 16) | ((unsigned)out[3] << 24);
}

void kmain(void) {
  unsigned shown = 0;

  __asm__ volatile("li t5, 0x600\n\t csrs mstatus, t5" ::: "t5");
  /* 注意：这里**故意不碰 mnstatus(0x744)**——NEMU 复位时 NMIE=0、硬件为 1，
   * 一读就会在 difftest 下报 CSR 不一致。本程序不应产生任何 trap，
   * 若真的 trap，NEMU 会直接报 double trap，本身就是失败信号。 */

  puts_u("vdot.vv random tests: ");
  putdec(NCASE);
  puts_u(" cases, vl=1..16\n");

  for (unsigned c = 0; c < NCASE; c++) {
    unsigned vl = (xorshift32() % 16u) + 1u;      /* 1..16 */
    for (int i = 0; i < 16; i++) {
      a[i] = (signed char)(xorshift32() & 0xFFu);
      b[i] = (signed char)(xorshift32() & 0xFFu);
    }

    /* 软件独立计算期望值（32 位模运算，避免有符号溢出 UB）
     * 题面 for i in range(8)：固定 8 个元素，与 vl 无关 */
    unsigned acc = 0;
    for (unsigned i = 0; i < 8; i++)
      acc += (unsigned)((int)a[i] * (int)b[i]);

    unsigned got = do_vdot(vl);

    if (g_trapped) {
      fails++;
      if (shown < 5) {
        shown++;
        puts_u("  TRAP c="); putdec(c);
        puts_u(" vl="); putdec(vl);
        puts_u(" cause="); puthex8(g_cause);
        puts_u(" epc="); puthex8(g_epc);
        puts_u("\n");
      }
      g_trapped = 0;
    }

    int tail_bad = 0;
    for (int i = 4; i < 16; i++) if (out[i] != 0) tail_bad = 1;

    if (got != acc || tail_bad) {
      fails++;
      if (shown < 5) {
        shown++;
        puts_u("  FAIL c="); putdec(c);
        puts_u(" vl="); putdec(vl);
        puts_u(" got="); puthex8(got);
        puts_u(" want="); puthex8(acc);
        if (tail_bad) puts_u(" tail!=0");
        puts_u("\n");
      }
    }

    /* FNV-1a over (vl, got) */
    checksum ^= (vl & 0xFFu); checksum *= 16777619u;
    checksum ^= (got & 0xFFu); checksum *= 16777619u;
    checksum ^= ((got >> 8) & 0xFFu); checksum *= 16777619u;
    checksum ^= ((got >> 16) & 0xFFu); checksum *= 16777619u;
    checksum ^= ((got >> 24) & 0xFFu); checksum *= 16777619u;
  }

  puts_u("fails="); putdec(fails);
  puts_u("  checksum="); puthex8(checksum);
  puts_u("\n");
  puts_u(fails == 0 ? "RESULT: PASS\n" : "RESULT: FAIL\n");
}
