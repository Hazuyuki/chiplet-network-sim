# 如何运行NVSwitch测试

## 当前状态

✅ **编译成功** - 测试程序可以编译
❌ **运行时错误** - 存在段错误，需要进一步调试

## 运行测试的步骤

### 步骤1: 编译测试程序

```bash
cd /share/zhuyu-nfs/chiplet-network-sim
cmake --preset Release -DBUILD_NVSWITCH_TEST=ON
cd builds/Release
cmake --build . --target test_nvswitch_simple
```

### 步骤2: 运行测试

```bash
./test_nvswitch_simple
```

**当前输出**：
- ✅ 配置文件读取成功
- ✅ 参数解析成功
- ❌ 在拓扑创建时发生段错误

## 调试建议

### 方法1: 使用GDB调试

```bash
cd /share/zhuyu-nfs/chiplet-network-sim/builds/Release
gdb ./test_nvswitch_simple
(gdb) run
(gdb) bt  # 崩溃后查看堆栈
```

### 方法2: 检查配置文件

确保配置文件格式正确：
```bash
cat ../../input/nvswitch_test.ini
```

### 方法3: 简化配置测试

尝试最小配置，看是否能定位问题：
- 减少GPU数量
- 减少Switch数量
- 增加Switch radix

## 已知问题

1. **段错误位置**：在拓扑创建过程中（`NVSwitchSystem`构造函数）
2. **可能原因**：
   - 节点ID设置问题
   - 端口连接时的空指针
   - Switch radix不足
   - 内存分配问题

## 下一步

1. 使用GDB定位具体的崩溃位置
2. 在关键位置添加调试输出
3. 逐步验证每个组件（节点创建、连接、路由）

## 快速验证命令

```bash
# 验证编译
ls -la test_nvswitch_simple

# 运行测试（会崩溃，但可以看到部分输出）
./test_nvswitch_simple 2>&1 | head -20

# 使用GDB调试
gdb ./test_nvswitch_simple
```

## 替代测试方法

如果测试程序有问题，可以直接测试主程序：

```bash
# 运行主程序（也会崩溃，但可以看到拓扑创建信息）
./ChipletNetworkSim ../../input/nvswitch_test.ini 2>&1 | head -30
```

这样可以验证：
- ✅ 配置文件读取
- ✅ 拓扑创建
- ✅ 基本结构
