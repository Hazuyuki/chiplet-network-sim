# 多流跨 node 实验说明

## 目的

验证 **spine 无阻塞** 设计：当所有机器同时向其他节点发流时，Spine 层能否吃满、不因拥塞导致提前饱和或大量丢包/超时。

## 设计

- **流量模式**：`inter_group_uniform`  
  - 每个包的 src 和 dest 一定在**不同 group**（不同 node），所有流量都经过 spine。
- **拓扑**：与 allreduce 一致，使用 `spine_non_blocking = true`（40 个 spine，leaf radix 83）。
- **指标**：
  - 随注入率增加，**接收率**是否能跟上注入率直到较高负载；
  - **Timeout** 是否在高注入率下才明显增加；
  - 若在较低注入率就饱和或大量 timeout，说明 spine 或链路仍是瓶颈（未达无阻塞）。

## 如何运行

```bash
./run_cross_node_exp.sh
```

或直接：

```bash
./build/ChipletNetworkSim input/nvswitch_cross_node.ini
```

结果在 `output/nvswitch_cross_node.csv` 和 `output/cross_node_stdout.txt`。

## 配置要点（`input/nvswitch_cross_node.ini`）

| 项 | 值 | 说明 |
|----|-----|------|
| traffic | inter_group_uniform | 仅跨 group 流量 |
| spine_non_blocking | true | 无阻塞 spine（40 spine） |
| num_groups | 8 | 8 台机器 |
| start_injection / max_injection | 0.1 ~ 8 | 扫描注入率 |

## 如何看是否“无阻塞”

- **无阻塞**：在较高注入率下，接收率仍能接近注入率，且 timeout 很少；饱和点主要由端到端 credit/buffer 等限制决定，而不是 spine 容量。
- **有阻塞**：在较低注入率下就出现接收率明显低于注入率、或大量 timeout，说明 spine 或某层链路成为瓶颈。

可对比：
- 使用 `spine_non_blocking = true` 的跨 node 实验；
- 若关闭无阻塞（例如改为 `num_spine_switches = 2`、`inter_group_sw_connect = true` 等），在相同注入率下饱和是否明显更早、timeout 是否更多。
