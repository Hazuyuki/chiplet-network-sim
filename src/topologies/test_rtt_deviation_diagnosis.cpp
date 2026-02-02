/**
 * RTT 偏差诊断：分析 R 增大时仿真与理论偏差增大的原因
 *
 * 假设原因：(1) per-link credit 不均衡 (2) credit 回流离散化 (3) 无 credit 导致阻塞
 *
 * 实验设计：
 * - 对 R=8,12,16,20,24 分别运行单流饱和注入
 * - 采样：credit 分布（min/max/std）、每周期 credit 回报量、因无 credit 阻塞次数
 * - 输出相关性：R 越大是否伴随更严重的不均衡和阻塞
 */

#include "config.h"
#include "nvswitch.h"
#include "packet.h"
#include "traffic_manager.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

Parameters* param = nullptr;
TrafficManager* TM = nullptr;
System* network = nullptr;
boost::mt19937 gen;

static void run_one_cycle(std::vector<Packet*>& vec_pkts, System* system, uint64_t cyc,
                          bool do_credit_return = true) {
  param->current_simulation_cycle = cyc;
  if (do_credit_return) network->process_pending_credits(cyc);

  uint64_t j = 0;
  for (size_t i = 0; i < vec_pkts.size(); ++i) {
    Packet*& pkt = vec_pkts[i];
    if (pkt->releaselink_) {
      pkt->tail_trace().buffer->release_in_link(*pkt);
      if (pkt->leaving_vc_.buffer != nullptr)
        pkt->leaving_vc_.buffer->release_sw_link();
      pkt->releaselink_ = false;
    }
    if (pkt->finished_) {
      delete pkt;
    } else {
      vec_pkts[j++] = pkt;
    }
  }
  vec_pkts.resize(j);
  for (size_t i = 0; i < vec_pkts.size(); i++)
    system->update(*vec_pkts[i]);
}

struct DiagResult {
  int R;
  double throughput;
  double credit_min_mean;   // 各周期 credit 最小值的平均
  double credit_max_mean;
  double credit_std_mean;   // 各周期 36 个 (port,vc) credit 的 std 的平均
  double credit_zero_frac;  // 采样中 credit=0 的 (port,vc) 占比
  uint64_t no_credit_blocked;
  double credit_returns_mean;  // 每周期 credit 回报量的平均
  double credit_returns_std;   // 每周期 credit 回报量的标准差（大则离散）
};

static DiagResult run_diagnosis(int R, NVSwitchSystem* system, int warmup_cycles, int measure_cycles) {
  param->credit_return_delay = R;
  system->reset();
  network->reset_diagnostics();
  network->enable_diagnostics(true);

  TM = new TrafficManager();
  TM->injection_rate_ = 18.0;  // 饱和注入
  std::vector<Packet*> packets;

  Node* gpu0 = network->get_group(0)->get_node(0);  // GPU0
  const int radix = gpu0->radix_;
  const int vc_num = gpu0->vc_num_;
  const int total_vcs = radix * vc_num;

  // Warmup
  for (uint64_t cyc = 0; cyc < (uint64_t)warmup_cycles; cyc++) {
    TM->genMes(packets, cyc);
    run_one_cycle(packets, system, cyc);
  }

  network->reset_diagnostics();

  // Measurement phase: 在主循环内采样 credit（在 process_pending_credits 之后）
  std::vector<double> credit_mins, credit_maxs, credit_stds;
  std::vector<double> credit_returns_per_cycle;
  uint64_t total_zero = 0;
  uint64_t total_samples = 0;
  uint64_t arrived_before = TM->message_arrived_.load();
  const int sample_interval = 5;  // 每 5 周期采样一次

  for (uint64_t cyc = warmup_cycles; cyc < (uint64_t)(warmup_cycles + measure_cycles); cyc++) {
    param->current_simulation_cycle = cyc;
    network->process_pending_credits(cyc);

    // 本周期 credit 回报量
    double ret = static_cast<double>(network->get_diag_credit_returns_last_cycle());
    credit_returns_per_cycle.push_back(ret);

    // 周期性采样 credit 分布（process_pending_credits 之后、consume 之前）
    if ((cyc - warmup_cycles) % sample_interval == 0) {
      std::vector<int> credits;
      for (int p = 0; p < radix; p++) {
        for (int v = 0; v < vc_num; v++) {
          int c = gpu0->get_credit(p, v);
          credits.push_back(c);
          if (c == 0) total_zero++;
          total_samples++;
        }
      }
      int cmin = *std::min_element(credits.begin(), credits.end());
      int cmax = *std::max_element(credits.begin(), credits.end());
      double mean = 0;
      for (int c : credits) mean += c;
      mean /= credits.size();
      double var = 0;
      for (int c : credits) var += (c - mean) * (c - mean);
      double cstd = credits.size() > 1 ? std::sqrt(var / (credits.size() - 1)) : 0;
      credit_mins.push_back(cmin);
      credit_maxs.push_back(cmax);
      credit_stds.push_back(cstd);
    }

    TM->genMes(packets, cyc);
    run_one_cycle(packets, system, cyc, false);  // 不再重复 process_pending_credits
  }

  uint64_t arrived_after = TM->message_arrived_.load();
  double throughput = static_cast<double>(arrived_after - arrived_before) / measure_cycles;
  uint64_t no_credit_blocked = network->get_diag_no_credit_blocked();

  double min_mean = 0, max_mean = 0, std_mean = 0;
  for (double v : credit_mins) min_mean += v;
  for (double v : credit_maxs) max_mean += v;
  for (double v : credit_stds) std_mean += v;
  if (!credit_mins.empty()) {
    min_mean /= credit_mins.size();
    max_mean /= credit_maxs.size();
    std_mean /= credit_stds.size();
  }

  double ret_mean = 0, ret_var = 0;
  for (double r : credit_returns_per_cycle) ret_mean += r;
  if (!credit_returns_per_cycle.empty()) {
    ret_mean /= credit_returns_per_cycle.size();
    for (double r : credit_returns_per_cycle) ret_var += (r - ret_mean) * (r - ret_mean);
    ret_var /= credit_returns_per_cycle.size();
  }
  double ret_std = std::sqrt(ret_var);

  delete TM;
  TM = nullptr;
  network->enable_diagnostics(false);

  DiagResult res;
  res.R = R;
  res.throughput = throughput;
  res.credit_min_mean = min_mean;
  res.credit_max_mean = max_mean;
  res.credit_std_mean = std_mean;
  res.credit_zero_frac = total_samples > 0 ? (100.0 * total_zero / total_samples) : 0;
  res.no_credit_blocked = no_credit_blocked;
  res.credit_returns_mean = ret_mean;
  res.credit_returns_std = ret_std;
  return res;
}

