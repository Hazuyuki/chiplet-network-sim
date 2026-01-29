# Credit 流控：Credit 耗尽、Buffer 大小、RTT、带宽与 Throughput 的关系

## 一、符号与假设

- **B**：链路带宽（flits/cycle），本仿真中 channel width = 1，即 B = 1。
- **buffer_size**：每个 VC 的接收端 buffer 深度（flits），即配置中的 `buffer_size`；每个 (port, VC) 的初始 credit 数 = buffer_size。
- **vc_number**：每端口 VC 数；同一端口上多条 VC **并行**占用 credit，互不共享。
- **C_eff**：**有效 credit 容量**（flits）。对**单 VC** 分析时 C_eff = buffer_size；对**整条链路（多 VC 合计）**分析时，可同时在途的 flit 数 = 各 VC credit 之和，即 **C_eff = buffer_size × vc_number**（本仿真中 = 8×2 = 16）。
- **R**：Credit 回报延迟（cycles），即配置中的 `credit_return_delay`（RTT 的建模）。
- **T_other**：从“发送端发出 flit”到“该 flit 对应 credit 被触发回报”之间的**其他延迟**（不含 R）：
  - 含：链路传播延迟、包在接收端 buffer 内停留时间（排队 + 被转发）等；
  - **T_other 与场景相关**：多流 (uniform) 时取 T_other≈4 可与“约从 R=12 开始掉”对齐；**单发单收**时有效 T_other 更大（约 8–9），见下文“单发单收与 T_other”小节。

以下**按整条链路（多 VC 合计）**写公式时用 C_eff；若按单 VC 写则 C = buffer_size。

---

## 二、Credit 与“在途数据量”

- 发送端每发出 1 flit，扣 1 credit（对应某 VC）；接收端在**包尾离开本 buffer** 后，经过 **R** 周期把相应 credit 还回。
- 因此，在任意时刻，“已发出但尚未归还”的 credit 数 = 正在占用该链路/接收 buffer 的 flit 数。
- 有 **vc_number** 个 VC 时，同一端口上**各 VC 的 credit 独立**，故整条链路可同时在途的 flit 数 ≤ **C_eff = buffer_size × vc_number**。
- 稳态下，发送速率 r (flits/cycle) 要能维持，必须满足：  
  **在途占用的 credit 数 ≤ C_eff**。

---

## 三、Credit 占用时间（单 flit 视角）

从“发送端因发该 flit 扣掉 1 credit”到“该 credit 被归还”的时间长度为：

- **T = T_other + R**

即：包到达并离开接收 buffer 的时间（T_other）+ 回报延迟（R）。

在稳态、发送速率 r 下，平均“在途”credit 数 ≈ **r × T**（Little 定律）。  
因此有：

- **r × T ≤ C_eff**  
即  
- **r × (T_other + R) ≤ C_eff**

于是**该链路上可维持的最大吞吐**为：

- **r_max = C_eff / (T_other + R)**，且不超过 B，即 **r_max = min(B, C_eff/(T_other+R))**

---

## 四、RTT 何时开始影响 Throughput？

1. **理想上界**：链路带宽为 B，理论最大吞吐为 B。  
   若 **r_max ≥ B**，即 **C_eff / (T_other + R) ≥ B**，则 credit 足够，throughput 可达 B。  
   解出：  
   **R ≤ R_crit = C_eff/B − T_other**。  
   即当 **R ≤ R_crit** 时，throughput 可达满带宽 B；**R > R_crit** 时，r_max = C_eff/(T_other+R) < B，throughput 被 RTT 限制。

2. **临界 RTT**：  
   **R_crit = C_eff/B − T_other**  
   - 当 **R ≤ R_crit**：throughput ≈ B，RTT 对 throughput 影响小；  
   - 当 **R > R_crit**：throughput 随 R 增大而下降，**r_max = C_eff/(T_other+R)**。

3. **为何仿真里“约从 12 才开始掉”？—— 有效容量要用 C_eff，且 T_other 针对多流**  
   - 若错误地用 **C = buffer_size = 8**（单 VC），且 T_other = 2，则 **R_crit = 8−2 = 6**，理论会预测 R>6 就掉，与图中“约从 12 才开始掉”不符。  
   - 本仿真中 **vc_number = 2**，同一端口两条 VC **并行**，可同时在途的 flit 数 = 2×8 = **C_eff = 16**。  
   - 取 **T_other ≈ 4**（链路 + 接收端平均停留，针对**多流 uniform**），则 **R_crit = 16 − 4 = 12**。  
   - 即 **R > 12** 时 throughput 才被 credit 明显限制，与**多流**仿真曲线一致。  
   **结论**：分析模型里“容量”应取 **C_eff = buffer_size × vc_number**；**T_other=4 适用于多流**，单发单收时有效 T_other 更大（见下文）。

