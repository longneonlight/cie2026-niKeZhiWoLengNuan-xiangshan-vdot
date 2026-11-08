/* 探针：vdot 之后紧跟 vmv.x.s（同一 vd）是否会挂死 —— 用于定位 GEMV 内核卡住的原因 */
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
  puts_u("probe:start\n");
  unsigned long acc=0;
  for(int i=0;i<10;i++){
    long r;
    __asm__ volatile("vsetvli t1,%1,e8,m1,ta,ma\n\t"
                     ".word 0xFA20A1D7\n\t"
                     "vmv.x.s %0,v3"
                     : "=r"(r) : "r"(n) : "t1","memory");
    acc += (unsigned long)r;
  }
  puts_u("probe:after-loop acc="); putdec(acc); puts_u("\n");
  puts_u("RESULT: DONE\n");
}
