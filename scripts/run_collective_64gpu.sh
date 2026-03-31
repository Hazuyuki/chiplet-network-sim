#!/bin/bash
# CNSim Collective All-Reduce 64 GPU 批量测试脚本
# 使用 collective 模式自动扫描不同数据大小

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CNSIM_DIR="${SCRIPT_DIR}/.."
BUILD_DIR="${CNSIM_DIR}/build"
INPUT_DIR="${CNSIM_DIR}/input"
OUTPUT_DIR="${BUILD_DIR}/output"

# 创建输出目录
mkdir -p "${OUTPUT_DIR}"

# 测试配置 - traffic 名称需要包含 "collective" 才能触发自动数据大小扫描
MODES=("collective_ring_all_reduce" "collective_hierarchical_all_reduce")

echo "=== CNSim Collective All-Reduce 64 GPU Test ==="
echo ""

for MODE in "${MODES[@]}"; do
    echo "=============================================="
    echo "Testing: ${MODE}"
    echo "=============================================="
    
    # 生成配置文件
    if [ "$MODE" == "collective_ring_all_reduce" ]; then
        CONFIG_FILE="${INPUT_DIR}/nvswitch_collective_ring_64gpu.ini"
        # 修改 traffic 为 collective_ring_all_reduce
        sed -i "s/^traffic =.*/traffic = collective_ring_all_reduce/" "${CONFIG_FILE}"
    else
        CONFIG_FILE="${INPUT_DIR}/nvswitch_collective_hierarchical_64gpu.ini"
        # 修改 traffic 为 collective_hierarchical_all_reduce
        sed -i "s/^traffic =.*/traffic = collective_hierarchical_all_reduce/" "${CONFIG_FILE}"
    fi
    
    # 运行仿真
    cd "${BUILD_DIR}"
    ./ChipletNetworkSim "${CONFIG_FILE}"
    
    echo "Completed: ${MODE}"
    echo ""
done

echo "=== All tests completed ==="
echo ""

# 汇总结果
echo "=== Results Summary ==="
echo ""

for MODE in "${MODES[@]}"; do
    if [ "$MODE" == "collective_ring_all_reduce" ]; then
        OUTPUT_FILE="${OUTPUT_DIR}/nvswitch_collective_ring_64gpu.csv"
    else
        OUTPUT_FILE="${OUTPUT_DIR}/nvswitch_collective_hierarchical_64gpu.csv"
    fi
    
    if [ -f "${OUTPUT_FILE}" ]; then
        echo "--- ${MODE} ---"
        cat "${OUTPUT_FILE}"
        echo ""
    fi
done