---

## 五、三者关系小结

| 量       | 关系说明 |
|----------|----------|
| **Credit 耗尽** | 在途量 = r×(T_other+R)；当 ≥ C_eff 时发送端无 credit，throughput 受限于 C_eff/(T_other+R)。 |
| **Buffer 大小 × VC 数 (C_eff)** | C_eff 越大，可同时在途的 flit 越多，R_crit = C_eff/B − T_other 越大，越能容忍大 RTT 仍满带宽。 |
| **RTT (R)** | R 越大，单 credit 占用时间越长，r_max = C_eff/(T_other+R) 越小，throughput 越低。 |
| **带宽 B** | B 越大，满带宽所需在途量越大，R_crit = C_eff/B − T_other 越小，越容易在较小 R 下就受 RTT 限制。 |

**RTT 开始明显影响 throughput 的条件**：  
**R > R_crit = C_eff/B − T_other**  

- 其中 **C_eff = buffer_size × vc_number**（多 VC 时同一端口容量相加）。  
- RTT 超过该临界值时，throughput 从满带宽 B 掉到 **C_eff/(T_other+R)** 并随 R 增大继续下降。

---

## 六、设计含义

- 若要**大 RTT 下仍保持高 throughput**：应增大 **C_eff**（buffer 深度 × VC 数），使 **C_eff ≥ B×(T_other+R)**（BDP 思想）。
- 若 **C_eff 固定**：R 或 T_other 增大会导致 r_max 下降。

本仿真中 **buffer_size=8, vc_number=2 ⇒ C_eff=16, B=1**。**多流**时取 **T_other≈4**，故 **R_crit = 12**；delay ≤ 12 时 throughput 接近满带宽，与多流仿真一致。**单发单收**时有效 T_other 更大，R_crit 更小，见下节。

---

## 七、吞吐估计公式（与图上对比用）

**公式**（单链路饱和吞吐估计）：

$$\text{Throughput}(R) = \min\left(B,\; \frac{C_{eff}}{T_{other}+R}\right)$$

- **R**：credit 回报延迟（cycles）  
- **C_eff**：buffer_size × vc_number  
- **T_other**：链路 + 接收端停留（cycles）  
- **B**：链路带宽（flits/cycle）

图中蓝线为该公式曲线，红点为仿真在各 delay 下能达到的**最大接收率**（饱和区）。公式给出的是单链路 credit 限制下的上界；仿真为多路径网络，实际最大吞吐可**明显**高于单链路公式值（见下表与下节）。

| R (delay) | 公式 min(B, C_eff/(T_other+R)) | 仿真 max throughput |
|-----------|--------------------------------|---------------------|
| 16        | 0.80                            | ~0.94               |
| 20        | 0.67                            | ~0.87               |
| 24        | 0.57                            | ~0.79               |

---

## 八、为何仿真吞吐高于公式估计？

公式 **Throughput(R) = min(B, C_eff/(T_other+R))** 描述的是**单条链路**在“该链路 100% 被 credit 限制”时的饱和吞吐。仿真给出的是**全网、多跳、多链路**下的**每节点平均接收率**。二者尺度不同，仿真更高主要有以下原因。

### 1. 单链路 vs 全网

- **公式**：针对**一条链路**。假设这条链路上 flit 流连续、credit 是唯一瓶颈，则该链路吞吐 ≤ C_eff/(T_other+R)。
- **仿真**：NVSwitch 拓扑下有多条链路、多跳；uniform 流量分散到不同路径。**没有一条链路**一定被 100% 占满；瓶颈可能先在别处（如注入率、其他端口）出现，所以**平均到每条链路的利用率**往往低于 100%。
- 因此，**全网平均每节点吞吐**可以高于“单链路公式给出的那条链路的吞吐”。公式相当于“最坏的那条链路”的上界，而仿真测的是整网平均，自然容易更高。

### 2. 并非所有流量都经过“高 R 链路”

- 配置的 **credit_return_delay = R** 作用在**每一跳**的 credit 回报上。
- 一条流可能只经过少数几跳；不同流经过的跳数、经过的端口不同。对**单条流**而言，端到端吞吐受限于**路径上各跳中最紧的那一跳**；但对**全网**而言，我们统计的是所有流量的平均接收率。
- 若部分流量路径较短、或未压到 credit 极限，它们会拉高平均吞吐，使**仿真曲线高于单链路公式**。

