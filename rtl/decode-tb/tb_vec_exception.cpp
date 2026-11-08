// 驱动带探针的 VecExceptionGenX（由 patch_vec_exception.py 生成）。
// 用法: ./vecx <hex-instr> [vsew] [vlmul] [vstart]
#include "VVecExceptionGenX.h"
#include "verilated.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  if (argc < 2) { fprintf(stderr, "usage: %s <hex-instr> [vsew] [vlmul] [vstart]\n", argv[0]); return 1; }
  VVecExceptionGenX* v = new VVecExceptionGenX;
  v->io_inst = (uint32_t)strtoull(argv[1], nullptr, 16);
  v->io_decodedInst_fuType = 0x200000ULL;   // FuType.vimac
  v->io_decodedInst_srcType_0 = 4; v->io_decodedInst_srcType_1 = 4; v->io_decodedInst_srcType_2 = 4;
  v->io_decodedInst_vecWen = 1;
  v->io_vtype_illegal = 0;
  v->io_vtype_vsew = (argc > 2) ? atoi(argv[2]) : 0;
  v->io_vtype_vlmul = (argc > 3) ? atoi(argv[3]) : 0;
  v->io_vstart = (argc > 4) ? atoi(argv[4]) : 0;
  v->eval();
  printf("inst=0x%08x illegalInst=%d\n", v->io_inst, (int)v->io_illegalInst);
  printf("  inst=%d vill=%d eew=%d emul=%d regNum=%d overlap=%d vstart=%d notDepVtype=%d\n",
     (int)v->o_instIllegal, (int)v->o_villIllegal, (int)v->o_eewIllegal, (int)v->o_emulIllegal,
     (int)v->o_regNumIllegal, (int)v->o_regOverlapIllegal, (int)v->o_vstartIllegal,
     (int)v->o_notDependVtypeInst);
  printf("  vs1Emul=%d vs2Emul=%d vdEmul=%d  (LMUL=1 时应为 4)\n",
     (int)v->o_vs1Emul, (int)v->o_vs2Emul, (int)v->o_vdEmul);
  delete v;
  return 0;
}
