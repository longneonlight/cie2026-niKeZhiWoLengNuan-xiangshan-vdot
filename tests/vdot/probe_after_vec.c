/* probe6：向量指令之后到底能不能继续执行？
 *   S -> vle8 -> l -> vwmul -> (RAM 写入) -> 空转 -> m -> D
 * 如果 m 出现：vwmul 本身没问题，之前卡住的是"向量指令后紧跟的 UART store"。
 * 如果 m 不出现：卡在 vwmul 本身。
 */
#define UART_TX (*(volatile unsigned char *)0x40600004UL)
__asm__(".section .text\n.globl _start\n_start:\n li sp,0x80100000\n call kmain\n1:\n wfi\n j 1b\n");
static void putc_u(char c){UART_TX=(unsigned char)c;}
static void puts_u(const char*s){while(*s)putc_u(*s++);}

static signed char a[16] __attribute__((aligned(16)));
static signed char b[16] __attribute__((aligned(16)));
static volatile int sink;

void kmain(void){
  __asm__ volatile("li t5,0x600\n\t csrs mstatus,t5":::"t5");
  for(int i=0;i<16;i++){a[i]=(signed char)(i+1);b[i]=(signed char)(i+1);}
  long n=8;
  puts_u("S");
  __asm__ volatile("vsetvli t1,%0,e8,m1,ta,ma\n\t vle8.v v1,(%1)\n\t vle8.v v2,(%2)"
                   :: "r"(n),"r"(a),"r"(b):"t1","memory");
  puts_u("l");
  __asm__ volatile("vsetvli t1,%0,e8,m1,ta,ma\n\t vwmul.vv v3,v1,v2" :: "r"(n) : "t1","memory");
  sink = 12345;                       /* RAM 写，不碰 UART */
  for (volatile int i = 0; i < 2000; i++) { }   /* 空转，给向量单元时间 */
  puts_u("m");
  puts_u("D");
  for(;;) __asm__ volatile("wfi");
}
