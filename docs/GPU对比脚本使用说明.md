# CNSim GPU 对比脚本文档

## 一、概述

本文档介绍用于将 CNSim 仿真结果与 GPU 实际通信性能进行对比的脚本工具。

### 背景

CNSim（Chiplet Network Simulator）是一个周期精确的网络仿真器，用于评估大规模 GPU 互连网络的性能。为了将仿真结果与真实 GPU（如 A800 + NVLink）的通信时间对齐，需要：

1. 理解 CNSim 的输入参数与实际 GPU 通信的对应关系
2. 运行仿真获取饱和点带宽和延迟
3. 将仿真结果转换为实际物理量（GB/s、ns）

---

## 二、脚本说明

### 2.1 cnsim_saturation.py

饱和点扫描脚本，利用 CNSim 自身的 injection rate 扫描功能，找到饱和点后计算带宽和延迟。

#### 功能

- 解析 CNSim 配置文件
- 读取仿真输出 CSV 文件
- 自动判定饱和点（延迟增长或吞吐率下降）
- 计算带宽（GB/s）和延迟（ns）
- 根据数据量计算 packet 数量

#### 用法

```bash
python cnsim_saturation.py <config.ini> [options]

# 示例
python cnsim_saturation.py input/nvswitch_allreduce.ini -d 256MB -g 1500 -n 8
```

#### 参数

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `config` | CNSim 配置文件路径 | 必填 |
| `-d, --data-size` | GPU 通信数据大小（如 256MB, 1GB） | 256MB |
| `-g, --gpu-clock` | GPU 时钟频率（MHz） | 1500 |
| `-n, --num-gpus` | GPU 数量 | 8 |
| `-b, --binary` | CNSim 可执行文件路径 | ./ChipletNetworkSim |
| `-o, --output` | 输出 CSV 文件路径 | - |
| `--run` | 运行 CNSim 仿真（默认只解析已有输出） | False |

#### 输出示例

```
============================================================
CNSim 饱和点扫描结果
============================================================
配置文件: input/nvswitch_allreduce.ini
GPU 数量: 8
GPU 时钟: 1500 MHz
数据大小: 256.00 MB (268435456 bytes)
------------------------------------------------------------
Packet Length: 64 flits/packet
Packet Count:  262144 packets
Flit Size:    16 bytes/flit
------------------------------------------------------------
Injection Rate: 18.0500 flits/(node*cycle)
Throughput:     15.7217 flits/(node*cycle)
Latency:        250.12 cycles
------------------------------------------------------------
>>> 带宽: 3018.57 GB/s
>>> 延迟: 166.75 ns (0.1667 us)
============================================================

###RESULT###
BANDWIDTH_GBPS=3018.57
LATENCY_NS=166.75
INJECTION_RATE=18.0500
THROUGHPUT=15.7217
PACKET_LENGTH=64
PACKET_COUNT=262144
############
```

---

## 三、关键概念

### 3.1 CNSim 数据单元

| 层级 | CNSim 参数 | 大小 |
|------|------------|------|
| Flit | - | 16 bytes（固定） |
| Packet | `packet_length` | 可变（flits 数） |
| 数据量 | 用户输入 | 可变（如 256MB） |

**packet 数量计算**：
```
packet_count = data_size_bytes / (packet_length × 16)

示例：256MB, packet_length=64
packet_count = 268435456 / (64 × 16) = 262144
```

### 3.2 Injection Rate

CNSim 中的注入率定义：
```
injection_rate = flits_per_cycle / num_nodes

单位：flits/(node·cycle)
```

### 3.3 带宽和延迟计算

```python
# 带宽 = throughput × num_gpus × flit_size × clock
bandwidth_gbps = throughput * num_gpus * 16 * gpu_clock_mhz * 1e6 / 1e9

# 延迟 = latency_cycles / clock
latency_ns = latency_cycles / gpu_clock_mhz * 1e3
```

---

## 四、配置文件示例

### 4.1 packet_length = 1（默认）

```ini
[Network]
topology = NVSwitch
num_gpus_per_server = 8
num_switches_per_server = 4
gpu_nvlink_ports = 18
flow_control = credit
credit_return_delay = 2
buffer_size = 64
switch_buffer_size = 2048

[Workload]
traffic = ring_all_reduce
packet_length = 1

[Simulation]
simulation_time = 2000
start_injection = 0.05
injection_increment = 0.5
max_injection = 18
```

