#!/bin/bash
# 简单的GDB调试脚本

cd /share/zhuyu-nfs/chiplet-network-sim/builds/Debug

echo "=== 启动GDB交互式调试 ==="
echo "使用以下命令："
echo "  (gdb) run ../../input/nvswitch_test.ini"
echo "  等待程序运行几秒后，按 Ctrl+C 中断"
echo "  (gdb) bt          # 查看堆栈"
echo "  (gdb) info threads # 查看所有线程"
echo "  (gdb) thread apply all bt  # 查看所有线程堆栈"
echo "  (gdb) quit"
echo ""

gdb ./ChipletNetworkSim