int main(int argc, char* argv[]) {
  std::string test_config = R"(
[Network]
topology = NVSwitch
num_gpus_per_group = 2
num_switches_per_group = 1
num_groups = 1
gpu_nvlink_ports = 18
switches_fully_connected = true
inter_group_sw_connect = false
routing_algorithm = min
gpu_switch_latency = 1
switch_switch_latency = 1
buffer_size = 8
vc_number = 2
router_stages = OneStage
flow_control = credit
credit_return_delay = 0

[Workload]
traffic = single_flow
packet_length = 1
traffic_scale = 1

[Simulation]
simulation_time = 5000
start_injection = 0.05
injection_increment = 0.01
timeout_threshold = 1000
timeout_limit = 100
threads = 1
issue_width = 1000

[Files]
output_file = test_rtt_diag_output.csv
log_file = test_rtt_diag.log
)";

  std::ofstream ofs("output/test_rtt_diagnosis.ini");
  ofs << test_config;
  ofs.close();

  param = new Parameters("output/test_rtt_diagnosis.ini");
  param->flow_control = "credit";

  gen.seed(42);
  NVSwitchSystem* system = new NVSwitchSystem();
  network = system;
  system->init_flow_control();

  const int warmup = 2000;
  const int measure = 1500;
  const std::vector<int> R_values = {8, 12, 16, 20, 24};

  std::cout << "============================================" << std::endl;
  std::cout << "RTT 偏差诊断实验" << std::endl;
  std::cout << "============================================" << std::endl;
  std::cout << "假设: R 增大时 (1) credit 不均衡加剧 (2) 回报离散化 (3) 无 credit 阻塞增加" << std::endl;
  std::cout << "Warmup: " << warmup << " cycles, Measure: " << measure << " cycles" << std::endl;
  std::cout << std::endl;

  std::vector<DiagResult> results;
  for (int R : R_values) {
    std::cout << "Running R=" << R << " ..." << std::endl;
    results.push_back(run_diagnosis(R, system, warmup, measure));
  }

  std::cout << "\n============================================" << std::endl;
  std::cout << "诊断结果" << std::endl;
  std::cout << "============================================" << std::endl;
  std::cout << "|  R  | Thr   | Credit(min) | Credit(max) | Credit(std) | Zero% | NoCreditBlock | Ret/cyc | Ret_std |" << std::endl;
  std::cout << "|-----|-------|-------------|-------------|-------------|-------|---------------|---------|---------|" << std::endl;

  for (const auto& r : results) {
    printf("| %3d | %5.2f | %11.2f | %11.2f | %11.2f | %5.1f | %14lu | %7.1f | %7.1f |\n",
           r.R, r.throughput, r.credit_min_mean, r.credit_max_mean, r.credit_std_mean,
           r.credit_zero_frac, r.no_credit_blocked, r.credit_returns_mean, r.credit_returns_std);
  }

  std::cout << "\n--- 解读 ---" << std::endl;
  std::cout << "• Credit(min) 低、Zero% 高 → per-link 不均衡严重，部分端口常无 credit" << std::endl;
  std::cout << "• NoCreditBlock 大 → 分配时常因无可用 credit 失败" << std::endl;
  std::cout << "• Ret_std 大 → credit 回报离散，非均匀回流" << std::endl;
  std::cout << "若 R 增大时上述指标恶化，则对应假设成立" << std::endl;

  delete system;
  delete param;
  return 0;
}
