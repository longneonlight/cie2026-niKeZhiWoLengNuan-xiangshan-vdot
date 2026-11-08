#!/usr/bin/env bash
# ============================================================
# 香山 vdot 开发环境部署（Ubuntu 22.04）
#
# 用法：
#   ./provision-2204.sh                 # 只装环境与工具链，不跑耗时构建
#   ./provision-2204.sh --build-rtl     # 额外执行 make verilog（约 85 分钟）
#
# 可覆盖的环境变量：
#   XIANGSHAN_DIR   香山工作区（默认 $HOME/XiangShan）
#   NEMU_DIR        NEMU 工作区（默认 $HOME/NEMU）
#
# 说明：
#   - 脚本不配置任何网络站点（apt 源、代理、镜像都请按自己的网络环境准备）；
#   - 幂等，可重复执行；每一步打印进度，失败即停；
#   - 需要 sudo 权限。
# ============================================================
set -euo pipefail

XS=${XIANGSHAN_DIR:-$HOME/XiangShan}
ND=${NEMU_DIR:-$HOME/NEMU}
BUILD_RTL=0
for arg in "$@"; do
  case "$arg" in
    --build-rtl) BUILD_RTL=1 ;;
    *) echo "未知参数: $arg"; exit 2 ;;
  esac
done

say() { printf '\n==> %s\n' "$*"; }

# ------------------------------------------------------------
say "0. 系统信息"
. /etc/os-release
echo "系统 : $PRETTY_NAME"
echo "内核 : $(uname -r)"
echo "CPU  : $(nproc) 核"
free -h | head -2
df -h / | tail -1
if [ "$(id -u)" -ne 0 ] && ! sudo -n true 2>/dev/null; then
  echo "需要 sudo 权限（或先以 root 运行）"; exit 1
fi

# ------------------------------------------------------------
say "1. 基础构建依赖"
sudo apt-get update
sudo apt-get install -y \
  build-essential git curl wget unzip \
  autoconf flex bison libfl-dev \
  device-tree-compiler python3 python3-pip \
  libgoogle-perftools-dev numactl

# ------------------------------------------------------------
say "2. JDK 17（香山用 mill 构建，需要 JDK 11 以上）"
sudo apt-get install -y openjdk-17-jdk
java -version

# ------------------------------------------------------------
say "3. mill（Scala 构建工具）"
if ! command -v mill >/dev/null 2>&1; then
  echo "未检测到 mill，请按下方方式自行安装后重跑本脚本："
  echo "  1) 从 mill 官方 release 下载可执行文件；"
  echo "  2) 放到 PATH 下并 chmod +x；"
  echo "  3) 首次运行会自动下载依赖，需要能访问 Maven 仓库。"
else
  mill --version || true
fi

# ------------------------------------------------------------
say "4. RISC-V 交叉工具链（编译裸机测试程序）"
sudo apt-get install -y gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu
riscv64-linux-gnu-gcc --version | head -1

# ------------------------------------------------------------
say "5. Verilator（香山要求 5.x）"
if command -v verilator >/dev/null 2>&1; then
  VV=$(verilator --version | awk '{print $2}')
  echo "当前版本: $VV"
  case "$VV" in
    5.*|6.*) echo "版本满足要求" ;;
    *) echo "版本低于 5.x，需要源码编译（模板见下）" ;;
  esac
else
  echo "未检测到 verilator"
fi
cat <<'EOF'

  如需源码编译，大致步骤（版本号按需替换）：
    git clone https://github.com/verilator/verilator -b v5.052
    cd verilator && autoconf && ./configure --prefix=/usr/local
    make -j"$(nproc)" && sudo make install
  注意：Verilator 5.050+ 需要 clang 18 及以上；若系统自带的 clang 太旧，
  需要先装新版 clang（这一步依赖你的软件源，本脚本不做站点配置）。
EOF

# ------------------------------------------------------------
say "6. 环境变量"
if [ -f "$HOME/.xs-env.sh" ]; then
  echo "已存在 $HOME/.xs-env.sh，跳过"
else
  cat > "$HOME/.xs-env.sh" <<EOF
# 香山与 NEMU 的工作区位置（按自己的目录改）
export XIANGSHAN_DIR="$XS"
export NEMU_DIR="$ND"
export PATH="\$PATH:\$HOME/bin"
EOF
  echo "已写入 $HOME/.xs-env.sh，可用 'source ~/.xs-env.sh' 加载"
fi

# ------------------------------------------------------------
say "7. 构建 NEMU（两种配置：独立解释器 + difftest 参考模型）"
if [ -d "$ND" ]; then
  make -C "$ND" riscv64-xs-ref_defconfig && make -C "$ND" -j"$(nproc)"
  make -C "$ND" riscv64-xs_defconfig    && make -C "$ND" -j"$(nproc)"
  ls -l "$ND/build/riscv64-nemu-interpreter" "$ND/build/riscv64-nemu-interpreter-so" 2>/dev/null || true
else
  echo "未找到 NEMU 工作区：$ND（可用 NEMU_DIR 指定）"
fi

# ------------------------------------------------------------
say "8. 香山 RTL（可选，约 85 分钟）"
MEM_GiB=$(free -g | awk '/^Mem:/{print $2}')
JX=$(( MEM_GiB > 20 ? 16 : 10 ))
if [ "$BUILD_RTL" = "1" ]; then
  echo "使用 JVM_XMX=${JX}G（按物理内存 ${MEM_GiB} GiB 估算）"
  make -C "$XS" verilog JVM_XMX="${JX}G" -j"$(nproc)"
  find "$XS/build/rtl" -name '*.sv' | wc -l
else
  echo "已跳过。要构建请加 --build-rtl；JVM_XMX 建议 ${JX}G"
fi

# ------------------------------------------------------------
say "9. 下一步"
cat <<EOF

  # 构建仿真器（约 85 分钟，只需一次）
  make -C "$XS" emu JVM_XMX=${JX}G -j\$(nproc)

  # 编译并运行阶段一的 hello（在仓库根目录执行）
  bash scripts/stage1-deploy.sh

  # 一键回归
  bash scripts/run-all.sh
EOF
