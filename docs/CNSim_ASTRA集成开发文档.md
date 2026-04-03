# CNSim-ASTRA-sim 集成开发文档

## 一、概述

本文档记录了 CNSim（Chiplet Network Simulator）与 ASTRA-sim 网络后端集成的开发过程，包括代码修改、测试验证和实现细节。

## 二、已完成的修改

### 2.1 ASTRA-sim 项目修改

#### 2.1.1 新增文件

| 文件路径 | 说明 |
|---------|------|
| `astra-sim/network_frontend/cnsim/CNSimNetworkApi.hh` | CNSim 网络后端头文件 |
| `astra-sim/network_frontend/cnsim/CNSimNetworkApi.cc` | CNSim 网络后端实现 |
| `astra-sim/network_frontend/cnsim/test_cnsim_integration.cc` | 集成测试代码 |
| `astra-sim/network_frontend/cnsim/README_CNSIM_BACKEND.md` | 后端集成文档 |

#### 2.1.2 修改的文件

| 文件 | 修改内容 |
|------|---------|
| `astra-sim/common/AstraNetworkAPI.hh` | 添加 `CNSim` 后端类型枚举值 |

#### 2.1.3 CNSimNetworkApi 类接口

实现了 `AstraNetworkAPI` 接口的完整方法：

```cpp
class CNSimNetworkApi : public AstraNetworkAPI {
public:
    // 构造函数
    explicit CNSimNetworkApi(int rank, const std::string& config_file,
                           CNSimBackendType backend_type, int num_nodes);
    
    // ASTRA-sim 接口实现
    int sim_send(...) override;           // 发送数据
    int sim_recv(...) override;          // 接收数据
    void sim_schedule(...) override;      // 事件调度
    timespec_t sim_get_time() override;   // 获取仿真时间
    double get_BW_at_dimension(int dim) override;  // 获取带宽
    void sim_notify_finished() override;  // 通知完成
    BackendType get_backend_type() override;  // 获取后端类型
};
```

#### 2.1.4 时间同步机制

使用静态成员实现全局时间同步：

```cpp
static double ns_per_cycle_;   // 每周期纳秒数 (默认 1.0)
static uint64_t global_cycle_; // 全局仿真周期
```

- **全局时间**：所有 `CNSimNetworkApi` 实例共享同一个 `global_cycle_`
- **时间推进**：`run_one_cycle()` 递增全局时间
- **时间转换**：`sim_get_time()` 返回 `global_cycle_ × ns_per_cycle_`

### 2.2 独立测试代码

为验证时间同步和 API 功能，创建了独立测试代码：

| 文件 | 说明 |
|------|------|
| `chiplet-network-sim/test_time_sync.cc` | 时间同步机制验证 |
| `chiplet-network-sim/test_api.cc` | 完整 API 功能验证 |

## 三、测试验证

### 3.1 时间同步测试

| 测试用例 | 验证内容 | 结果 |
|---------|---------|------|
| 全局时间同步 | 多实例共享同一全局时间 | ✅ PASSED |
| 时间推进 | 时间正确递增 | ✅ PASSED |
| 事件调度 | 基于全局时间调度回调 | ✅ PASSED |
| NS/周期转换 | 可配置时间单位 | ✅ PASSED |
| 端到端通信 | 时间同步下的数据包传输 | ✅ PASSED |

### 3.2 API 功能测试

| 测试用例 | 验证内容 | 结果 |
|---------|---------|------|
| `sim_send` | 发送数据到目标节点 | ✅ PASSED |
| `sim_recv` | 从源节点接收数据 | ✅ PASSED |
| `sim_schedule` | 延迟调度事件 | ✅ PASSED |
| `sim_get_time` | 获取仿真时间 | ✅ PASSED |
| `get_BW_at_dimension` | 获取带宽信息 | ✅ PASSED |
| `sim_notify_finished` | 通知完成状态 | ✅ PASSED |
| `get_backend_type` | 获取后端类型 | ✅ PASSED |
| 完整 Send/Receive | 端到端通信 | ✅ PASSED |
| 多节点时间同步 | 全局时间一致性 | ✅ PASSED |

## 四、使用方法

### 4.1 编译

```bash
# 方式一：使用独立测试代码
cd chiplet-network-sim
g++ -std=c++17 -o test_api test_api.cc -pthread
./test_api

# 方式二：集成到 ASTRA-sim（需要 CMake 配置）
cd astra-sim
mkdir -p build && cd build
cmake .. -DASTRA_SIM_USE_CNSIM=ON
make -j$(nproc)
```

### 4.2 运行测试

```bash
# 独立测试
./test_api

# 预期输出：
# ALL API TESTS PASSED ✓
```

### 4.3 集成到 ASTRA-sim

```cpp
#include "CNSimNetworkApi.hh"

// 创建 CNSim 网络后端
AstraSim::CNSimNetworkApi* network = new AstraSim::CNSimNetworkApi(
    rank,                    // 节点 ID
    "config.ini",           // CNSim 配置文件
    CNSimNetworkApi::CNSimBackendType::NVSwitch,  // 拓扑类型
    8                       // 节点数量
);

// 初始化
network->init_cnsim();

// 运行仿真循环
network->run_cycles(1000);

// 获取统计信息
uint64_t sent = network->get_packets_sent();
double latency = network->get_average_latency();
```

## 五、支持的拓扑类型

| 拓扑类型 | 说明 |
|---------|------|
| NVSwitch | NVIDIA NVSwitch 拓扑 |
| SingleChipMesh | 单芯片 Mesh |
| MultipleChipMesh | 多芯片 Mesh |
| FatTree | 胖树 |
| DragonflySW | Dragonfly 交换机 |
| DragonflyChiplet | Dragonfly Chiplet |
| RailX | Rail-X 拓扑 |

## 六、配置参数

可通过 INI 文件配置 CNSim：

```ini
[Network]
topology = NVSwitch
num_gpus_per_group = 8
num_switches_per_group = 4
num_groups = 1
gpu_nvlink_ports = 18

[Router]
buffer_size = 128
switch_buffer_size = 2048
vc_number = 2
flow_control = credit
credit_return_delay = 2

[Simulation]
simulation_time = 2000
```

## 七、后续工作

### 已完成
- ✅ 最小可行版本（MVP）实现
- ✅ 时间同步机制
- ✅ 核心 API 接口
- ✅ 独立测试验证

### 待完善
- [ ] 与真实 CNSim 引擎的深度集成
- [ ] 拓扑映射（ASTRA-sim YAML → CNSim 拓扑）
- [ ] 带宽/延迟参数传递
- [ ] 集合通信支持
- [ ] 性能优化

## 八、文件清单

### ASTRA-sim 项目
```
astra-sim/
├── astra-sim/
│   ├── common/
│   │   └── AstraNetworkAPI.hh          # 已修改：添加 CNSim 类型
│   └── network_frontend/
│       └── cnsim/
│           ├── CNSimNetworkApi.hh      # 新增
│           ├── CNSimNetworkApi.cc      # 新增
│           ├── test_cnsim_integration.cc  # 新增
│           └── README_CNSIM_BACKEND.md # 新增
```

### 独立测试代码
```
chiplet-network-sim/
├── test_time_sync.cc    # 时间同步测试
└── test_api.cc         # API 功能测试
```

---

**文档版本**: 1.0  
**更新日期**: 2026-03-11
