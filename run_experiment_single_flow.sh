#!/bin/bash
# 单发单收实验：不同 credit_return_delay，输出 output/single_flow_delay_N.csv
# 用于与单链路公式 min(B, C_eff/(T_other+R)) 对比
set -e
cd "$(dirname "$0")"
mkdir -p output

BIN="./build/ChipletNetworkSim"
TEMPLATE="input/nvswitch_single_flow_exp.ini"

if [ ! -x "$BIN" ]; then
  echo "请先编译: cd build && cmake .. && make -j4"
  exit 1
fi

DELAYS="0 2 4 6 8 12 16 20 24"
for d in $DELAYS; do
  echo "===== single_flow credit_return_delay = $d ====="
  sed "s/PLACEHOLDER/$d/g" "$TEMPLATE" > output/config_single_flow_${d}.ini
  "$BIN" output/config_single_flow_${d}.ini > output/single_flow_delay_${d}_stdout.txt 2>&1
  echo "  -> output/single_flow_delay_${d}.csv"
done

echo ""
if command -v python3 &>/dev/null; then
  python3 plot_single_flow_vs_formula.py && echo "已生成 single_flow_vs_formula.png"
else
  echo "绘图请运行: python3 plot_single_flow_vs_formula.py"
fi
