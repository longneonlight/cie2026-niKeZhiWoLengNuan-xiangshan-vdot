/* NEMU 上的 vdot.vv 首次执行测试
 * 编码：funct6=111110 vm=1 vs2=2 vs1=1 funct3=010 vd=3 opcode=1010111
 *       = 0xFA20A1D7  (vdot v3, v1, v2)
 * 期望：a=b=[1..16] → sum(i^2) = 1496 = 0x000005D8
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

void kmain(void) {
  for (int i = 0; i < 16; i++) { a[i] = (signed char)(i + 1); b[i] = (signed char)(i + 1); }
  for (int i = 0; i < 16; i++) out[i] = 0xAA;

  asm volatile(
      "li t5, 0x600\n\t"
      "csrs mstatus, t5\n\t"
      "li t0, 16\n\t"
      "vsetvli t1, t0, e8, m1, ta, ma\n\t"
      "vle8.v v1, (%0)\n\t"
      "vle8.v v2, (%1)\n\t"
      ".word 0xFA20A1D7\n\t"      /* vdot v3, v1, v2 */
      "vse8.v v3, (%2)\n\t"
      :
      : "r"(a), "r"(b), "r"(out)
      : "t0", "t1", "t5", "memory");

  unsigned got = (unsigned)out[0] | ((unsigned)out[1] << 8) |
                 ((unsigned)out[2] << 16) | ((unsigned)out[3] << 24);

  puts_u("vdot.vv on NEMU\n");
  puts_u("out  = "); puthex8(got); puts_u("\n");
  puts_u("want = "); puthex8(1496); puts_u("\n");
  puts_u(got == 1496 ? "RESULT: PASS\n" : "RESULT: FAIL\n");
}
