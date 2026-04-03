#!/bin/bash
# data_size 扫描实验 (扩大到 262144)

OUTPUT_FILE="/share_data/zhuyu/chiplet-network-sim/output/datasize_sweep_results.txt"
LOG_FILE="/share_data/zhuyu/chiplet-network-sim/output/datasize_sweep.log"

echo "=== data_size 扫描测试 (min 路由) ===" | tee $OUTPUT_FILE
echo "Date: $(date)" | tee -a $OUTPUT_FILE
echo "" | tee -a $OUTPUT_FILE
echo "data_size(flits), data_size(MB), throughput, cycles, time(s)" | tee -a $OUTPUT_FILE

for data_size in 1024 2048 4096 8192 16384 32768 65536 131072 262144; do
  cat > /share_data/zhuyu/chiplet-network-sim/input/test_sweep.ini << EOF
[Network]
topology = NVSwitch
num_spines = 4
num_leafs = 4
gpus_per_leaf = 2
total_gpus = 8
routing_algorithm = min

[Workload]
traffic = collective_ring_all_reduce
traffic_scale = 8
data_size = $data_size

[Simulation]
cycles = 100000
threads = 1
issue_width = 1

[Files]
output_file = output/test_sweep.csv
EOF
  
  echo "Running data_size = $data_size..." | tee -a $LOG_FILE
  start_time=$(date +%s)
  result=$(/share_data/zhuyu/chiplet-network-sim/build/ChipletNetworkSim /share_data/zhuyu/chiplet-network-sim/input/test_sweep.ini 2>&1)
  end_time=$(date +%s)
  elapsed=$((end_time - start_time))
  
  throughput=$(echo "$result" | grep "Throughput" | tail -1 | grep -oP "[0-9.]+")
  cycles=$(echo "$result" | grep "Data size" | tail -1 | grep -oP "Cycles: \K[0-9]+")
  
  # 1 flit = 8 bytes, 1 MB = 1048576 bytes
  data_mb=$((data_size * 8 / 1048576))
  
  echo "$data_size, $data_mb, $throughput, $cycles, ${elapsed}s" | tee -a $OUTPUT_FILE
  echo "  -> throughput=$throughput, cycles=$cycles, time=${elapsed}s" | tee -a $LOG_FILE
done

echo "" | tee -a $OUTPUT_FILE
echo "=== 实验完成 ===" | tee -a $OUTPUT_FILE
echo "完成时间: $(date)" | tee -a $OUTPUT_FILE
