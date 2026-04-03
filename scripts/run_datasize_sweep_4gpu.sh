#!/bin/bash

DATA_SIZES=(1024 2048 4096 8192 16384 32768 65536 131072 262144)
CONFIG_FILE="input/collective_ring_4gpu_sweep.ini"
RESULT_FILE="output/datasize_sweep_4gpu_results.txt"
LOG_FILE="output/datasize_sweep_4gpu.log"

echo "=== 4 GPU collective_ring_all_reduce data_size 扫描 ===" | tee $LOG_FILE
echo "开始时间: $(date)" | tee -a $LOG_FILE
echo "" | tee -a $LOG_FILE

echo "data_size(flits), data_size(MB), throughput, cycles, time(s)" > $RESULT_FILE

for data_size in "${DATA_SIZES[@]}"; do
    echo "" | tee -a $LOG_FILE
    SIZE_MB=\$(awk "BEGIN {printf \"%.3f\", \$data_size * 8 / 1024 / 1024}")
    echo "=== data_size = \$data_size flits (\$SIZE_MB MB) ===" | tee -a $LOG_FILE
    
    sed -i "s/data_size = .*/data_size = \$data_size/" \$CONFIG_FILE
    
    START_TIME=\$(date +%s)
    ./build/ChipletNetworkSim \$CONFIG_FILE 2>&1 | tee -a \$LOG_FILE
    END_TIME=\$(date +%s)
    TIME_SPENT=\$((END_TIME - START_TIME))
    
    THROUGHPUT=\$(grep "Throughput:" \$LOG_FILE | tail -1 | awk "{print \\\$2}")
    CYCLES=\$(grep "Cycles:" \$LOG_FILE | tail -1 | awk "{print \\\$3}")
    
    echo "\$data_size, \$SIZE_MB, \$THROUGHPUT, \$CYCLES, \${TIME_SPENT}s" | tee -a \$RESULT_FILE
done

echo "" | tee -a \$LOG_FILE
echo "=== 实验完成 ===" | tee -a \$LOG_FILE
echo "完成时间: \$(date)" | tee -a \$LOG_FILE
