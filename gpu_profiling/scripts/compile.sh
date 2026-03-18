#!/bin/bash
# 编译 NCCL All-Reduce Profiling 代码

set -e

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="${SCRIPT_DIR}/src"
BUILD_DIR="${SCRIPT_DIR}/build"

echo -e "${GREEN}=== Compiling NCCL All-Reduce Profiling Code ===${NC}"

# 检查 CUDA
if ! command -v nvcc &> /dev/null; then
    echo -e "${RED}Error: nvcc not found. Please ensure CUDA is installed.${NC}"
    exit 1
fi

# 创建 build 目录
mkdir -p "${BUILD_DIR}"

# 编译
echo "Compiling..."
nvcc -o "${BUILD_DIR}/nccl_allreduce" "${SRC_DIR}/nccl_allreduce.cu" \
    -lnccl -lcuda \
    -O3 -arch=sm_80 \
    -lineinfo

if [ $? -eq 0 ]; then
    echo -e "${GREEN}Compilation successful!${NC}"
    echo -e "Executable: ${BUILD_DIR}/nccl_allreduce"
else
    echo -e "${RED}Compilation failed!${NC}"
    exit 1
fi

# 检查 NCCL 库
echo ""
echo "Checking NCCL..."
if ldconfig -p | grep -q libnccl.so; then
    echo -e "${GREEN}NCCL library found${NC}"
    ldconfig -p | grep libnccl.so
else
    echo -e "${YELLOW}Warning: NCCL library not found in ldconfig${NC}"
fi

echo ""
echo "Done! Run with: ${BUILD_DIR}/nccl_allreduce -h"
