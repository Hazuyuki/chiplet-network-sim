#!/bin/bash
# 包泼洒测试
set -e
cd "$(dirname "$0")"

if [ ! -x "./build/test_packet_spraying" ]; then
  echo "编译中..."
  mkdir -p build && cd build && cmake .. && make -j4 test_packet_spraying && cd ..
fi

./build/test_packet_spraying "${1:-input/nvswitch_test.ini}"
