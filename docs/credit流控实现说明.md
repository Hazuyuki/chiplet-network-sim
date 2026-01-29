# 基于 Credit 的流控实现说明

本文说明为支持 **credit-based 流控** 在代码中做了哪些修改，以及具体实现方式。

---

## 一、设计思路对比

| 项目       | 原有（buffer-based）           | Credit-based（新增）                    |
|------------|--------------------------------|----------------------------------------|
| 谁存“可发多少” | 接收端 buffer 的 `vc_buffer_[vcb]` | **发送端** Node 的 `credits_[port][vcb]` |
| 发送条件   | 下游 `allocate_buffer()` 成功   | 发送端 `has_credit(port, vcb, n)` 为真 |
| 真正发送时 | 下游 buffer 减 `vc_buffer_`     | **发送端** `consume_credit(port, vcb, n)` |
| 释放时     | 下游 `release_buffer()` 加回    | **下游** 调用 **上游** `return_credit()` |

即：流控状态从“接收端维护剩余空间”改为“发送端维护 credit，接收端消费后回报”。

---

## 二、修改的文件与内容

### 1. 配置：`config.h` / `config.cpp`

**目的**：增加流控模式开关，与现有 buffer 流控并存。

- **config.h**
  - 在 `Parameters` 中增加成员：
    - `std::string flow_control;`  
    - 取值：`"buffer"`（默认，原有行为）或 `"credit"`。
  - 在 `print_params()` 中增加对 `flow_control` 的打印。

- **config.cpp**
  - 从 ini 读取：
    - `flow_control = params_ptree.get<std::string>("Network.flow_control", "buffer");`
  - 默认 `"buffer"`，配置里写 `flow_control = credit` 即启用 credit 流控。

**使用**：在 `[Network]` 中增加一行 `flow_control = credit` 即可切到 credit 模式。

---

### 2. 发送端（Node）：`node.h` / `node.cpp`

**目的**：在**发送端**为每条“出边 (port) × VC”维护 credit 计数，并支持检查、扣减、回报、查询。

- **node.h**
  - 增加成员：
    - `int vc_num_;`（构造时保存，用于 credit 数组大小）
    - `std::atomic_int* credits_{nullptr};`  
    - 下标：`port * vc_num_ + vcb`，表示“从本节点经 port 发往该 link 的 VC vcb 上还能发多少 flit”。
  - 增加接口：
    - `void init_credits();`  
      - 分配 `radix_ * vc_num_` 个原子 int，每个初始化为下游 buffer 深度（即 `buffer_size`）。
    - `bool has_credit(int port, int vcb, int n) const;`  
      - 当前 (port, vcb) 的 credit 是否 ≥ n。
    - `void consume_credit(int port, int vcb, int n);`  
      - 原子减 n（真正发出包时调用）。
    - `void return_credit(int port, int vcb, int n);`  
      - 原子加 n（下游释放/消费后回报）。
    - `int get_credit(int port, int vcb) const;`  
      - 读取当前 credit，用于测试/调试。
    - `int get_port_to_buffer(Buffer* buf) const;`  
      - 根据“下游 buffer 指针”找到对应的出端口 port，供 VC 分配时用。

- **node.cpp**
  - 构造：`vc_num_ = vc_num`；析构：`delete[] credits_`。
  - `init_credits()`：  
    - 若已有则先 `delete[] credits_`；  
    - 分配 `radix_ * vc_num_`，每个 `store(buffer_size)`（buffer_size 来自 `in_buffers_[0]->buffer_size_`）。
  - `has_credit`：`credits_[port * vc_num_ + vcb].load() >= n`。
  - `consume_credit`：对 `credits_[port * vc_num_ + vcb]` 做 CAS 减 n，保证非负。
  - `return_credit`：对同一下标做 CAS 加 n。
  - `get_credit`：返回该下标的 load；非法 (port/vcb) 或 `credits_==nullptr` 时返回 -1。
  - `get_port_to_buffer`：遍历 `link_buffers_[i]`，找到等于 `buf` 的 port i。
  - `reset()`：若 `credits_ != nullptr` 则调用 `init_credits()`，保证仿真重置后 credit 回到初始值。

