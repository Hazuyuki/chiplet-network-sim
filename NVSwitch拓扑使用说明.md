# NVSwitch拓扑实现说明

## 概述

本实现提供了一个类似NVIDIA NVSwitch的网络拓扑结构，支持GPU节点通过NVSwitch交换机进行全连接通信。

## 拓扑特点

1. **GPU节点（Endpoints）**: 可以注入和接收数据包的计算节点
2. **NVSwitch节点（Switches）**: 全连接的交换机，提供高带宽互连
3. **全连接结构**: 每个GPU连接到所有NVSwitch，NVSwitch之间可以全连接
4. **多组支持**: 支持多个组（类似DGX-2中的多个baseboard）

## 配置文件参数

在INI配置文件的`[Network]`部分，可以配置以下参数：

```ini
[Network]
topology = NVSwitch
num_gpus_per_group = 8          # 每个组的GPU数量
num_switches_per_group = 6       # 每个组的NVSwitch数量
num_groups = 1                   # 组的数量（如DGX-2有2个baseboard）
switch_radix = 18                # 每个NVSwitch的端口数（NVSwitch通常是18端口）
switches_fully_connected = true  # NVSwitch之间是否全连接
inter_group_sw_connect = false   # 不同组的NVSwitch是否连接
routing_algorithm = direct       # 路由算法：direct 或 min
gpu_switch_latency = 1           # GPU到NVSwitch的延迟（周期数）
switch_switch_latency = 1         # NVSwitch之间的延迟（周期数）
```

## 路由算法

### direct（直接路由）
- 确定性路由，选择固定的路径
- GPU到GPU：GPU -> Switch -> GPU
- 适用于简单的负载均衡场景

### min（最小路由）
- 负载均衡路由，尝试所有可能的路径
- 提供更好的带宽利用率
- 适用于高负载场景

## 端口分配

### GPU节点
- 每个GPU有`num_switches_per_group`个端口
- 端口`i`连接到第`i`个NVSwitch

### NVSwitch节点
- 端口0到`num_gpus_per_group-1`: 连接到GPU
- 端口`num_gpus_per_group`到`num_gpus_per_group + num_switches_per_group - 2`: 连接到其他NVSwitch（如果启用全连接）
- 端口`num_gpus_per_group + num_switches_per_group - 1`及以后: 用于组间连接（如果启用）

## 使用示例

### 基本配置（单组，类似单个baseboard）
```ini
[Network]
topology = NVSwitch
num_gpus_per_group = 8
num_switches_per_group = 6
num_groups = 1
switch_radix = 18
switches_fully_connected = true
inter_group_sw_connect = false
routing_algorithm = direct
```

### 多组配置（类似DGX-2，2个baseboard）
```ini
[Network]
topology = NVSwitch
num_gpus_per_group = 8
num_switches_per_group = 6
num_groups = 2
switch_radix = 18
switches_fully_connected = true
inter_group_sw_connect = true
routing_algorithm = min
```

## 注意事项

1. **Switch Radix要求**: 
   - 如果启用全连接，switch_radix必须至少为：`num_gpus_per_group + num_switches_per_group - 1`
   - 如果启用组间连接，需要额外的端口

2. **组间路由**: 
   - 如果`inter_group_sw_connect = false`，不同组之间的GPU无法直接通信
   - 需要启用`inter_group_sw_connect = true`才能支持组间通信

3. **路由算法选择**:
   - `direct`: 简单快速，但可能造成负载不均
   - `min`: 更好的负载均衡，但计算开销稍大

## 编译和运行

1. 确保代码已添加到`src/system.cpp`中（已完成）
2. 编译项目：
```bash
cmake --preset Release
cd builds/Release/
cmake --build .
```

3. 运行模拟：
```bash
./ChipletNetworkSim ../../input/nvswitch.ini
```

## 扩展建议

如果需要更复杂的NVSwitch拓扑，可以考虑：

1. **非全连接NVSwitch**: 修改`connect_switches()`实现部分连接
2. **更复杂的组间连接**: 实现更灵活的组间连接策略
3. **自适应路由**: 根据网络负载动态选择路径
4. **多级NVSwitch**: 支持多级NVSwitch层次结构

## 与真实NVSwitch的对应关系

- **GPU节点**: 对应NVIDIA GPU
- **NVSwitch节点**: 对应NVIDIA NVSwitch芯片
- **组（Group）**: 对应DGX-2中的baseboard
- **全连接**: 对应NVSwitch的18x18交叉开关

## 性能特性

- **带宽**: 每个GPU到每个NVSwitch都有专用链路
- **延迟**: 可配置GPU-Switch和Switch-Switch延迟
- **可扩展性**: 支持多个组，每个组可以有不同数量的GPU和Switch
