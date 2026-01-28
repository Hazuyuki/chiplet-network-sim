# NVSwitch拓扑测试说明

## 测试文件说明

### 1. 完整测试套件 (`src/topologies/test_nvswitch.cpp`)

这是一个完整的测试程序，包含以下测试：

- **测试1: 拓扑构建** - 验证拓扑参数和结构
- **测试2: GPU到Switch连接** - 验证GPU与Switch之间的连接
- **测试3: Switch之间连接** - 验证Switch之间的全连接
- **测试4: Direct路由算法** - 测试直接路由功能
- **测试5: MIN路由算法** - 测试负载均衡路由
- **测试6: 多组拓扑** - 测试多组配置
- **测试7: 数据包传输** - 端到端传输测试
- **测试8: NodeID转换** - 测试ID转换功能
- **测试9: 边界情况** - 测试最小配置

### 2. 简化测试 (`src/topologies/test_nvswitch_simple.cpp`)

快速验证拓扑结构的简化版本，不需要完整的模拟器环境。

### 3. 测试脚本 (`test_nvswitch.sh`)

自动化测试脚本，用于运行基本功能测试。

### 4. 测试配置文件 (`input/nvswitch_test.ini`)

用于测试的简化配置文件。

## 编译和运行测试

### 方法1: 编译完整测试套件

```bash
# 在项目根目录
cd builds/Release/  # 或 Debug
g++ -std=c++17 -I../src -I../src/topologies \
    ../src/topologies/test_nvswitch.cpp \
    ../src/topologies/nvswitch.cpp \
    ../src/system.cpp \
    ../src/group.cpp \
    ../src/node.cpp \
    ../src/buffer.cpp \
    ../src/packet.cpp \
    ../src/config.cpp \
    ../src/traffic_manager.cpp \
    -lboost_system -lboost_filesystem \
    -o test_nvswitch

# 运行测试
./test_nvswitch
```

### 方法2: 使用CMake添加测试目标

在`CMakeLists.txt`中添加：

```cmake
# 添加测试可执行文件
add_executable(test_nvswitch 
    src/topologies/test_nvswitch.cpp
    src/topologies/nvswitch.cpp
    ${SRC_FILES}
    ${NETRACE_FILES}
    ${TOPOLOGY_FILES}
    ${TRAFFIC_FILES}
)
target_include_directories(test_nvswitch PUBLIC src src/netrace src/topologies src/traffic)
target_link_libraries(test_nvswitch ${Boost_LIBRARIES})
```

然后编译和运行：

```bash
cmake --build builds/Release --target test_nvswitch
./builds/Release/test_nvswitch
```

### 方法3: 使用测试脚本

```bash
chmod +x test_nvswitch.sh
./test_nvswitch.sh
```

### 方法4: 直接运行模拟器测试

```bash
# 使用测试配置文件
cd builds/Release
./ChipletNetworkSim ../../input/nvswitch_test.ini
```

## 测试覆盖范围

### 结构测试
- ✓ 拓扑参数正确性
- ✓ 节点创建和ID分配
- ✓ 组结构正确性
- ✓ GPU和Switch节点属性

### 连接测试
- ✓ GPU到Switch全连接
- ✓ Switch之间全连接
- ✓ 端口映射正确性
- ✓ Buffer连接正确性

### 路由测试
- ✓ Direct路由算法
- ✓ MIN路由算法
- ✓ 候选通道生成
- ✓ 路径选择正确性

### 功能测试
- ✓ 数据包创建和初始化
- ✓ 数据包传输
- ✓ 多组支持
- ✓ NodeID转换

### 边界测试
- ✓ 最小配置
- ✓ 单GPU单Switch
- ✓ 大配置（如果资源允许）

## 预期输出

### 成功运行的输出示例：

```
========================================
NVSwitch拓扑测试套件
========================================

=== 测试1: 拓扑构建 ===
✓ 基本参数正确
✓ 组结构正确
✓ GPU节点创建正确
✓ Switch节点创建正确
✓ 测试1通过

=== 测试2: GPU到Switch连接 ===
✓ 所有GPU都正确连接到所有Switch
✓ 测试2通过

...

========================================
✓ 所有测试通过！
========================================
```

## 故障排查

### 常见问题

1. **编译错误: 找不到头文件**
   - 确保包含路径正确
   - 检查所有依赖文件是否存在

2. **运行时错误: 配置文件不存在**
   - 测试程序会在当前目录创建临时配置文件
   - 确保有写入权限

3. **断言失败**
   - 检查具体的断言失败信息
   - 验证拓扑参数是否合理
   - 检查Switch radix是否足够大

4. **连接验证失败**
   - 检查端口分配逻辑
   - 验证Switch radix是否满足连接需求

## 扩展测试

### 添加自定义测试

在`test_nvswitch.cpp`中添加新的测试函数：

```cpp
void test_custom_feature() {
    std::cout << "\n=== 测试X: 自定义功能 ===" << std::endl;
    
    // 测试代码
    
    std::cout << "✓ 测试X通过" << std::endl;
}
```

然后在`main()`函数中调用。

### 性能测试

可以添加性能测试来测量：
- 拓扑构建时间
- 路由算法执行时间
- 数据包传输延迟

### 压力测试

测试大规模配置：
- 大量GPU和Switch
- 多组配置
- 长时间运行

## 持续集成

可以将测试集成到CI/CD流程中：

```yaml
# 示例 GitHub Actions
- name: Build and Test
  run: |
    cmake --preset Release
    cmake --build builds/Release --target test_nvswitch
    ./builds/Release/test_nvswitch
```

## 测试最佳实践

1. **每次修改后运行测试** - 确保新功能不破坏现有功能
2. **测试边界情况** - 包括最小和最大配置
3. **验证输出** - 不仅检查是否通过，还要验证结果的正确性
4. **保持测试更新** - 当添加新功能时，添加相应的测试
5. **文档化测试** - 清楚说明每个测试的目的和预期结果
