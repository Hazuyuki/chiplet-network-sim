# Collective Ring All-Reduce 功能说明

## 概述

`collective_ring_all_reduce` 是 NVSwitch 拓扑下的 Ring All-Reduce 集体通信模式实现。

## 实现原理

### Ring All-Reduce 算法

Ring All-Reduce 分两个阶段：
1. **Scatter-Reduce 阶段**：`(n-1)` 步
2. **All-Gather 阶段**：`(n-1)` 步

总传输量 = `data_size × (n-1) × 2`

### 吞吐率计算

```
throughput = data_size × (n-1) × 2 / (cycles × n)
```

## 配置示例

```ini
[Network]
topology = NVSwitch
routing_algorithm = min    # 使用 min 路由实现 Packet Spraying

[Workload]
traffic = collective_ring_all_reduce
traffic_scale = 8
data_size = 4096           # 数据量 (flits)
```

## 实验结果

### 路由算法对比

| 路由算法 | 端口使用 | 吞吐率 |
|----------|----------|--------|
| direct   | 单端口   | 0.99   |
| min      | 多端口   | 17+    |

### data_size 扫描

| data_size | throughput |
|-----------|------------|
| 1,024     | 3.28       |
| 4,096     | 11.22      |
| 16,384    | 14.30      |
| 65,536    | 16.64      |
| 131,072   | 17.34      |

## 理论带宽利用率

Ring All-Reduce 理论带宽利用率 = `(n-1)/n`

| GPU 数量 | 理论利用率 |
|----------|-----------|
| 8        | 87.5%     |
| 16       | 93.75%    |
| 32       | 96.875%   |
