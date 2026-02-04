#!/bin/bash
# 多流跨 node 实验：验证 spine 无阻塞
# 用法: ./run_cross_node_exp.sh
# 观察：若无阻塞，随注入率增加接收率应能跟上直至接近理论容量，且 timeout 很少；
#       若 spine 阻塞，会在较低注入率下就出现饱和或大量 timeout。
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"
BIN="${SCRIPT_DIR}/build/ChipletNetworkSim"
INI="${SCRIPT_DIR}/input/nvswitch_cross_node.ini"
mkdir -p output 2>/dev/null || true
echo "===== 多流跨 node 实验 (inter_group_uniform + spine_non_blocking) ====="
"$BIN" "$INI" 2>&1 | tee output/cross_node_stdout.txt
echo ""
echo "结果写入 output/nvswitch_cross_node.csv 与 output/cross_node_stdout.txt"
