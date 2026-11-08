#define UART_TX (*(volatile unsigned char *)0x40600004UL)
__asm__(
    ".section .text\n.globl _start\n_start:\n"
    "  li sp, 0x80100000\n"
    "  la t0, trap_entry\n  csrw mtvec, t0\n"
    "  call kmain\n1:\n  wfi\n  j 1b\n"
    "  .align 4\ntrap_entry:\n"
    "  csrr t0, mcause\n  csrw 0x008, zero\n"
    "  csrr t0, mepc\n  addi t0, t0, 4\n  csrw mepc, t0\n  mret\n");
static void putc_u(char c){ UART_TX=(unsigned char)c; }
static void puts_u(const char*s){ while(*s) putc_u(*s++); }
static signed char a[16]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
static signed char b[16]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
static unsigned char out[16];
void kmain(void){
  __asm__ volatile("li t5,0x600\n csrs mstatus,t5" ::: "t5");
  puts_u("vmul with vstart=4\n");
  __asm__ volatile(
    "li t0,16\n vsetvli t1,t0,e8,m1,ta,ma\n"
    "vle8.v v1,(%0)\n vle8.v v2,(%1)\n"
    "li t0,4\n csrw 0x008,t0\n"
    "vmul.vv v3,v1,v2\n"
    "csrw 0x008,zero\n"
    "li t0,16\n vsetvli t1,t0,e8,m1,ta,ma\n"
    "vse8.v v3,(%2)\n"
    :: "r"(a),"r"(b),"r"(out) : "t0","t1","memory");
  puts_u("done\n");
}