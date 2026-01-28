# traffic_scale 参数说明

## 基本含义

`traffic_scale` 表示**参与流量生成的节点数量范围**，用于控制流量生成器选择源节点和目的节点的范围。

## 代码中的使用

### 1. 初始化逻辑

```cpp
// src/traffic_manager.cpp:9-10
traffic_scale_ = param->traffic_scale;
if (traffic_scale_ == 0) traffic_scale_ = network->num_cores_;
```

**关键点**：
- 如果 `traffic_scale = 0`，会自动设置为 `network->num_cores_`（网络中的核心数量）
- 如果设置为非零值，则使用该值作为节点范围

### 2. 流量生成中的使用

#### Uniform 流量模式
```cpp
// src/traffic_manager.cpp:181-188
Packet* TrafficManager::uniform_mess() {
  int src, dest;
  while (true) {
    src = gen() % traffic_scale_;   // 源节点范围: [0, traffic_scale_-1]
    dest = gen() % traffic_scale_;  // 目的节点范围: [0, traffic_scale_-1]
    if (dest != src) break;
  }
  return new Packet(network->int_to_nodeid(src), network->int_to_nodeid(dest), message_length_);
}
```

**作用**：限制随机生成的源节点和目的节点在 `[0, traffic_scale_-1]` 范围内。

#### 消息生成速率计算
```cpp
// src/traffic_manager.h:50-52
inline double message_per_cycle() const {
  return injection_rate_ * traffic_scale_ / param->packet_length;
};
```

**作用**：计算每个周期生成的消息数量
- `injection_rate_`：注入率（flits/(node*cycle)）
- `traffic_scale_`：参与流量生成的节点数
- `packet_length`：数据包长度

**公式**：每个周期的消息数 = 注入率 × 节点数 / 数据包长度

### 3. 其他流量模式中的使用

#### Hotspot 流量
```cpp
int node_per_WG = traffic_scale_ / 4;  // 将节点分成4组
src = (WG1 * node_per_WG + gen() % node_per_WG) % traffic_scale_;
```

#### Bit-complement/Reverse/Shuffle/Transpose 流量
```cpp
int bits = (int)floor(log2(traffic_scale_));  // 根据traffic_scale计算需要的比特数
```

#### Collective AllReduce 流量
```cpp
TM->data_size = TM->traffic_scale_ * (1 << i);  // 数据大小与traffic_scale相关
```

## 配置建议

### 推荐设置

1. **默认值（推荐）**：`traffic_scale = 0`
   - 自动使用所有核心节点
   - 适合大多数情况

2. **自定义值**：`traffic_scale = N`（N > 0）
   - 只使用前N个核心节点生成流量
   - 可以用于测试部分节点的性能

### 注意事项

⚠️ **重要限制**：
- `traffic_scale` **必须大于1**，否则会导致无限循环
- 当 `traffic_scale = 1` 时：
  - `gen() % 1` 总是返回 0
  - `src` 和 `dest` 总是相等
  - `while (true)` 循环无法退出
  - **程序会卡死！**

### 示例配置

```ini
[Workload]
traffic = uniform
packet_length = 1
traffic_scale = 0    # 使用所有核心（推荐）
# traffic_scale = 4  # 只使用前4个核心
# traffic_scale = 1  # ❌ 错误！会导致无限循环
```

## 实际例子

### NVSwitch 拓扑示例

假设网络配置：
- `num_gpus_per_group = 4`
- `num_groups = 1`
- `num_cores_ = 4`（只有GPU是核心）

**配置1**：`traffic_scale = 0`
- 自动设置为 `num_cores_ = 4`
- 源节点和目的节点范围：`[0, 3]`
- 所有4个GPU都会参与流量生成

**配置2**：`traffic_scale = 2`
- 源节点和目的节点范围：`[0, 1]`
- 只有前2个GPU参与流量生成
- 可以用于测试部分节点的性能

**配置3**：`traffic_scale = 1` ❌
- 源节点和目的节点范围：`[0, 0]`
- `src = 0`, `dest = 0`，总是相等
- 导致 `uniform_mess()` 中的 `while(true)` 无限循环
- **程序卡死！**

## 总结

| 参数值 | 行为 | 推荐度 |
|--------|------|--------|
| `0` | 自动使用所有核心节点 | ⭐⭐⭐ 推荐 |
| `> 1` | 使用前N个核心节点 | ⭐⭐ 可用于测试 |
| `1` | 导致无限循环，程序卡死 | ❌ 禁止使用 |

**最佳实践**：除非有特殊需求，否则使用 `traffic_scale = 0` 让系统自动设置。
