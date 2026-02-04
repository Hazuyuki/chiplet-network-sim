#!/bin/bash
# 寻找达不到18的最主要原因：对比不同参数下的饱和吞吐
# 基线: R=4, buffer=32 → ~4
# 若 R=0 时显著提升 → credit_return_delay 是主因
# 若 buffer=256 时显著提升 → buffer_size 是主因
# 若 R=0+buffer=256 仍低 → 其他（ring 模式、路径长度等）

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"
BIN="${SCRIPT_DIR}/build/ChipletNetworkSim"
INI="${SCRIPT_DIR}/input/nvswitch_allreduce.ini"
mkdir -p output

# 备份并创建临时 INI，缩短实验时间
MAX_INJ=6
SIM_TIME=1200

run_and_extract() {
  local label="$1"
  local r="$2"
  local buf="$3"
  echo "===== $label: credit_return_delay=$r, buffer_size=$buf ====="
  # 生成临时配置
  sed -e "s/credit_return_delay = .*/credit_return_delay = $r/" \
      -e "s/buffer_size = .*/buffer_size = $buf/" \
      -e "s/max_injection = .*/max_injection = $MAX_INJ/" \
      -e "s/simulation_time = .*/simulation_time = $SIM_TIME/" \
      "$INI" > output/tmp_allreduce.ini
  "$BIN" output/tmp_allreduce.ini 2>&1 | tee "output/main_cause_${label}.txt" | \
    grep -E "Maximum average receiving|Saturation|Injection rate" | tail -5
  echo ""
}

echo "===== 寻找达不到18的主因：对比实验 ====="
echo ""

run_and_extract "R4_buf32" 4 32
run_and_extract "R0_buf32" 0 32
run_and_extract "R4_buf256" 4 256
run_and_extract "R0_buf256" 0 256

echo "===== 完成。查看 output/main_cause_*.txt 获取详情 ====="
