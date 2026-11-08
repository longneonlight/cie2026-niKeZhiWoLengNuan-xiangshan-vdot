/* OPMVV 通路探针：先跑 vmul.vv（已知指令），再跑 vdot（自定义）
 * 用来区分「OPMVV 整体不可用」还是「只有 vdot 译码不认」
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
  __asm__ volatile("li t5, 0x600\n\t csrs mstatus, t5" ::: "t5");
  for (int i = 0; i < 16; i++) { a[i] = (signed char)(i + 1); b[i] = (signed char)(i + 1); }

  puts_u("probe start\n");

  /* ---- 1) vmul.vv：已知的 OPMVV 指令 ---- */
  __asm__ volatile(
      "li t0, 8\n\t"
      "vsetvli t1, t0, e8, m1, ta, ma\n\t"
      "vle8.v v1, (%0)\n\t"
      "vle8.v v2, (%1)\n\t"
      "vmul.vv v3, v1, v2\n\t"
      "vse8.v v3, (%2)\n\t"
      :
      : "r"(a), "r"(b), "r"(out)
      : "t0", "t1", "memory");
  puts_u("vmul.vv  ok, v3[0..3] = ");
  puthex8((unsigned)out[0] | ((unsigned)out[1] << 8) |
          ((unsigned)out[2] << 16) | ((unsigned)out[3] << 24));
  puts_u("\n");

  /* ---- 2) vdot：自定义指令 ---- */
  __asm__ volatile(
      "li t0, 8\n\t"
      "vsetvli t1, t0, e8, m1, ta, ma\n\t"
      "vle8.v v1, (%0)\n\t"
      "vle8.v v2, (%1)\n\t"
      ".word 0xFA20A1D7\n\t"
      "vse8.v v3, (%2)\n\t"
      :
      : "r"(a), "r"(b), "r"(out)
      : "t0", "t1", "memory");
  puts_u("vdot     ok, v3[0..3] = ");
  puthex8((unsigned)out[0] | ((unsigned)out[1] << 8) |
          ((unsigned)out[2] << 16) | ((unsigned)out[3] << 24));
  puts_u("\nRESULT: DONE\n");
}
