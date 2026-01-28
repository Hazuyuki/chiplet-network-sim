# 运行NVSwitch测试 - 实际步骤

## 当前状态

NVSwitch拓扑已实现，但在运行时遇到段错误，需要进一步调试。

## 推荐的测试方法

### 方法1: 使用CMake编译测试程序（推荐用于调试）

这样可以运行单元测试，逐步验证功能：

```bash
# 1. 配置CMake启用测试
cd /share/zhuyu-nfs/chiplet-network-sim
cmake --preset Release -DBUILD_NVSWITCH_TEST=ON

# 2. 进入构建目录
cd builds/Release

# 3. 编译简化测试（更容易调试）
cmake --build . --target test_nvswitch_simple

# 4. 运行测试
./test_nvswitch_simple
```

### 方法2: 使用GDB调试主程序

```bash
cd /share/zhuyu-nfs/chiplet-network-sim/builds/Release

# 使用GDB运行
gdb ./ChipletNetworkSim
(gdb) run ../../input/nvswitch_test.ini
(gdb) bt  # 查看堆栈跟踪
```

### 方法3: 检查编译输出

确保看到拓扑创建信息：

```bash
cd /share/zhuyu-nfs/chiplet-network-sim/builds/Release
./ChipletNetworkSim ../../input/nvswitch_test.ini 2>&1 | grep -i "NVSwitch\|topology\|error"
```

## 已知问题

当前存在段错误，可能的原因：
1. 节点ID设置问题（已修复，但可能还有其他问题）
2. 端口连接时的空指针
3. Switch radix不足导致数组越界

## 调试建议

### 1. 检查Switch Radix

确保 `switch_radix` 足够大：
- 需要至少：`num_gpus_per_group + num_switches_per_group - 1`
- 对于测试配置：4 + 3 - 1 = 6，而配置是10，应该足够

### 2. 添加调试输出

在 `nvswitch.cpp` 的关键位置添加 `std::cout` 输出，定位崩溃位置。

### 3. 简化配置测试

尝试最小配置：
```ini
num_gpus_per_group = 2
num_switches_per_group = 1
switch_radix = 5
```

## 下一步

1. 运行简化测试程序，看是否能通过基本测试
2. 如果简化测试通过，问题可能在连接或路由部分
3. 使用GDB定位具体的崩溃位置
4. 逐步添加调试输出，缩小问题范围

## 快速验证命令

```bash
# 验证编译
cd /share/zhuyu-nfs/chiplet-network-sim/builds/Release
ls -la ChipletNetworkSim

# 验证配置文件
cat ../../input/nvswitch_test.ini

# 尝试运行（会崩溃，但可以看到部分输出）
./ChipletNetworkSim ../../input/nvswitch_test.ini 2>&1 | head -20
```
