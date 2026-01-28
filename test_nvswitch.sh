#!/bin/bash
# NVSwitch拓扑测试脚本

echo "=========================================="
echo "NVSwitch拓扑测试"
echo "=========================================="

# 检查是否在正确的目录
if [ ! -f "CMakeLists.txt" ]; then
    echo "错误: 请在项目根目录运行此脚本"
    exit 1
fi

# 创建测试构建目录
mkdir -p test_build
cd test_build

# 配置CMake（如果需要）
if [ ! -f "CMakeCache.txt" ]; then
    cmake .. -DCMAKE_BUILD_TYPE=Debug
fi

# 编译测试程序
echo ""
echo "编译测试程序..."
cmake --build . --target ChipletNetworkSim

if [ $? -ne 0 ]; then
    echo "编译失败！"
    exit 1
fi

# 运行基本功能测试
echo ""
echo "=========================================="
echo "运行基本功能测试"
echo "=========================================="

# 测试1: 使用配置文件运行模拟器
echo ""
echo "测试1: 配置文件测试"
if [ -f "../input/nvswitch.ini" ]; then
    timeout 30 ./ChipletNetworkSim ../input/nvswitch.ini > test_output.log 2>&1
    if [ $? -eq 0 ]; then
        echo "✓ 配置文件测试通过"
    else
        echo "✗ 配置文件测试失败"
        cat test_output.log
    fi
else
    echo "✗ 找不到配置文件: input/nvswitch.ini"
fi

# 测试2: 验证拓扑创建
echo ""
echo "测试2: 拓扑创建验证"
echo "检查输出中是否包含正确的拓扑信息..."

if grep -q "NVSwitch Topology" test_output.log 2>/dev/null; then
    echo "✓ 拓扑信息输出正确"
else
    echo "✗ 未找到拓扑信息"
fi

# 测试3: 验证没有错误
echo ""
echo "测试3: 错误检查"
if grep -qi "error\|fatal\|exception" test_output.log 2>/dev/null; then
    echo "✗ 发现错误信息:"
    grep -i "error\|fatal\|exception" test_output.log | head -5
else
    echo "✓ 未发现错误"
fi

# 清理
echo ""
echo "=========================================="
echo "测试完成"
echo "=========================================="
echo "测试日志保存在: test_build/test_output.log"

cd ..
