/* hello xiangshan —— 阶段一环境验证程序
 *
 * 目的：用一条最小程序证明三件事都通了——
 *   1. 处理器能复位到 0x80000000 并开始取指执行；
 *   2. 程序能访问 MMIO 设备（这里是最简单的 UART 发送寄存器）；
 *   3. 输出能被外部看到（NEMU 直接打印到终端，RTL 仿真器写进日志）。
 * 同一个镜像会在 NEMU 与香山 RTL 仿真器上各跑一遍，两端输出应当逐字符相同。
 *
 * 内存与设备地址（来自 xs-env 默认配置）：
 *   CONFIG_MBASE        = 0x80000000   程序装载与复位入口
 *   CONFIG_UARTLITE_MMIO = 0x40600000  串口寄存器基址，发送口偏移 0x4
 *
 * 编译：见同目录 02-命令清单.txt；本文件不依赖任何库，-nostdlib 即可
 */
#define UART_TX (*(volatile unsigned char *)0x40600004UL)

/* 要打印的内容。团队标识写在字符串里，IP 是本虚拟机的内网地址 */
static const char msg[] =
    "hello xiangshan, I am niKeZhiWoLengNuan, 192.168.157.50\n";

/* 裸机入口：没有 C 运行时，_start 直接就是一个循环 */
void _start(void) {
  /* 逐字节往 UART 发送寄存器写，写一个字节就打印一个字符 */
  for (const char *p = msg; *p; ++p) {
    UART_TX = (unsigned char)*p;
  }
  /* 打印完原地等待中断。仿真器不会自己退出，外部用 timeout 或轮询日志结束 */
  for (;;) {
    __asm__ volatile("wfi");
  }
}
