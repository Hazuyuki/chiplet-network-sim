#!/bin/bash
# 构建并运行 Credit 流控测试
# 用法: ./run_test_credit_flow_control.sh [config.ini]
#       不传参数时使用 input/nvswitch_test.ini（需保证 flow_control=credit）

set -e
cd "$(dirname "$0")"
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4 test_credit_flow_control
cd ..
if [ -n "$1" ]; then
  ./build/test_credit_flow_control "$1"
else
  ./build/test_credit_flow_control input/nvswitch_test.ini
fi
