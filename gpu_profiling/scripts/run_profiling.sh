#!/bin/bash
# NCCL All-Reduce Profiling 运行脚本 (Python 版本)
# 支持 nsys 和 ncu 进行性能分析

set -e

# 默认参数
DATA_SIZE="256MB"
ITERATIONS=100
WARMUP=10
NUM_GPUS=4

# Profiling 选项
RUN_NSYS=false
RUN_NCU=false
OUTPUT_DIR=""

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="${SCRIPT_DIR}/../src"
RESULTS_DIR="${SCRIPT_DIR}/../results"

function print_usage() {
    echo "Usage: $0 [options]"
    echo ""
    echo "Options:"
    echo "  -s, --size <size>      Data size (default: ${DATA_SIZE})"
    echo "                         Examples: 1MB, 256MB, 1gb"
    echo "  -i, --iter <num>      Number of iterations (default: ${ITERATIONS})"
    echo "  -w, --warmup <num>    Warmup iterations (default: ${WARMUP})"
    echo "  -g, --ngpus <num>     Number of GPUs (default: ${NUM_GPUS})"
    echo "  --nsys                Run with nsys profiling"
    echo "  --ncu                 Run with ncu profiling"
    echo "  -o, --output <dir>   Output directory (default: ${RESULTS_DIR})"
    echo "  -h, --help            Show this help message"
    echo ""
    echo "Examples:"
    echo "  # Basic run"
    echo "  $0 -s 256MB -i 100"
    echo ""
    echo "  # Run with nsys profiling"
    echo "  $0 -s 256MB -i 100 --nsys"
    echo ""
    echo "  # Run with ncu profiling"
    echo "  $0 -s 256MB -i 100 --ncu"
}

# 解析参数
while [[ $# -gt 0 ]]; do
    case $1 in
        -s|--size)
            DATA_SIZE="$2"
            shift 2
            ;;
        -i|--iter)
            ITERATIONS="$2"
            shift 2
            ;;
        -w|--warmup)
            WARMUP="$2"
            shift 2
            ;;
        -g|--ngpus)
            NUM_GPUS="$2"
            shift 2
            ;;
        --nsys)
            RUN_NSYS=true
            shift
            ;;
        --ncu)
            RUN_NCU=true
            shift
            ;;
        -o|--output)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        -h|--help)
            print_usage
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            print_usage
            exit 1
            ;;
    esac
done

# 设置输出目录
if [ -z "$OUTPUT_DIR" ]; then
    TIMESTAMP=$(date +%Y%m%d_%H%M%S)
    OUTPUT_DIR="${RESULTS_DIR}/${DATA_SIZE}_${TIMESTAMP}"
fi

mkdir -p "${OUTPUT_DIR}"

# Python 脚本路径
PYTHON_SCRIPT="${SRC_DIR}/nccl_profiling.py"

# 检查 Python 脚本
if [ ! -f "${PYTHON_SCRIPT}" ]; then
    echo -e "${RED}Error: Python script not found: ${PYTHON_SCRIPT}${NC}"
    exit 1
fi

echo -e "${GREEN}=== NCCL All-Reduce Profiling (Python) ===${NC}"
echo "Data size: ${DATA_SIZE}"
echo "Iterations: ${ITERATIONS}"
echo "Warmup: ${WARMUP}"
echo "Number of GPUs: ${NUM_GPUS}"
echo "Output directory: ${OUTPUT_DIR}"
echo ""

# 检查 Python 和 PyTorch
if ! command -v python3 &> /dev/null; then
    echo -e "${RED}Error: python3 not found.${NC}"
    exit 1
fi

# 检查 PyTorch
if ! python3 -c "import torch" 2>/dev/null; then
    echo -e "${RED}Error: PyTorch is not installed.${NC}"
    echo "Install with: pip install torch"
    exit 1
fi

PYTHON_VERSION=$(python3 --version)
TORCH_VERSION=$(python3 -c "import torch; print(torch.__version__)")
CUDA_AVAILABLE=$(python3 -c "import torch; print(torch.cuda.is_available())")

echo "Python: ${PYTHON_VERSION}"
echo "PyTorch: ${TORCH_VERSION}"
echo "CUDA available: ${CUDA_AVAILABLE}"
echo ""

# 构建命令行参数
PYTHON_ARGS="-s ${DATA_SIZE} -i ${ITERATIONS} -w ${WARMUP} -g ${NUM_GPUS}"

if [ "$RUN_NSYS" = true ]; then
    PYTHON_ARGS="${PYTHON_ARGS} --nsys"
fi

if [ "$RUN_NCU" = true ]; then
    PYTHON_ARGS="${PYTHON_ARGS} --ncu"
fi

if [ -n "$OUTPUT_DIR" ]; then
    PYTHON_ARGS="${PYTHON_ARGS} -o ${OUTPUT_DIR}"
fi

# 运行 Python 脚本
echo -e "${BLUE}=== Running profiling ===${NC}"
python3 ${PYTHON_SCRIPT} ${PYTHON_ARGS}

echo ""
echo -e "${GREEN}=== Complete ===${NC}"
echo "Results saved to: ${OUTPUT_DIR}"
