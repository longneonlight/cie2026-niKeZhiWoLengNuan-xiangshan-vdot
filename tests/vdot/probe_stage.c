/* probe5：把 7 条向量序列拆成 4 段，每段之间打一个字符，定位卡在哪一段
 *   S -> l(vle8) -> m(vwmul) -> r(vwredsum) -> v(vmv.x.s) -> D
 */
#define UART_TX (*(volatile unsigned char *)0x40600004UL)
__asm__(".section .text\n.globl _start\n_start:\n li sp,0x80100000\n call kmain\n1:\n wfi\n j 1b\n");
static void putc_u(char c){UART_TX=(unsigned char)c;}
static void puts_u(const char*s){while(*s)putc_u(*s++);}

static signed char a[16] __attribute__((aligned(16)));
static signed char b[16] __attribute__((aligned(16)));

void kmain(void){
  __asm__ volatile("li t5,0x600\n\t csrs mstatus,t5":::"t5");
  for(int i=0;i<16;i++){a[i]=(signed char)(i+1);b[i]=(signed char)(i+1);}
  long n=8;
  puts_u("S");
  __asm__ volatile("vsetvli t1,%0,e8,m1,ta,ma\n\t vle8.v v1,(%1)\n\t vle8.v v2,(%2)\n\t vmv.v.i v0,0"
                   :: "r"(n),"r"(a),"r"(b):"t1","memory");
  puts_u("l");
  __asm__ volatile("vsetvli t1,%0,e8,m1,ta,ma\n\t vwmul.vv v3,v1,v2" :: "r"(n) : "t1","memory");
  puts_u("m");
  __asm__ volatile("vsetvli t1,%0,e16,m1,ta,ma\n\t vwredsum.vs v4,v3,v0" :: "r"(n) : "t1","memory");
  puts_u("r");
  long r;
  __asm__ volatile("vsetvli t1,x0,e32,m1,ta,ma\n\t vmv.x.s %0,v4" : "=r"(r) : : "t1");
  puts_u("v");
  puts_u("D");
  (void)r;
  for(;;) __asm__ volatile("wfi");
}
