/* vdot 参考实现 + 测试向量生成器
 *
 * 用途：
 *   1. 作为 vdot.vv 语义的独立实现（与 RTL / NEMU 三方对照）
 *   2. 生成 tests/vectors/*.csv 的期望值
 *
 * 语义（唯一依据：赛题页面所附伪代码图，见 docs/01-semantics.md）：
 *
 *     def vdot_vv(vs1, vs2, sew=8):
 *         result = 0
 *         for i in range(8):                       # ← 固定 8 个
 *             result += sign_extend(vs1[i]) * sign_extend(vs2[i])
 *         vd[0] = result
 *         vd[1..VLEN-1] = 0
 *
 * 即：acc = Σ_{i<8} (int32)a[i] * (int32)b[i]（32 位回绕，**与 vl 无关**）
 *     vd[31:0] = acc，vd[127:32] = 0
 *
 * 编译： gcc -O2 -fwrapv -o vdot_ref vdot_ref.c
 * 运行： ./vdot_ref            # 打印 CSV
 *        ./vdot_ref selftest   # 自检
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define VLMAX 16
#define NELEM 8 /* 题面固定取 8 个 int8 */

/* 参考语义：用无符号累加保证 32 位回绕定义明确 */
int32_t vdot_ref(const int8_t *a, const int8_t *b, int vl) {
  (void)vl; /* 题面 for i in range(8)：vl 不参与计算，保留形参只为调用侧接口稳定 */
  uint32_t acc = 0;
  for (int i = 0; i < NELEM; ++i) {
    int32_t p = (int32_t)a[i] * (int32_t)b[i];
    acc += (uint32_t)p;
  }
  return (int32_t)acc;
}

struct Case {
  const char *id;
  const char *name;
  const int8_t *a;
  const int8_t *b;
  int vl;
  int32_t expect;
};

/* ---------- 测试数据 ---------- */

static int8_t inc[VLMAX];
static int8_t sinc[VLMAX]; /* -1..-16 */
static int8_t half[VLMAX];

static const int8_t pos127[VLMAX] = {127, 127, 127, 127, 127, 127, 127, 127,
                                     127, 127, 127, 127, 127, 127, 127, 127};
static const int8_t neg128[VLMAX] = {-128, -128, -128, -128, -128, -128,
                                      -128, -128, -128, -128, -128, -128,
                                     -128, -128, -128, -128};
static const int8_t zero16[VLMAX] = {0};
static const int8_t ones16[VLMAX] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
static const int8_t alt8[VLMAX] = {1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1};

static void init_data(void) {
  for (int i = 0; i < VLMAX; ++i) {
    inc[i] = (int8_t)(i + 1);
    sinc[i] = (int8_t)(-(i + 1));
    half[i] = (int8_t)(i + 1);
  }
}

static void print_hex(const int8_t *v, int n, char *out) {
  for (int i = 0; i < n; ++i) sprintf(out + i * 2, "%02x", (uint8_t)v[i]);
  out[n * 2] = 0;
}

int main(int argc, char **argv) {
  init_data();

  struct Case cases[] = {
      {"1", "positive", inc, inc, 16, 204},
      {"2", "negative", sinc, inc, 16, -204},
      {"3", "mixed", alt8, ones16, 16, 0},
      {"4", "max", pos127, pos127, 16, 129032},
      {"5", "min", neg128, neg128, 16, 131072},
      {"6", "zero", zero16, inc, 16, 0},
      {"7", "one_side_zero", zero16, inc, 16, 0},
      {"8", "vl8_same_as_16", half, half, 8, 204},
      {"9", "vl4_same_as_16", inc, inc, 4, 204},
      {"10", "vl_zero_same_as_16", inc, inc, 0, 204},
  };
  const int ncases = (int)(sizeof(cases) / sizeof(cases[0]));

  if (argc > 1 && strcmp(argv[1], "selftest") == 0) {
    int fail = 0;
    for (int i = 0; i < ncases; ++i) {
      int32_t got = vdot_ref(cases[i].a, cases[i].b, cases[i].vl);
      if (got != cases[i].expect) {
        printf("FAIL case %s (%s): got %d expect %d\n", cases[i].id, cases[i].name,
               got, cases[i].expect);
        fail = 1;
      } else {
        printf("ok   case %-2s %-16s vl=%-2d -> %d\n", cases[i].id, cases[i].name,
               cases[i].vl, got);
      }
    }
    printf(fail ? "SELFTEST FAILED\n" : "SELFTEST PASSED (%d cases)\n", ncases);
    return fail;
  }

  printf("id,case,vl,vm,vstart,vs1_hex,vs2_hex,vd0_expected\n");
  for (int i = 0; i < ncases; ++i) {
    char h1[64], h2[64];
    print_hex(cases[i].a, VLMAX, h1);
    print_hex(cases[i].b, VLMAX, h2);
    printf("%s,%s,%d,1,0,%s,%s,%d\n", cases[i].id, cases[i].name, cases[i].vl, h1, h2,
           cases[i].expect);
  }
  return 0;
}
