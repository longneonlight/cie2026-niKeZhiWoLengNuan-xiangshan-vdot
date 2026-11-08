/* vadd_lane_isolation.c —— 验证 SEW=8 时 8 位 lane 之间不会互相进位
 *
 * 思路：构造"某一条 lane 刚好溢出、相邻 lane 有非零值"的输入。
 *   如果进位会跨字节传播，相邻字节就会比期望值多 1；如果不会，结果就与逐 lane 手算一致。
 *
 * 四个用例：
 *   A  lane0 = 0xFF + 0x01（溢出），其余 lane 全 0            → 期望 out 全 00
 *      若进位串到 lane1：out[1] 会变成 01
 *   B  lane1 = 0xFF + 0x01（溢出），lane0 与 lane2 都有值      → 期望 out 全 00
 *      若进位串到 lane2：out[2] 会变成 01
 *   C  16 个 lane 同时 0xFF + 0x01                            → 期望 out 全 00
 *   D  常规用例：全 0x01 + 全 0x01                            → 期望 out 全 02（证明加法确实生效）
 *
 * 裸机前置：入口设 sp；写 mstatus.VS = 11。
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
static void put_hex2(unsigned char v) {
  const char *d = "0123456789abcdef";
  putc_u(d[(v >> 4) & 0xF]);
  putc_u(d[v & 0xF]);
}

/* 用例数据：全部放在 .rodata，随 flat binary 一起加载 */
static const unsigned char A_A[16] = {0xFF, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const unsigned char A_B[16] = {0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static const unsigned char B_A[16] = {0x11, 0xFF, 0x22, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const unsigned char B_B[16] = {0x11, 0x01, 0x22, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static const unsigned char C_A[16] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static const unsigned char C_B[16] = {
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01};

static const unsigned char D_A[16] = {
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01};
static const unsigned char D_B[16] = {
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01};

/* 一次 vadd.vv：v1 = a，v2 = b，结果存到 out */
static void vadd16(const unsigned char *a, const unsigned char *b,
                   unsigned char *out) {
  asm volatile(
      "li t5, 0x600\n\t"                 /* mstatus.VS = 11 */
      "csrs mstatus, t5\n\t"
      "li t0, 16\n\t"                    /* vl = 16 */
      "vsetvli t1, t0, e8, m1, ta, ma\n\t"  /* SEW=8, LMUL=1 */
      "vle8.v  v1, (%0)\n\t"             /* v1 = a[0..15] */
      "vle8.v  v2, (%1)\n\t"             /* v2 = b[0..15] */
      "vadd.vv v3, v1, v2\n\t"           /* v3 = v1 + v2，逐字节 */
      "vse8.v  v3, (%2)\n\t"             /* out[0..15] = v3 */
      :
      : "r"(a), "r"(b), "r"(out)
      : "t0", "t1", "t5", "memory");
}

static void show(const char *name, const unsigned char *in, const unsigned char *out) {
  puts_u(name);
  puts_u("  in[0..3]=");
  for (int i = 0; i < 4; ++i) { if (i) putc_u(' '); put_hex2(in[i]); }
  puts_u("  out=");
  for (int i = 0; i < 16; ++i) { if (i) putc_u(' '); put_hex2(out[i]); }
  puts_u("\n");
}

void kmain(void) {
  unsigned char out[16];

  vadd16(A_A, A_B, out);
  show("A lane0 溢出", A_A, out);

  vadd16(B_A, B_B, out);
  show("B lane1 溢出", B_A, out);

  vadd16(C_A, C_B, out);
  show("C 全 lane 溢出", C_A, out);

  vadd16(D_A, D_B, out);
  show("D 常规 01+01", D_A, out);
}
