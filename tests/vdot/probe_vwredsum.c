/* 最小复现：vwmul.vv + vwredsum.vs 在"每块重设 vtype + 每块读回"的循环里卡死
 *
 * 每轮固定 7 条：vsetvli(e8) / vle8 / vle8 / vwmul / vsetvli(e16) / vwredsum / vsetvli(e32) / vmv.x.s
 * 每轮之前打一行 "iter N"，UART 停在哪个 N 就是第几轮卡住。
 * 4 轮足够；数据为 1..8，点积 = 204。
 */
#define UART_TX (*(volatile unsigned char *)0x40600004UL)
__asm__(".section .text\n.globl _start\n_start:\n li sp,0x80100000\n call kmain\n1:\n wfi\n j 1b\n");

static void putc_u(char c){UART_TX=(unsigned char)c;}
static void puts_u(const char*s){while(*s)putc_u(*s++);}
static void putdec(unsigned long v){char b[24];int n=0;if(!v){putc_u('0');return;}while(v){b[n++]=(char)('0'+v%10);v/=10;}while(n)putc_u(b[--n]);}

static signed char a[16] __attribute__((aligned(16)));
static signed char b[16] __attribute__((aligned(16)));

void kmain(void){
  __asm__ volatile("li t5,0x600\n\t csrs mstatus,t5":::"t5");
  for(int i=0;i<16;i++){a[i]=(signed char)(i+1);b[i]=(signed char)(i+1);}
  long n=8;
  __asm__ volatile("vsetvli t1,%0,e8,m1,ta,ma\n\t vle8.v v1,(%1)\n\t vle8.v v2,(%2)"
                   :: "r"(n),"r"(a),"r"(b):"t1","memory");
  puts_u("probe3:start dot=204\n");
  for(int k=0;k<4;k++){
    long r;
    puts_u("iter "); putdec(k); puts_u("\n");
    __asm__ volatile(
        "vsetvli t1, %1, e8, m1, ta, ma\n\t"
        "vle8.v v1, (%2)\n\t"
        "vle8.v v2, (%3)\n\t"
        "vwmul.vv v3, v1, v2\n\t"
        "vsetvli t1, %1, e16, m1, ta, ma\n\t"
        "vwredsum.vs v4, v3, v0\n\t"
        "vsetvli t1, x0, e32, m1, ta, ma\n\t"
        "vmv.x.s %0, v4"
        : "=&r"(r)
        : "r"(n), "r"(a), "r"(b)
        : "t1", "memory");
    puts_u("  got "); putdec((unsigned long)r); puts_u("\n");
  }
  puts_u("probe3:done\n");
}
