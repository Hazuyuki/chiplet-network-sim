#!/bin/bash
# 64 卡 8 机 ring all-reduce，多链路 (spine_leaf_links_per_pair=4) 实验
# 后台运行完整扫描 (start_injection=0.1, max_injection=18)，结果写 output/
set -e
cd "$(dirname "$0")"
mkdir -p output
echo "Start 64-card 8-machine allreduce (multi-link) at $(date)"
nohup ./build/ChipletNetworkSim input/nvswitch_nvl256_multilink.ini \
  > output/nvswitch_nvl256_multilink_stdout.txt 2>&1 &
echo "PID: $!"
echo "Log: output/nvswitch_nvl256_multilink_stdout.txt"
echo "CSV: output/nvswitch_nvl256_multilink.csv"
