/* vdot.vv 波形用最小程序（阶段三复核：抓一次 vdot 的 FU 输入/输出）
 *
 * 目的：只跑一条 vdot，让波形窗口尽量小；结果经 UART 打印，便于确认跑对了。
 * 期望：vs1 = vs2 = 0x0101…01（16 字节），8 个 int8 乘积各为 1，结果 = 8 = 0x00000008。
 *
 * 编码：vdot v3, v1, v2 = 0xFA20A1D7（用 .word 嵌入，汇编器不认识自定义助记符）
 */
#define UART_TX (*(volatile unsigned char *)0x40600004UL)

__asm__(
    ".section .text\n"
    ".globl _start\n"
    "_start:\n"
    "  li sp, 0x80100000\n"
    "  call kmain\n"
    "1:\n  wfi\n  j 1b\n");

static void putc_u(char c) { UART_TX = (unsigned char)c; }
static void puts_u(const char *s) { while (*s) putc_u(*s++); }
static void hex8(unsigned v) {
  const char *d = "0123456789abcdef";
  for (int i = 28; i >= 0; i -= 4) putc_u(d[(v >> i) & 0xF]);
}

static unsigned char out[16];

void kmain(void) {
  __asm__ volatile(
      "li t5, 0x600\n\t"          /* mstatus.VS = 11（否则向量指令非法） */
      "csrs mstatus, t5\n\t"
      "li t0, 16\n\t"
      "vsetvli t1, t0, e8, m1, ta, ma\n\t"
      "li t2, 0x0101010101010101\n\t"
      "vmv.v.x v1, t2\n\t"
      "vmv.v.x v2, t2\n\t"
      ".word 0xFA20A1D7\n\t"      /* vdot v3, v1, v2 */
      "vse8.v v3, (%0)\n\t"
      :
      : "r"(out)
      : "t0", "t1", "t2", "t5", "memory");

  puts_u("vdot: ");
  for (int i = 0; i < 4; ++i) {
    if (i) putc_u(' ');
    hex8(out[i]);
  }
  puts_u(out[0] == 8 ? "\nRESULT: PASS\n" : "\nRESULT: FAIL\n");
  for (;;) __asm__ volatile("wfi");
}
