// vdot 参数化模型的位精确自检（Verilator）
//
// 对 NELEM 个元素做：Σ sext8(a[i]) * sext8(b[i])（i < vl），32 位回绕。
// 与 NEMU 模型独立实现（这里是 C++ 逐元素，NEMU 是 RVV 风格的 vcompute），
// 用于给 A10 的"参数化后语义不变"提供位精确证据。
//
// 编译期需要 -DNELEM=<n> -DVLENW=<VLEN/32>
#include "Vvdot_nelem.h"
#include "verilated.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>

#ifndef NELEM
#error "NELEM must be defined"
#endif
#ifndef VLENW
#error "VLENW must be defined"
#endif

// v2.3：题面 `for i in range(8)` 不引用 vl，所以参考模型不按 vl 截断。
// `vl` 仍然被驱动（dut->vl），用来验证"结果与 vl 无关"——模型侧该端口不参与逻辑。
static uint32_t reference(const uint8_t *a, const uint8_t *b, int nelem, int vl) {
  (void)vl;
  uint32_t s = 0; // 无符号累加 = 模 2^32，避免 C++ 有符号溢出 UB
  for (int i = 0; i < nelem; ++i) {
    int32_t prod = (int32_t)(int8_t)a[i] * (int32_t)(int8_t)b[i];
    s += (uint32_t)prod;
  }
  return s;
}

static Vvdot_nelem *dut;
static long checks = 0;
static long fails = 0;

static void run_case(const uint8_t *a, const uint8_t *b, int vl, const char *tag) {
  uint32_t w1[VLENW] = {0}, w2[VLENW] = {0};
  for (int i = 0; i < NELEM; ++i) {
    w1[i / 4] |= (uint32_t)a[i] << (8 * (i % 4));
    w2[i / 4] |= (uint32_t)b[i] << (8 * (i % 4));
  }
  for (int k = 0; k < VLENW; ++k) {
    dut->vs1[k] = w1[k];
    dut->vs2[k] = w2[k];
  }
  dut->vl = (uint8_t)vl;
  dut->eval();

  uint32_t exp = reference(a, b, NELEM, vl);
  ++checks;
  if (dut->sum != exp) {
    ++fails;
    if (fails <= 10) {
      std::printf("  FAIL [%s] vl=%d exp=%08x got=%08x\n", tag, vl, exp, dut->sum);
    }
  }
}

int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  dut = new Vvdot_nelem;
  dut->vd_old = 0;   // ACC=0 时该端口不参与逻辑；显式置 0 避免 4 态未初始化

  uint8_t a[NELEM], b[NELEM];

  // 1) 定向：极值组合
  const uint8_t vals[] = {0x00, 0x01, 0x7f, 0x80, 0xff};
  long dir = 0;
  for (uint8_t va : vals) {
    for (uint8_t vb : vals) {
      for (int i = 0; i < NELEM; ++i) { a[i] = va; b[i] = vb; }
      for (int vl = 0; vl <= NELEM + 1; ++vl) { run_case(a, b, vl, "directed"); ++dir; }
    }
  }

  // 2) 随机：vl 覆盖 0、中间、满、越界
  std::mt19937 rng(20261101u);
  std::uniform_int_distribution<int> byte(0, 255);
  std::uniform_int_distribution<int> vld(0, NELEM + 4);
  for (int t = 0; t < 400; ++t) {
    for (int i = 0; i < NELEM; ++i) {
      a[i] = (uint8_t)byte(rng);
      b[i] = (uint8_t)byte(rng);
    }
    int vl = vld(rng);
    if (t % 50 == 0) vl = 255; // vl 远超 NELEM：应全部参与
    run_case(a, b, vl, "random");
  }

  std::printf("NELEM=%d VLEN=%d : %ld checks, %ld fails -> %s\n",
              NELEM, VLENW * 32, checks, fails, fails == 0 ? "PASS" : "FAIL");
  delete dut;
  return fails == 0 ? 0 : 1;
}
