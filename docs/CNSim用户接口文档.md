# CNSim 用户接口文档

## 一、概述

CNSim（Chiplet Network Simulator）是一个周期精确的并行网络仿真器，支持多种网络拓扑（NVSwitch、Mesh、FatTree、Dragonfly 等），可用于评估片上/片间互连网络的性能。

## 二、配置文件格式

CNSim 使用 INI 格式的配置文件，文件结构如下：

```ini
[Network]
# 网络拓扑参数

[Workload]
# 流量负载参数

[Simulation]
# 仿真参数

[Files]
# 文件输入输出参数
```

## 三、网络参数 [Network]

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `topology` | string | - | 拓扑类型：`NVSwitch`、`SingleChipMesh`、`DragonflySW`、`DragonflyChiplet`、`FatTree`、`HammingMesh`、`RailX` |
| `num_gpus_per_group` | int | 8 | 每个 GPU 组的 GPU 数量（NVSwitch 拓扑） |
| `num_switches_per_group` | int | 4 | 每个 GPU 组的交换机数量（NVSwitch 拓扑） |
| `num_groups` | int | 1 | GPU 组数量 |
| `gpu_nvlink_ports` | int | 18 | 每个 GPU 的 NVLink 端口数 |
| `switches_fully_connected` | bool | true | 交换机是否全互联 |
| `spine_non_blocking` | bool | true | Spine 层是否非阻塞 |
| `routing_algorithm` | string | min | 路由算法：`min`（最小路径）、`valiant` |
| `gpu_switch_latency` | int | 1 | GPU 到交换机链路延迟（cycles） |
| `switch_switch_latency` | int | 1 | 交换机间链路延迟（cycles） |
| `buffer_size` | int | 64 | 节点缓冲区大小（flits） |
| `switch_buffer_size` | int | 256 | 交换机缓冲区大小（flits），<=0 表示与 buffer_size 相同 |
| `vc_number` | int | 2 | 虚拟通道数量 |
| `router_stages` | string | OneStage | 路由器流水线阶段数：`OneStage`、`TwoStage`、`ThreeStage` |
| `flow_control` | string | credit | 流控策略：`buffer`（接收端预留）、`credit`（发送端 credit，NVSwitch 风格） |
| `credit_return_delay` | int | 4 | Credit 回报延迟（cycles），0=立即回报，>0 模拟 RTT |
| `processing_time` | int | 1 | 包处理时间（cycles） |

## 四、流量参数 [Workload]

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `traffic` | string | test | 流量模式 |
| `packet_length` | int | 1 | 数据包长度（flits） |
| `traffic_scale` | int | 0 | 流量缩放因子 |
| `single_flow_dest` | int | 1 | single_flow 模式下的目的节点 |

### 支持的流量模式

| 流量模式 | 说明 |
|---------|------|
| `test` | 测试流量 |
| `uniform` | 均匀随机流量 |
| `single_flow` | 单流（源到单一目的节点） |
| `hotspot` | 热点流量 |
| `bitcomplement` | 位补 traffic |
| `bittranspose` | 位转置 traffic |
| `bitreverse` | 位反转 traffic |
| `bitshuffle` | 位打乱 traffic |
| `adversarial` | 对抗性流量 |
| `sd_trace` | SD 追踪流量 |
| `netrace` | Netrace 追踪流量 |
| `ring_all_reduce` | Ring All-Reduce 流量 |
| `ring_all_reduce_bi` | 双向 Ring All-Reduce |

## 五、仿真参数 [Simulation]

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `simulation_time` | uint64 | 2000 | 仿真周期数 |
| `start_injection` | double | 0.05 | 起始注入率（flits/cycle/port） |
| `injection_increment` | double | 0.5 | 注入率增量 |
| `max_injection` | double | 18 | 最大注入率，0=不强制 |
| `timeout_threshold` | int | 2000 | 超时阈值（cycles） |
| `timeout_limit` | int | 200 | 超时包数量限制 |
| `threads` | int | 8 | 并行线程数 |
| `issue_width` | int | 32 | 每次发射的包数量 |

## 六、文件参数 [Files]

| 参数 | 类型 | 说明 |
|------|------|------|
| `trace_file` | string | 追踪文件路径 |
| `netrace_file` | string | Netrace 文件路径 |
| `output_file` | string | 输出 CSV 文件路径 |
| `log_file` | string | 日志文件路径 |

## 七、配置示例

### 8 GPU All-Reduce 配置

```ini
[Network]
topology = NVSwitch
num_gpus_per_group = 8
num_switches_per_group = 4
num_groups = 1
gpu_nvlink_ports = 18
switches_fully_connected = true
spine_non_blocking = true
routing_algorithm = min
gpu_switch_latency = 1
switch_switch_latency = 1
buffer_size = 64
switch_buffer_size = 2048
vc_number = 2
flow_control = credit
credit_return_delay = 2

[Workload]
traffic = ring_all_reduce
packet_length = 1
traffic_scale = 0

[Simulation]
simulation_time = 2000
start_injection = 0.05
injection_increment = 0.5
max_injection = 18
timeout_threshold = 2000
timeout_limit = 200
threads = 8
issue_width = 32

[Files]
output_file = output/nvswitch_allreduce.csv
log_file = output/nvswitch_allreduce.log
```

### 单流配置

```ini
[Network]
topology = NVSwitch
num_gpus_per_group = 8
num_switches_per_group = 4
num_groups = 1
gpu_nvlink_ports = 18
switches_fully_connected = true
routing_algorithm = min
buffer_size = 128
switch_buffer_size = 256
vc_number = 2
flow_control = credit
credit_return_delay = 4

[Workload]
traffic = single_flow
packet_length = 1
single_flow_dest = 7

[Simulation]
simulation_time = 1000
start_injection = 0.1
injection_increment = 0.1
max_injection = 1.0

[Files]
output_file = output/single_flow.csv
```

## 八、性能调优建议

### 1. 吞吐率优化

- **增大 `switch_buffer_size`**：交换机缓冲区越大，拥塞时性能越好（如 2048）
- **减小 `credit_return_delay`**：减少 credit 回报延迟可提高吞吐量（如设为 2）
- **增加 `vc_number`**：更多虚拟通道可减少拥塞

### 2. 延迟优化

- **减小 `buffer_size`**：小缓冲区减少排队延迟
- **减小 `processing_time`**：减少包处理时间
- **使用 `OneStage` 路由器**：减少流水线阶段数

### 3. 仿真速度优化

- **增加 `threads`**：更多并行线程加速仿真
- **增大 `issue_width`**：每次发射更多包减少仿真周期
- **减小 `simulation_time`**：减少仿真周期数

## 九、输出格式

仿真结果输出为 CSV 格式，包含以下列：

| 列名 | 说明 |
|------|------|
| Injection Rate | 注入率（flits/cycle/port） |
| Average Latency | 平均延迟（cycles） |
| Throughput | 吞吐率（flits/cycle） |
| Completed Packets | 完成的包数量 |
| Timeout Packets | 超时包数量 |

## 十、运行方法

```bash
cd build
./ChipletNetworkSim ../input/nvswitch_allreduce.ini
```

---

**文档版本**: 1.0  
**更新日期**: 2026-02-14
