# GPU NCCL All-Reduce 性能测试报告

## 1. 实验概述

本实验旨在测量 NVIDIA A800 GPU 之间通过 NVLink 进行 NCCL All-Reduce 通信的实际带宽性能，为 CNSim（Chiplet Network Simulator）仿真结果与真实 GPU 通信性能的对齐提供参考数据。

### 1.1 硬件环境

- **GPU 型号**: NVIDIA A800-SXM4-80GB (8 块)
- **NVLink**: 第三代 NVLink，每个 GPU 有 8 条链路
- **理论带宽**:
  - 单向: 200 GB/s (8 × 25 GB/s per direction)
  - 双向: 400 GB/s (8 × 50 GB/s)

### 1.2 软件环境

- **容器**: `pytorch/pytorch:2.5.1-cuda12.1-cudnn9-runtime`
- **PyTorch 版本**: 2.5.1
- **CUDA 版本**: 12.1
- **NCCL 版本**: 2.21.5

## 2. 实验方法

### 2.1 测试原理

All-Reduce 是分布式训练中常用的集合通信操作，其中 Double-Ring 算法是一种高效的实现方式。以 4 个 GPU 为例：

1. **Reduce-Scatter 阶段**: 将数据分成 N 份，每份 D/N 字节，进行 N-1 轮通信
2. **All-Gather 阶段**: 重复 N-1 轮通信，最终每个 GPU 获得完整的归约结果

对于 N 个 GPU，总通信轮数为 2(N-1)。

### 2.2 带宽计算方法

```python
# 单向带宽（每个 GPU 视角）
bandwidth_gbps = (data_size * 2) / (time_ms / 1000) / 1e9
```

其中：
- `data_size * 2` 表示 All-Reduce 中 Reduce-Scatter + All-Gather 两个阶段各发送一次
- `time_ms / 1000` 将毫秒转换为秒
- `1e9` 将 bytes/s 转换为 GB/s

### 2.3 测试脚本

#### 2.3.1 Python Profiling 脚本

`gpu_profiling/src/nccl_profiling.py` 是核心测试脚本，使用 PyTorch 的 NCCL 后端进行 All-Reduce 通信测试。

关键特性：
- 使用 `torchrun` 启动多进程，每个进程绑定一个 GPU
- 使用 `torch.cuda.cudart().cudaProfilerStart()` 和 `cudaProfilerStop()` 标记 profiling 范围
- 支持自定义数据大小、迭代次数、预热次数

#### 2.3.2 批量测试脚本

`gpu_profiling/scripts/batch_profiling.sh` (4 GPU) 和 `batch_profiling_8gpu.sh` (8 GPU) 用于自动化批量测试。

功能：
- 测试多个数据大小 (1MB, 4MB, 16MB, 64MB, 256MB, 1024MB)
- 每次测试先运行基准测试获取时间，再运行 nsys 生成报告
- 自动汇总结果并计算效率

#### 2.3.3 nsys 集成

测试使用 NVIDIA Nsight Systems (nsys) 进行性能分析：

```bash
nsys profile -o output_report \
             -f true \
             -c cudaProfilerApi \
             --capture-range-end stop \
             -x true \
             torchrun --nproc_per_node=4 nccl_profiling.py -s 256MB
```

关键参数：
- `-c cudaProfilerApi`: 使用 CUDA Profiler API 捕获 GPU 活动
- `--capture-range-end stop`: 在代码调用 `cudaProfilerStop()` 时停止捕获
- `-x true`: 生成 XML 格式的输出

## 3. 实验结果

### 3.1 4 GPU 测试结果

| 数据大小 | 平均时间 (ms) | 带宽 (GB/s) | 效率 (相对 200 GB/s) |
|----------|---------------|-------------|---------------------|
| 1 MB     | 0.092         | 22.74       | 11.4%              |
| 4 MB     | 0.159         | 52.74       | 26.4%              |
| 16 MB    | 0.280         | 119.68      | 59.8%              |
| 64 MB    | 0.804         | 167.01      | 83.5%              |
| 256 MB   | 2.894         | 185.52      | 92.8%              |
| 1024 MB  | 10.850        | 197.92      | **99.0%**          |

### 3.2 8 GPU 测试结果

