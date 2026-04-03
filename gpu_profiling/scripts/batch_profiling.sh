#!/bin/bash
# 批量运行不同数据大小的 NCCL All-Reduce Profiling 测试
# 自动生成 nsys report 并计算带宽

set -e

# 测试参数
DATA_SIZES=("1MB" "4MB" "16MB" "64MB" "256MB" "1024MB")
ITERATIONS=100
WARMUP=10
NUM_GPUS=4

# 输出目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_DIR="${SCRIPT_DIR}/../results"
SRC_DIR="${SCRIPT_DIR}/../src"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# 创建主输出目录
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
MAIN_OUTPUT_DIR="${RESULTS_DIR}/batch_${TIMESTAMP}"
mkdir -p "${MAIN_OUTPUT_DIR}"

echo -e "${GREEN}=== Batch NCCL All-Reduce Profiling ===${NC}"
echo "Data sizes: ${DATA_SIZES[*]}"
echo "Iterations: ${ITERATIONS}"
echo "Number of GPUs: ${NUM_GPUS}"
echo "Output directory: ${MAIN_OUTPUT_DIR}"
echo ""

# 先运行不带 nsys 的测试，获取基准时间
echo -e "${BLUE}=== Running baseline tests (without nsys) ===${NC}"
for SIZE in "${DATA_SIZES[@]}"; do
    echo "Testing ${SIZE}..."
    
    SIZE_OUTPUT_DIR="${MAIN_OUTPUT_DIR}/${SIZE}"
    mkdir -p "${SIZE_OUTPUT_DIR}"
    
    # 使用 torchrun 运行，不带 nsys
    torchrun --nproc_per_node=${NUM_GPUS} \
        "${SRC_DIR}/nccl_profiling.py" \
        -s "${SIZE}" \
        -i "${ITERATIONS}" \
        -w "${WARMUP}" \
        2>&1 | tee "${SIZE_OUTPUT_DIR}/baseline_output.log"
    
    echo "Completed ${SIZE}"
done

echo ""
echo -e "${BLUE}=== Running nsys profiling ===${NC}"

# 重新运行，这次使用 nsys 生成 report
for SIZE in "${DATA_SIZES[@]}"; do
    echo "Running nsys profile for ${SIZE}..."
    
    SIZE_OUTPUT_DIR="${MAIN_OUTPUT_DIR}/${SIZE}"
    REPORT_NAME="nccl_${SIZE}_report"
    
    # 使用 nsys profile 运行
    nsys profile -o "${SIZE_OUTPUT_DIR}/${REPORT_NAME}" \
                 -f true \
                 -c cudaProfilerApi \
                 --capture-range-end stop \
                 -x true \
                 torchrun --nproc_per_node=${NUM_GPUS} \
                 "${SRC_DIR}/nccl_profiling.py" \
                 -s "${SIZE}" \
                 -i "${ITERATIONS}" \
                 -w "${WARMUP}" \
                 2>&1 | tee "${SIZE_OUTPUT_DIR}/nsys_output.log"
    
    echo "Completed ${SIZE}"
done

# 汇总结果
echo ""
echo -e "${GREEN}=== Results Summary ===${NC}"
echo "================================================================================"
printf "| %-10s | %-12s | %-15s | %-15s | %-10s |\n" "Data Size" "Time (ms)" "Bandwidth (GB/s)" "理论单向带宽" "效率"
echo "----------------------------------------------------------------------"

# 从日志文件中提取结果
for SIZE in "${DATA_SIZES[@]}"; do
    LOG_FILE="${MAIN_OUTPUT_DIR}/${SIZE}/baseline_output.log"
    
    if [ -f "$LOG_FILE" ]; then
        # 提取时间和带宽
        AVG_TIME=$(grep "AVG_TIME_MS=" "$LOG_FILE" | head -1 | cut -d'=' -f2)
        BANDWIDTH=$(grep "BANDWIDTH_GBPS=" "$LOG_FILE" | head -1 | cut -d'=' -f2)
        
        if [ -n "$AVG_TIME" ] && [ -n "$BANDWIDTH" ]; then
            # A800 单 GPU 单向理论带宽 400 GB/s
            EFFICIENCY=$(echo "scale=1; $BANDWIDTH / 400 * 100" | bc)
            printf "| %-10s | %-12s | %-15s | %-15s | %-10s%%|\n" "$SIZE" "$AVG_TIME" "$BANDWIDTH" "400 GB/s" "$EFFICIENCY"
        else
            printf "| %-10s | %-12s | %-15s | %-15s | %-10s|\n" "$SIZE" "N/A" "N/A" "400 GB/s" "N/A"
        fi
    else
        printf "| %-10s | %-12s | %-15s | %-15s | %-10s|\n" "$SIZE" "No log" "No log" "400 GB/s" "No log"
    fi
done

echo "================================================================================"
echo ""
echo -e "${GREEN}Results saved to: ${MAIN_OUTPUT_DIR}${NC}"

# 列出所有生成的 nsys report 文件
echo ""
echo "Generated nsys report files:"
find "${MAIN_OUTPUT_DIR}" -name "*.nsys-rep" -type f 2>/dev/null || echo "No nsys reports found"
