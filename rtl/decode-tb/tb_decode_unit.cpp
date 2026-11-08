// 驱动 DecodeUnit，读一条指令的译码结果。
// 用法: ./dec_tb <hex-instr> [vsew] [vlmul]
#include "VDecodeUnit.h"
#include "verilated.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  if (argc < 2) { fprintf(stderr, "usage: %s <hex-instr> [vsew] [vlmul]\n", argv[0]); return 1; }
  VDecodeUnit* d = new VDecodeUnit;
  d->io_enq_ctrlFlow_instr = (uint32_t)strtoull(argv[1], nullptr, 16);
  d->io_enq_ctrlFlow_pc = 0x8000022eULL;
  d->io_enq_vtype_vma = 1; d->io_enq_vtype_vta = 1;
  d->io_enq_vtype_vsew = (argc > 2) ? atoi(argv[2]) : 0;
  d->io_enq_vtype_vlmul = (argc > 3) ? atoi(argv[3]) : 0;
  d->io_enq_vstart = 0;
  d->eval();
  unsigned long long ft = d->io_deq_decodedInst_fuType;
  int nb = 0; for (int i = 0; i < 35; i++) if ((ft >> i) & 1) nb++;
  printf("instr=0x%08x\n", d->io_enq_ctrlFlow_instr);
  printf("  fuType      = 0x%llx (bit %d, %s)\n", ft,
         nb == 1 ? __builtin_ctzll(ft) : -1, nb == 1 ? "one-hot" : "NOT one-hot");
  printf("  fuOpType    = 0x%llx\n", (unsigned long long)d->io_deq_decodedInst_fuOpType);
  printf("  selImm      = %llu %s\n", (unsigned long long)d->io_deq_decodedInst_selImm,
         d->io_deq_decodedInst_selImm == 6 ? "(INVALID_INSTR: 未匹配任何表项)" : "(ok)");
  printf("  uopSplitType= 0x%llx\n", (unsigned long long)d->io_deq_decodedInst_uopSplitType);
  printf("  srcType     = %d %d %d (src0..2)\n", (int)d->io_deq_decodedInst_srcType_0,
         (int)d->io_deq_decodedInst_srcType_1, (int)d->io_deq_decodedInst_srcType_2);
  printf("  vecWen      = %d\n", (int)d->io_deq_decodedInst_vecWen);
  printf("  exc_2(illegal) = %d\n", (int)d->io_deq_decodedInst_exceptionVec_2);
  delete d;
  return 0;
}
