#!/bin/bash
# GDB调试脚本 - 用于调试NVSwitch拓扑卡死问题

BUILD_DIR="${1:-Debug}"  # 默认使用Debug构建

cd /share/zhuyu-nfs/chiplet-network-sim/builds/$BUILD_DIR

if [ ! -f "./ChipletNetworkSim" ]; then
    echo "错误: 找不到可执行文件 ./ChipletNetworkSim"
    echo "请先编译程序: cd builds/$BUILD_DIR && cmake --build ."
    exit 1
fi

echo "=== 启动GDB调试 (使用 $BUILD_DIR 构建) ==="
echo "程序将在运行10秒后自动中断并显示堆栈"
echo ""

# 创建GDB命令文件 - 使用更简单的方法
cat > /tmp/gdb_commands.txt << 'EOF'
set confirm off
set pagination off
handle SIGINT stop print
handle SIGTERM stop print
run ../../input/nvswitch_test.ini
EOF

# 在后台启动GDB
gdb -x /tmp/gdb_commands.txt ./ChipletNetworkSim > /tmp/gdb_output.txt 2>&1 &
GDB_PID=$!

# 等待10秒
sleep 10

# 发送中断信号
kill -INT $GDB_PID 2>/dev/null
sleep 1

# 获取堆栈信息
echo "=== 获取堆栈信息 ==="
gdb -batch -ex "attach $GDB_PID" \
    -ex "bt 30" \
    -ex "info threads" \
    -ex "thread apply all bt 15" \
    -ex "detach" \
    -ex "quit" ./ChipletNetworkSim 2>&1 || {
    # 如果attach失败，尝试从输出文件读取
    echo "=== 从GDB输出读取信息 ==="
    tail -100 /tmp/gdb_output.txt
}

# 清理
kill $GDB_PID 2>/dev/null
wait $GDB_PID 2>/dev/null
