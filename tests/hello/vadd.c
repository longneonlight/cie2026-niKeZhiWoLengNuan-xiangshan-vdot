/* vadd.vv 功能验证程序
 *
 * 目的：在 NEMU（参考模型）或香山 RTL 仿真器上，验证向量加法指令 vadd.vv 的语义
 *       —— 逐元素相加，元素之间互不影响。
 *
 * 做法：
 *   1. 用 vsetvli 设定 SEW=8、LMUL=1、vl=16（此时 VLMAX 也等于 16）；
 *   2. 用 vmv.v.x 把 0x01 广播到 v1、v2 的全部 16 个 byte lane；
 *   3. 执行 vadd.vv v3, v1, v2；
 *   4. 用 vse8.v 把 v3 存回内存，逐字节检查是否都等于 0x02。
 *
 * 裸机运行的两个前提（缺一不可）：
 *   1. 在汇编入口先设 sp —— C 函数序言会立刻用 sp 保存寄存器，
 *      不设会往 0xffffffffffffffec 写数据，报 invalid mem write；
 *   2. 把 mstatus.VS 置成 11 —— VS=Off 时硬件把任何向量指令都判为非法。
 *
 * 编译：riscv64-linux-gnu-gcc -march=rv64gcv -mabi=lp64d -nostdlib -nostartfiles \
 *         -ffreestanding -O2 -fno-pic -fno-pie -mcmodel=medany -T link.ld \
 *         vadd.c -o vadd.elf
 *       riscv64-linux-gnu-objcopy -O binary vadd.elf vadd.bin
 * 运行：~/bin/nemu-standalone -b vadd.bin          （NEMU）
 *       build/emu -i vadd.bin --no-diff            （香山 RTL）
 *
 * 期望输出：
 *   vadd.vv test
 *   v3 bytes: 02 02 02 02 02 02 02 02
 *   RESULT: PASS (all lanes == 0x02)
 */

/* UART16550 的发送寄存器（MMIO 地址 0x40600004）。往这里写一个字节就会打印出去 */
#define UART_TX (*(volatile unsigned char *)0x40600004UL)

/* 裸机入口：没有 C 运行时，_start 必须自己设栈并调用 kmain */
__asm__(
    ".section .text\n"
    ".globl _start\n"
    "_start:\n"
    "  li sp, 0x80100000\n"   /* 栈放在 0x80100000：避开代码段（0x80000000 起）与数据段 */
    "  call kmain\n"          /* 进入 C 主函数 */
    "1:\n"
    "  wfi\n"                 /* 结束后原地等待中断；仿真器靠外部 timeout 或轮询日志退出 */
    "  j 1b\n");

/* 往 UART 写一个字符 */
static void putc_u(char c) { UART_TX = (unsigned char)c; }

/* 打印一个以 0 结尾的字符串 */
static void puts_u(const char *s) { while (*s) putc_u(*s++); }

/* 把一个字节按两位十六进制打印（小写） */
static void put_hex2(unsigned char v) {
  const char *d = "0123456789abcdef";
  putc_u(d[(v >> 4) & 0xF]);   /* 高 4 位 */
  putc_u(d[v & 0xF]);          /* 低 4 位 */
}

void kmain(void) {
  unsigned char buf[16];       /* 存放 v3 的 16 个字节，用普通内存即可 */
  for (int i = 0; i < 16; ++i) buf[i] = 0xAA;   /* 先填脏值：若指令没写回，这里仍是 0xAA */

  asm volatile(
      "li t5, 0x600\n\t"        /* 0x600 = 二进制 0110_0000_0000：把 mstatus.VS 两位置成 11 */
      "csrs mstatus, t5\n\t"    /* 写 mstatus：不置 VS 的话，下面第一条向量指令就非法 */
      "li t0, 16\n\t"           /* 期望的 vl = 16 */
      "vsetvli t1, t0, e8, m1, ta, ma\n\t"  /* SEW=8、LMUL=1、tail/mask 均为 agnostic */
      "li t2, 0x0101010101010101\n\t"       /* 64 位寄存器装不下 128 位，先造 8 个 0x01 */
      "vmv.v.x v1, t2\n\t"      /* 把 t2 广播到 v1 的每个元素 → 全 1 */
      "vmv.v.x v2, t2\n\t"      /* 同上 → v2 全 1 */
      "vadd.vv v3, v1, v2\n\t"  /* 被测指令：逐字节相加，v3 = 0x02 × 16 */
      "vse8.v v3, (%0)\n\t"     /* 把 v3 的 16 个字节存到 buf，供 C 代码检查 */
      :
      : "r"(buf)                /* %0 = buf 的地址 */
      : "t0", "t1", "t2", "t5", "memory");

  puts_u("vadd.vv test\n");

  /* 打印前 8 个字节，方便肉眼确认 */
  puts_u("v3 bytes: ");
  for (int i = 0; i < 8; ++i) {
    if (i) putc_u(' ');
    put_hex2(buf[i]);
  }
  puts_u("\n");

  /* 逐字节检查：16 个 lane 应该全是 0x02 */
  int ok = 1;
  for (int i = 0; i < 16; ++i)
    if (buf[i] != 0x02) ok = 0;
  puts_u(ok ? "RESULT: PASS (all lanes == 0x02)\n"
            : "RESULT: FAIL\n");
}
