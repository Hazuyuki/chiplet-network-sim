#!/bin/bash
# 链路瓶颈诊断实验，见 docs/链路瓶颈诊断实验设计.md
# 用法: ./run_link_bottleneck_experiments.sh          # 仅跑链路类型统计（约 2 分钟）
#       ./run_link_bottleneck_experiments.sh --full   # 再跑 E1/E2/E3 分段隔离（较久）
set -e
cd "$(dirname "$0")"
mkdir -p output

echo "========== 1. 链路阻塞按类型统计 (64 卡 8 机 ring all-reduce) =========="
./build/test_link_bottleneck_diagnosis
echo "结果: output/link_bottleneck_by_type.csv"
echo ""

if [ "$1" = "--full" ]; then
  echo "========== 2. 分段隔离实验 (E1/E2/E3，较长时间) =========="
  echo "E1: 单机 8 GPU 无 spine..."
  ./build/ChipletNetworkSim input/nvswitch_8gpu_single_group.ini > output/exp_e1_stdout.txt 2>&1
  echo "E2: 64 卡 GPU 端口减半..."
  ./build/ChipletNetworkSim input/nvswitch_64card_half_gpu_ports.ini > output/exp_e2_stdout.txt 2>&1
  echo "E3: 64 卡 spine 每对 1 链路..."
  ./build/ChipletNetworkSim input/nvswitch_64card_single_spine_link.ini > output/exp_e3_stdout.txt 2>&1
  echo "Done. 对比 output/nvswitch_8gpu_single_group.csv、*half_gpu_ports.csv、*single_spine_link.csv 与 nvswitch_nvl256_multilink.csv 的饱和吞吐。"
else
  echo "跳过分段隔离实验。要跑 E1/E2/E3 请执行: $0 --full"
fi
