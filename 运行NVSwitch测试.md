# 运行NVSwitch测试 - 快速指南

## 方法1: 使用测试配置文件（最简单）⭐ 推荐

这是最快的方法，直接使用主程序运行测试配置：

```bash
# 1. 确保已编译（如果还没编译）
cd /share/zhuyu-nfs/chiplet-network-sim
cd builds/Release
cmake --build .

# 2. 运行测试配置
./ChipletNetworkSim ../../input/nvswitch_test.ini
```

**预期输出**：应该看到类似这样的信息：
```
NVSwitch Topology: 1 groups, 4 GPUs/group, 3 switches/group, switch_radix=10
Number of cores: 4
Number of nodes: 7
...
```

---

## 方法2: 编译并运行完整测试套件

如果你想运行完整的单元测试（包含9个测试用例）：

```bash
# 1. 配置CMake启用测试
cd /share/zhuyu-nfs/chiplet-network-sim
cmake --preset Release -DBUILD_NVSWITCH_TEST=ON

# 2. 进入构建目录
cd builds/Release

# 3. 编译测试程序
cmake --build . --target test_nvswitch

# 4. 运行测试
./test_nvswitch
```

**预期输出**：
```
========================================
NVSwitch拓扑测试套件
========================================

=== 测试1: 拓扑构建 ===
✓ 基本参数正确
✓ 组结构正确
...
========================================
✓ 所有测试通过！
========================================
```

---

## 方法3: 运行简化测试

快速验证拓扑结构：

```bash
# 1. 配置CMake启用测试
cd /share/zhuyu-nfs/chiplet-network-sim
cmake --preset Release -DBUILD_NVSWITCH_TEST=ON

# 2. 进入构建目录
cd builds/Release

# 3. 编译简化测试
cmake --build . --target test_nvswitch_simple

# 4. 运行测试
./test_nvswitch_simple
```

---

## 方法4: 使用测试脚本

```bash
cd /share/zhuyu-nfs/chiplet-network-sim
./test_nvswitch.sh
```

---

## 快速检查清单

运行测试后，检查：

- ✅ 看到 "NVSwitch Topology" 输出
- ✅ GPU和Switch数量正确
- ✅ 没有错误信息
- ✅ 数据包可以成功传输

---

## 如果遇到问题

### 问题1: 找不到可执行文件
```bash
# 确保在正确的目录
cd /share/zhuyu-nfs/chiplet-network-sim/builds/Release
ls -la ChipletNetworkSim
```

### 问题2: 找不到配置文件
```bash
# 检查配置文件是否存在
ls -la input/nvswitch_test.ini
```

### 问题3: 编译错误
```bash
# 重新配置CMake
cd /share/zhuyu-nfs/chiplet-network-sim
rm -rf builds/Release
cmake --preset Release
cd builds/Release
cmake --build .
```
