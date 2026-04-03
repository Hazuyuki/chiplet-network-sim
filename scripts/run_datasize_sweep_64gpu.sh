#!/bin/bash

# 64 GPU data_size 扫描实验
DATA_SIZES=(1024 2048 4096 8192 16384 32768 65536 131072 262144)
CONFIG_FILE="input/collective_ring_64gpu_sweep.ini"
RESULT_FILE="output/datasize_sweep_64gpu_results.txt"
LOG_FILE="output/datasize_sweep_64gpu.log"

echo "=== 64 GPU collective_ring_all_reduce data_size 扫描 ===" | tee $LOG_FILE
echo "开始时间: $(date)" | tee -a $LOG_FILE
echo "" | tee -a $LOG_FILE

echo "data_size(flits), data_size(MB), throughput, cycles, time(s)" > $RESULT_FILE

for data_size in "${DATA_SIZES[@]}"; do
    echo "" | tee -a $LOG_FILE
    SIZE_MB=$(echo "scale=3; $data_size * 8 / 1024 / 1024" | bc)
    echo "=== data_size = $data_size flits ($SIZE_MB MB) ===" | tee -a $LOG_FILE
    
    # 更新配置文件
    sed -i "s/data_size = .*/data_size = $data_size/" $CONFIG_FILE
    
    # 运行仿真
    START_TIME=$(date +%s)
    ./build/ChipletNetworkSim $CONFIG_FILE 2>&1 | tee -a $LOG_FILE
    END_TIME=$(date +%s)
    TIME_SPENT=$((END_TIME - START_TIME))
    
    # 提取吞吐量
    THROUGHPUT=$(grep "Throughput" output/collective_ring_64gpu_sweep.csv | tail -1 | awk -F"," '{print $2}')
    CYCLES=$(grep "Cycles" output/collective_ring_64gpu_sweep.csv | tail -1 | awk -F"," '{print $2}')
    
    if [ -n "$THROUGHPUT" ]; then
        echo "$data_size, $SIZE_MB, $THROUGHPUT, $CYCLES, ${TIME_SPENT}s" | tee -a $RESULT_FILE
    else
        echo "$data_size, $SIZE_MB, N/A, N/A, ${TIME_SPENT}s" | tee -a $RESULT_FILE
    fi
done

echo "" | tee -a $LOG_FILE
echo "=== 实验完成 ===" | tee -a $LOG_FILE
echo "完成时间: $(date)" | tee -a $LOG_FILE