### 4.2 packet_length = 64（更大数据包）

```ini
[Network]
topology = NVSwitch
num_gpus_per_server = 8
num_switches_per_server = 4
gpu_nvlink_ports = 18
flow_control = credit
credit_return_delay = 2
buffer_size = 64
switch_buffer_size = 2048

[Workload]
traffic = ring_all_reduce
packet_length = 64

[Simulation]
simulation_time = 2000
start_injection = 0.05
injection_increment = 0.5
max_injection = 18
```

---

## 五、GPU 实测对齐方法

### 5.1 获取 GPU 通信数据

使用 NCCL 或 NSYS 获取实际通信时间：

```bash
# NCCL 性能测试
nccl-tests/all_reduce_perf

# NSYS Profiling
nsys profile -o profile ./your_program
```

### 5.2 参数映射

| GPU 实测 | CNSim 输入 |
|---------|-----------|
| 数据大小（bytes） | `packet_length` + 数据量 |
| 通信时间（μs） | 仿真 saturation point |
| GPU 时钟（MHz） | `-g` 参数 |

### 5.3 对齐步骤

1. **运行 CNSim 仿真**：
   ```bash
   python cnsim_saturation.py input/nvswitch_allreduce.ini \
       -d 256MB -g 1500 -n 8 --run
   ```

2. **获取 GPU 实测数据**：
   - 数据量：D bytes
   - 通信时间：T μs

3. **对比分析**：
   - 调整 CNSim 参数（`credit_return_delay`, `buffer_size` 等）
   - 找到与实测最匹配的仿真结果

---

## 六、饱和点判定逻辑

脚本使用以下逻辑判定饱和点：

```python
for i in range(1, len(data_points)):
    prev_rate, prev_lat, prev_throughput = data_points[i-1]
    curr_rate, curr_lat, curr_throughput = data_points[i]
    
    # 条件1: 延迟增长超过 1.5 倍
    if prev_lat > 0 and curr_lat / prev_lat > 1.5:
        saturation_idx = i - 1
        break
    
    # 条件2: 吞吐率开始下降
    if curr_throughput < prev_throughput * 0.95:
        saturation_idx = i - 1
        break
```

---

## 七、代码修改

### 7.1 Credit 等待逻辑

修改了 `buffer.cpp`，添加等待逻辑避免 credit 竞态条件导致的 assert 崩溃：

```cpp
// buffer.cpp - allocate_in_link
const int max_wait = 10000;
int wait_count = 0;
while (!upstream_node_->has_credit(upstream_port_, vcb, p.length_)) {
    if (++wait_count >= max_wait) {
        in_link_used_.store(false);
        return false;
    }
    std::this_thread::yield();
}
```

### 7.2 移除 consume_credit 中的 assert

修改了 `node.cpp`，移除 assert 允许 credit 为负（临时状态）：

```cpp
void Node::consume_credit(int port, int vcb, int n) {
    // 使用原子操作扣减 credit
    while (!credits_[idx].compare_exchange_weak(c, c - n)) {
        // 继续尝试
    }
    // 移除 assert
}
```

---

## 八、常见问题

### Q1: packet_length 应该设置多大？

建议根据实际网络包大小设置：
- 较小值（1-16）：仿真速度快，但与实际 GPU 行为差异大
- 较大值（64-256）：更接近实际 GPU 通信

### Q2: 仿真结果与 GPU 实测不匹配怎么办？

1. 调整 `credit_return_delay`：模拟 RTT 延迟
2. 调整 `buffer_size`：影响排队延迟
3. 调整 `gpu_nvlink_ports`：改变链路数量

### Q3: 仿真器崩溃怎么办？

可能需要增加等待次数或调整 buffer 大小。

---

## 九、参考

- CNSim 论文：Yinxiao Feng 等人，USENIX ATC 2024
- A800 规格：12 NVLink，600 GB/s 总带宽
- NCCL 文档：默认 chunk size = 512KB

---

**文档版本**: 1.0
**更新日期**: 2026-03-17