| 数据大小 | 平均时间 (ms) | 带宽 (GB/s) | 效率 (相对 200 GB/s) |
|----------|---------------|-------------|---------------------|
| 1 MB     | 0.100         | 21.02       | 10.5%              |
| 4 MB     | 0.140         | 59.85       | 29.9%              |
| 16 MB    | 0.335         | 100.29      | 50.1%              |
| 64 MB    | 0.921         | 145.80      | 72.9%              |
| 256 MB   | 3.261         | 164.65      | 82.3%              |
| 1024 MB  | 12.369        | 173.62      | 86.8%              |

### 3.3 4 GPU vs 8 GPU 对比

| 数据大小 | 4 GPU 带宽 | 8 GPU 带宽 | 差异    |
|----------|------------|------------|---------|
| 1 MB     | 22.74 GB/s | 21.02 GB/s | -7.6%  |
| 4 MB     | 52.74 GB/s | 59.85 GB/s | +13.5% |
| 16 MB    | 119.68 GB/s | 100.29 GB/s | -16.2% |
| 64 MB    | 167.01 GB/s | 145.80 GB/s | -12.7% |
| 256 MB   | 185.52 GB/s | 164.65 GB/s | -11.2% |
| 1024 MB  | 197.92 GB/s | 173.62 GB/s | -12.3% |

## 4. 结果分析

### 4.1 带宽趋势

1. **小数据量 (1-4 MB)**：带宽较低，因为通信启动开销（握手、调度等）占主导
2. **中等数据量 (16-64 MB)**：带宽快速上升，开始接近硬件带宽
3. **大数据量 (256 MB+)**：带宽趋于稳定，达到硬件带宽的 80-99%

### 4.2 GPU 数量影响

8 GPU 相比 4 GPU 带宽下降的原因：
- Double-Ring All-Reduce 的通信轮数随 GPU 数量增加：4 GPU 需 3 轮，8 GPU 需 7 轮
- 更多通信轮次意味着更多的启动开销和同步等待

### 4.3 效率分析

- **4 GPU 最大效率**: 99.0% (1024 MB)
- **8 GPU 最大效率**: 86.8% (1024 MB)

大数据量时效率接近理论上限，证明 NVLink 通信效率非常高。

## 5. 与 CNSim 对齐

### 5.1 带宽单位转换

CNSim 输出单位为 flits/(node·cycle)，需要转换为 GB/s：

```python
# 带宽 = throughput * num_gpus * flit_size * clock
FLIT_SIZE = 16  # bytes
clock_hz = gpu_clock_mhz * 1e6
bandwidth = (result.throughput * num_gpus * FLIT_SIZE * clock_hz) / 1e9  # GB/s
```

### 5.2 延迟单位转换

CNSim 输出为 cycle，需要转换为 ns：

```python
# 延迟 = cycles * clock_period
clock_period_ns = 1 / (gpu_clock_mhz * 1e6) * 1e9  # ns
latency_ns = result.latency * clock_period_ns
```

### 5.3 饱和点对齐

实验中观察到的饱和行为：
- 4 GPU: 在测试范围内未观察到明显饱和
- 8 GPU: 带宽随数据量增加趋于稳定

CNSim 的饱和点分析可用于预测更大规模 GPU 集群的通信性能。

## 6. 生成的文件

### 6.1 脚本文件

- `gpu_profiling/src/nccl_profiling.py` - NCCL All-Reduce Profiling 主脚本
- `gpu_profiling/scripts/run_profiling.sh` - 单次运行脚本
- `gpu_profiling/scripts/batch_profiling.sh` - 4 GPU 批量测试脚本
- `gpu_profiling/scripts/batch_profiling_8gpu.sh` - 8 GPU 批量测试脚本

### 6.2 测试结果

- `gpu_profiling/results/batch_20260318_083530/` - 4 GPU 测试结果
- `gpu_profiling/results/batch_8gpu_20260318_084307/` - 8 GPU 测试结果

每个目录下包含各数据大小的 nsys report 文件 (`.nsys-rep`)。

## 7. 参考文献

1. NVIDIA A800 Datasheet - https://www.nvidia.com/
2. NCCL Documentation - https://docs.nvidia.com/deeplearning/nccl/
3. NVIDIA Nsight Systems - https://docs.nvidia.com/nsight-systems/
4. Double-Ring All-Reduce Algorithm - Thakur et al., "Optimization of Collective Communication Operations in MPICH"

---

*实验日期: 2026年3月18日*
*测试环境: Docker container zhuyu-prof*
