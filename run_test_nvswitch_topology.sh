#!/bin/bash
# 构建并运行 NVSwitch 拓扑测试
# 用法: ./run_test_nvswitch_topology.sh [config.ini]
#       不传参数时使用 input/nvswitch_test.ini

set -e
cd "$(dirname "$0")"
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4 test_nvswitch_topology
cd ..
if [ -n "$1" ]; then
  ./build/test_nvswitch_topology "$1"
else
  ./build/test_nvswitch_topology input/nvswitch_test.ini
fi
