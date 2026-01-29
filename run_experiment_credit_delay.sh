#!/bin/bash
# 实验：不同 credit_return_delay，跑仿真并写 output/credit_delay_N.csv
# 用法: ./run_experiment_credit_delay.sh

set -e
cd "$(dirname "$0")"
mkdir -p output

BIN="./build/ChipletNetworkSim"
TEMPLATE="input/nvswitch_credit_delay_exp.ini"

if [ ! -x "$BIN" ]; then
  echo "请先编译: cd build && cmake .. && make -j4"
  exit 1
fi

DELAYS="0 2 4 6 8 12 16 20 24"
for d in $DELAYS; do
  echo "===== credit_return_delay = $d ====="
  sed "s/PLACEHOLDER/$d/g" "$TEMPLATE" > output/config_delay_${d}.ini
  "$BIN" output/config_delay_${d}.ini > output/credit_delay_${d}_stdout.txt 2>&1
  echo "  -> output/credit_delay_${d}.csv"
done

echo ""
if [ -x "/share/zhuyu-nfs/llm/bin/python" ]; then
  /share/zhuyu-nfs/llm/bin/python plot_credit_delay.py && echo "已生成 credit_delay_throughput.png, credit_delay_comparison.png"
else
  echo "绘图请运行: python3 plot_credit_delay.py"
fi