**语义**：每个 (port, vcb) 的 credit 初始 = 下游该 VC 的 buffer 深度；发包时减，下游释放时加，从而在发送端维护“还能发多少”的状态。

---

### 3. 接收端（Buffer）：`buffer.h` / `buffer.cpp`

**目的**：让每个输入 buffer 知道“谁在给我发”（上游 Node + 出端口），以便在**释放时向发送端回报 credit**；并在**真正接收时**触发发送端扣 credit。

- **buffer.h**
  - 增加成员：
    - `Node* upstream_node_{nullptr};`
    - `int upstream_port_{-1};`  
    - 表示“本 buffer 的上游节点”和“该节点上指向本 buffer 的出端口”。
  - 增加接口：
    - `void set_upstream(Node* node, int port);`  
      - 拓扑建好后由系统统一设置。

- **buffer.cpp**
  - `set_upstream`：`upstream_node_ = node; upstream_port_ = port;`
  - **release_buffer(vcb, n)**（包尾离开本 buffer 时调用）：
    - 若 `param->flow_control == "credit"` 且 `upstream_node_ != nullptr` 且 `upstream_port_ >= 0`：  
      - 调用 `upstream_node_->return_credit(upstream_port_, vcb, n);`  
      - **不再**对 `vc_buffer_[vcb]` 做加回（credit 模式下接收端不维护“剩余空间”做流控）。
    - 否则：保持原逻辑，对 `vc_buffer_[vcb]` 做 CAS 加 n。
  - **allocate_in_link(Packet& p)**（包被推入本 buffer、占用入链路时）：
    - 若 `param->flow_control == "credit"` 且 upstream 有效：  
      - 在 `push_pkt` 之前调用  
        `upstream_node_->consume_credit(upstream_port_, vcb, p.length_);`  
      - 表示“发送端已经真正把包发过来了”，在发送端扣掉对应 credit。
    - 然后照常 `push_pkt`。

**语义**：  
- 发送端“扣 credit”发生在**接收端 allocate_in_link**（即包进入下游 buffer）时。  
- 接收端“回报 credit”发生在**本 buffer release_buffer**（包尾离开本 buffer）时。  
- 这样，发送端 credit 与“在途 + 在下游 buffer 中的 flit 数”守恒，初始等于下游 buffer 深度。

---

### 4. 系统与 VC 分配：`system.h` / `system.cpp`

**目的**：拓扑建好后为每个 buffer 设置 upstream，并在 credit 模式下为每个节点初始化 credit；VC 分配阶段改为“看发送端 credit”而不是“占下游 buffer”。

- **system.h**
  - 增加：
    - `void init_flow_control();`  
      - 在 `read_config` / 拓扑构建完成后调用，用于设置 upstream 和（若为 credit）初始化各节点 credit。

- **system.cpp**
  - **init_flow_control()**（新增）：
    - 遍历所有 group、所有 node、所有 port：
      - 若 `node->link_buffers_[port] != nullptr`，则  
        `node->link_buffers_[port]->set_upstream(node, port);`  
        （即：该 port 连到的下游 buffer 的上游就是本 node、本 port）。
      - 若 `param->flow_control == "credit"`，则对该 node 调用 `node->init_credits();`。
  - **vc_allocate(Packet& p)**（修改）：
    - 先得到**当前发送端**：  
      `Node* sender = get_node(current_vc.buffer == nullptr ? p.source_ : current_vc.id);`
    - 若 `param->flow_control == "credit"`：
      - **不再**对下游做 `allocate_buffer()`。
      - 对每个候选 VC，用 `sender->get_port_to_buffer(vc.buffer)` 得到 port，再 `sender->has_credit(port, vc.vcb, p.length_)`；  
        优先选“空 VC 且有足够 credit”的通道，否则选“有足够 credit”的通道；  
        找到则设 `p.next_vc_` 并 return，否则不分配。
    - 否则：保持原逻辑，对下游 buffer 做 `allocate_buffer()` 等。

**语义**：  
- Credit 模式下，VC 分配只检查“发送端有没有足够 credit”，不提前占用下游 buffer 计数。  
- 真正占用发生在 `allocate_in_link` 时（发送端扣 credit）；释放发生在 `release_buffer` 时（上游 return_credit）。