### 3. T_other 的取值偏保守

- 公式里取 **T_other = 4** 是为了让 **R_crit ≈ 12** 与仿真“约从 12 开始掉”对齐。
- 实际仿真里，链路延迟、交换延迟、buffer 停留等加在一起，**有效 T_other** 可能略小；即有效 **(T_other + R)** 略小，对应的 **C_eff/(T_other+R)** 会略大。
- 因此公式本身会略低估“单链路”可达吞吐，仿真在单链路意义上也可能略高于当前公式曲线。

### 4. 公式应理解为“单链路下界/估计”而非“全网吞吐”

- **结论**：  
  - 公式适合作为**单条链路**在 credit 受限时的吞吐估计（或保守下界）。  
  - 仿真测的是**多链路、多跳网络**的每节点平均接收率，会高于单链路公式，尤其在 R 较大时（如 R=16,20,24）差异明显。  
- **若要做“公式 vs 仿真”的严格对照**：应做**单发单收**实验（一条流 node0→node1）；此时需用**单流场景下的有效 T_other**（见下节），公式才能与仿真一致。

---

## 九、单发单收与 T_other：为何 R=12 就掉很多？

单发单收实验里，**R=12 时吞吐已降到约 0.75**，而用 **T_other=4** 的理论给出 R_crit=12、在 R=12 时仍为满带宽 1.0，与单流仿真不符。问题出在**理论中的常数 T_other**：它和场景有关，不是普适常数。

### 单流下有效 T_other 更大

- **T_other=4** 是对**多流 (uniform)** 拟合的：多流时“约从 R=12 开始掉”，故取 T_other=4 得 R_crit=12。
- **单发单收**只有一条流、路径 2 跳（GPU0→Switch→GPU1）。在饱和时，瓶颈链路上 flit 在 buffer 内**排队**更明显，从“发送端发出”到“credit 被触发回报”的**平均时间**更长，即**有效 T_other 更大**（约 **8–9**）。
- 用单流数据反推：R=12 时 thr≈0.75，由 **C_eff/(T_other+R)=0.75** 得 **T_other+12=16/0.75≈21.3**，故 **T_other≈9.3**。取 **T_other_fit≈9** 时：
  - **R_crit_single = C_eff/B − 9 = 16−9 = 7**，即单流下 R>7 就开始被 credit 限制；
  - R=12 时 **thr = 16/(9+12)≈0.76**，与仿真 ~0.75 一致；
  - R=16,20,24 时公式与单流仿真也接近。

### 结论

- **理论公式本身没错**，错在把 **T_other 当成普适常数**。  
- **T_other 与场景相关**：多流时 T_other≈4（R_crit≈12）；单发单收时有效 T_other≈8–9（R_crit≈7），故单流在 R=12 时已明显下降。  
- 与单流仿真对比时，应用**单流拟合的 T_other（如 9）** 画公式曲线，则理论与单流数据一致。

---

## 十、单发单收实验（公式对照）

已支持 **single_flow** 流量模式：固定从 node0 发到 node1，配合 **traffic_scale=1** 使注入率表示该单流速率。

**运行单发单收实验：**

```bash
./run_experiment_single_flow.sh
```

- 配置：`input/nvswitch_single_flow_exp.ini`（2 GPU、1 switch、single_flow、traffic_scale=1）。
- 输出：`output/single_flow_delay_N.csv`（N = 0,2,4,6,8,12,16,20,24）。
- 绘图（需 matplotlib）：`python3 plot_single_flow_vs_formula.py` → `single_flow_vs_formula.png`。

**典型结果**（公式用 **T_other=4** 时与单流仿真不一致；改用 **T_other=9** 后一致）：

| R (delay) | 公式 (T_other=4) | 公式 (T_other=9) | 单发单收仿真 |
|-----------|------------------|------------------|--------------|
| 0–8       | 1.0              | 1.0              | ~1.0         |
| 12        | 1.0              | **0.76**         | ~0.75        |
| 16        | 0.80             | **0.64**         | ~0.66        |
| 20        | 0.67             | **0.55**         | ~0.58        |
| 24        | 0.57             | **0.48**         | ~0.53        |

单流下有效 T_other≈9，故用 T_other=9 的公式与单流仿真一致；T_other=4 只适用于多流场景。