---

### 5. 主流程：`main.cpp`

**目的**：在拓扑和配置就绪后，统一做一次流控初始化（设置 upstream + 若为 credit 则初始化各节点 credit）。

- 在 `network = System::New(param->topology)` 之后、`TM = new TrafficManager()` 之前增加一行：  
  **`network->init_flow_control();`**

这样无论用哪种拓扑，只要配置了 `flow_control = credit`，都会正确设置 upstream 并初始化 credit。

---

## 三、数据流小结（credit 模式）

1. **初始化**  
   - `init_flow_control()`：  
     - 为每个 `link_buffers_[port]` 设置 `set_upstream(node, port)`；  
     - 对每个 node 调用 `init_credits()`，使每个 (port, vcb) 的 credit = buffer_size。

2. **VC 分配**  
   - `vc_allocate`：  
     - 只检查 `sender->has_credit(port, vcb, p.length_)`，不调用下游 `allocate_buffer()`。

3. **真正发送（包进入下游 buffer）**  
   - 下游 `allocate_in_link(p)` 成功时：  
     - 调用 `upstream_node_->consume_credit(upstream_port_, vcb, p.length_)`；  
     - 再 `push_pkt`。  
   - 即：**发送端在“包进入下游”时扣 credit**。

4. **释放（包尾离开某 buffer）**  
   - 该 buffer 的 `release_buffer(vcb, n)` 被调用时：  
     - 若为 credit 模式且 upstream 有效：  
       - `upstream_node_->return_credit(upstream_port_, vcb, n)`；  
     - 不再改 `vc_buffer_`。

5. **重置**  
   - Node::reset() 里若已有 `credits_`，会再次 `init_credits()`，保证多次运行/重置后 credit 状态正确。

---

## 四、与原有 buffer 流控的兼容

- 配置为 `flow_control = buffer`（或未配，默认）时：  
  - 不调用 `init_credits()`，Node 的 `credits_` 保持为 nullptr；  
  - `vc_allocate` 走原有“下游 allocate_buffer”逻辑；  
  - `release_buffer` 只做 `vc_buffer_` 加回，不调用 `return_credit`；  
  - `allocate_in_link` 不调用 `consume_credit`。  
- 因此原有 buffer 流控行为不变，仅新增 credit 路径和配置项。

以上即为支持 credit 流控所修改的代码与实现方式。

---

## 五、修改 Credit 回报 RTT（延迟）

**含义**：真实硬件里 credit 从接收端回到发送端有链路延迟。本仿真用 **credit 回报延迟（cycles）** 模拟这段 RTT。

**配置**（`config.h` / `config.cpp`）：

- `int credit_return_delay`：credit 回报延迟（周期数），默认 0 = 立即回报（与之前行为一致）。
- ini 中在 `[Network]` 下增加：`credit_return_delay = 2`（示例：2 周期后发送端才收到 credit）。

**实现要点**：

1. **当前周期**：main 在每周期初把 `param->current_simulation_cycle` 设为该周期号，并在 `run_one_cycle(..., cycle)` 里先调用 `network->process_pending_credits(cycle)`，再执行释放/更新。
2. **延迟回报**：当包尾离开某 buffer 时，若 `credit_return_delay > 0`，不立刻 `return_credit()`，而是把一次回报事件入队：`delivery_cycle = current_simulation_cycle + credit_return_delay`，`network->push_pending_credit_return(delivery_cycle, node, port, vcb, n)`。
3. **到期处理**：每周期初 `process_pending_credits(current_cycle)` 会取出所有 `delivery_cycle <= current_cycle` 的事件，对对应 (node, port, vcb) 调用 `return_credit(..., n)`。
4. **线程安全**：pending 队列用 `pending_credit_mutex_` 保护，多线程下 `release_buffer` 入队、主线程 `process_pending_credits` 出队并执行回报。
5. **重置**：`System::reset()` 会清空 `pending_credit_returns_`，避免跨 run 的残留事件。

**使用**：在 `[Network]` 中设置 `credit_return_delay = N`（N 为周期数）即可模拟 N 周期的 credit 回报 RTT。
