/**
 * All-Reduce 饱和点诊断：分析为何 ring_all_reduce 饱和 ~4.2 而单流可达 18
 *
 * 对比 single-flow 与 ring_all_reduce 在相同拓扑下的诊断指标，
 * 定位瓶颈：credit 饥饿 / 端口争用 / 接收端反压 等
 */

#include "config.h"
#include "nvswitch.h"
#include "packet.h"
#include "traffic_manager.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
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
  std::string scenario;
  double injection_rate;
  double throughput;
  double inj_mean;           // 每周期实际注入 flit 数（全网）
  double inj_std;
  double inj_under_frac;     // 注入 < offered 的周期占比
  double credit_zero_frac;   // credit=0 的 (port,vc) 占比（采样节点平均）
  uint64_t no_credit_blocked;
  double idle_port_frac_mean;
  double idle_port_frac_std;
  double unused_avail_mean;
  // 在途包诊断：定位网络内部拥塞
  double in_flight_mean;      // 全网在途包数均值
  double in_flight_max;       // 采样周期内在途包数最大值
  double in_flight_per_node_max;   // 单节点最大在途包数（采样均值）
  double in_flight_at_switch_frac; // 在途包中位于 switch 节点的占比
};

static DiagResult run_allreduce_diagnosis(const std::string& traffic, double inj_rate,
                                          NVSwitchSystem* system, int warmup, int measure) {
  param->traffic = traffic;
  param->traffic_scale = (traffic == "single_flow") ? 1 : network->num_cores_;
  system->reset();
  network->reset_diagnostics();
  network->enable_diagnostics(true);

  TM = new TrafficManager();
  TM->traffic_ = traffic;
  TM->traffic_scale_ = param->traffic_scale;
  TM->injection_rate_ = inj_rate;

  std::vector<Packet*> packets;

  // 采样节点：单流只有 GPU0；all-reduce 采样多节点
  std::vector<Node*> sample_nodes;
  if (traffic == "single_flow") {
    sample_nodes.push_back(network->get_group(0)->get_node(0));
  } else {
    for (int core : {0, 16, 32, 48}) {
      int g = core / param->params_ptree.get<int>("Network.num_gpus_per_group", 8);
      int n = core % param->params_ptree.get<int>("Network.num_gpus_per_group", 8);
      sample_nodes.push_back(network->get_group(g)->get_node(n));
    }
  }

  const int radix = sample_nodes[0]->radix_;
  const int vc_num = sample_nodes[0]->vc_num_;
  const double offered_load = (traffic == "single_flow")
                                  ? inj_rate
                                  : (inj_rate * network->num_cores_);

  for (uint64_t cyc = 0; cyc < (uint64_t)warmup; cyc++) {
    TM->genMes(packets, cyc);
    run_one_cycle(packets, system, cyc);
  }
  network->reset_diagnostics();

  std::vector<double> injected_per_cycle;
  std::vector<double> idle_port_frac_samples;
  std::vector<double> unused_avail_samples;
  std::vector<double> in_flight_samples;
  std::vector<double> in_flight_per_node_max_samples;
  std::vector<double> in_flight_at_switch_frac_samples;
  uint64_t total_zero = 0, total_samples = 0;
  uint64_t arrived_before = TM->message_arrived_.load();
  const int sample_interval = 5;

  // 构建 GPU 节点集合（用于区分在途包位于 GPU vs Switch）
  std::unordered_set<Node*> gpu_nodes;
  for (int g = 0; g < system->num_groups_; g++) {
    NVSwitchGroup* grp = system->get_group(g);
    for (int i = 0; i < grp->num_gpus_; i++)
      gpu_nodes.insert(grp->get_gpu(i));
  }

  for (uint64_t cyc = warmup; cyc < (uint64_t)(warmup + measure); cyc++) {
    param->current_simulation_cycle = cyc;
    network->process_pending_credits(cyc);

    bool do_sample = ((cyc - warmup) % sample_interval == 0);
    double idle_sum = 0, unused_sum = 0;
    int nodes_sampled = 0;

    std::vector<int> avail_per_node;
    if (do_sample) {
      for (Node* node : sample_nodes) {
        int idle_ports = 0, avail_ports = 0;
        for (int p = 0; p < radix; p++) {
          bool port_has_credit = false;
          for (int v = 0; v < vc_num; v++) {
            int c = node->get_credit(p, v);
            if (c == 0) total_zero++;
            total_samples++;
            if (c >= 1) port_has_credit = true;
          }
          if (!port_has_credit) idle_ports++;
          else avail_ports++;
        }
        idle_sum += 100.0 * idle_ports / radix;
        avail_per_node.push_back(avail_ports);
        nodes_sampled++;
      }
      idle_port_frac_samples.push_back(nodes_sampled > 0 ? idle_sum / nodes_sampled : 0);
    }

    TM->genMes(packets, cyc);
    run_one_cycle(packets, system, cyc, false);
    injected_per_cycle.push_back(static_cast<double>(network->get_diag_injected_last_cycle()));

    if (do_sample && !avail_per_node.empty()) {
      double unused_sum = 0;
      for (size_t idx = 0; idx < sample_nodes.size(); idx++) {
        int avail = avail_per_node[idx];
        uint64_t used = network->get_diag_injected_port_count(sample_nodes[idx]);
        unused_sum += (avail > 0) ? (100.0 * (avail - static_cast<int>(used)) / avail) : 0;
      }
      unused_avail_samples.push_back(unused_sum / avail_per_node.size());
    }

    // 在途包诊断：按 head 所在 buffer 的 node 统计
    if (do_sample) {
      std::unordered_map<Node*, int> per_node_count;
      int at_switch = 0;
      for (Packet* p : packets) {
        if (p->finished_) continue;
        const VCInfo& ht = p->head_trace();
        if (ht.buffer == nullptr) continue;
        Node* n = ht.buffer->node_;
        per_node_count[n]++;
        if (gpu_nodes.find(n) == gpu_nodes.end()) at_switch++;
      }
      in_flight_samples.push_back(static_cast<double>(packets.size()));
      int max_per_node = 0;
      for (const auto& kv : per_node_count) max_per_node = std::max(max_per_node, kv.second);
      in_flight_per_node_max_samples.push_back(static_cast<double>(max_per_node));
      in_flight_at_switch_frac_samples.push_back(
          packets.empty() ? 0 : 100.0 * at_switch / static_cast<int>(packets.size()));
    }
  }

  uint64_t arrived_after = TM->message_arrived_.load();
  double throughput = static_cast<double>(arrived_after - arrived_before) / measure;
  if (traffic != "single_flow") throughput /= network->num_cores_;
  uint64_t no_credit_blocked = network->get_diag_no_credit_blocked();

  double inj_mean = 0, inj_var = 0, inj_under = 0;
  for (double v : injected_per_cycle) inj_mean += v;
  if (!injected_per_cycle.empty()) {
    inj_mean /= injected_per_cycle.size();
    for (double v : injected_per_cycle) {
      inj_var += (v - inj_mean) * (v - inj_mean);
      if (v < offered_load * 0.99) inj_under += 1.0;
    }
    inj_var /= injected_per_cycle.size();
    inj_under = 100.0 * inj_under / injected_per_cycle.size();
  }

  double idle_mean = 0, idle_var = 0;
  for (double v : idle_port_frac_samples) idle_mean += v;
  if (!idle_port_frac_samples.empty()) {
    idle_mean /= idle_port_frac_samples.size();
    for (double v : idle_port_frac_samples) idle_var += (v - idle_mean) * (v - idle_mean);
    idle_var /= idle_port_frac_samples.size();
  }

  double unused_mean = 0;
  for (double v : unused_avail_samples) unused_mean += v;
  if (!unused_avail_samples.empty())
    unused_mean /= unused_avail_samples.size();

  double in_flight_mean = 0, in_flight_max_val = 0;
  double in_flight_per_node_max_mean = 0, in_flight_at_switch_frac_mean = 0;
  if (!in_flight_samples.empty()) {
    for (double v : in_flight_samples) {
      in_flight_mean += v;
      in_flight_max_val = std::max(in_flight_max_val, v);
    }
    in_flight_mean /= in_flight_samples.size();
  }
  if (!in_flight_per_node_max_samples.empty()) {
    for (double v : in_flight_per_node_max_samples) in_flight_per_node_max_mean += v;
    in_flight_per_node_max_mean /= in_flight_per_node_max_samples.size();
  }
  if (!in_flight_at_switch_frac_samples.empty()) {
    for (double v : in_flight_at_switch_frac_samples) in_flight_at_switch_frac_mean += v;
    in_flight_at_switch_frac_mean /= in_flight_at_switch_frac_samples.size();
  }

  delete TM;
  TM = nullptr;
  network->enable_diagnostics(false);

  DiagResult res;
  res.scenario = traffic;
  res.injection_rate = inj_rate;
  res.throughput = throughput;
  res.inj_mean = inj_mean;
  res.inj_std = std::sqrt(inj_var);
  res.inj_under_frac = inj_under;
  res.credit_zero_frac = total_samples > 0 ? (100.0 * total_zero / total_samples) : 0;
  res.no_credit_blocked = no_credit_blocked;
  res.idle_port_frac_mean = idle_mean;
  res.idle_port_frac_std = std::sqrt(idle_var);
  res.unused_avail_mean = unused_mean;
  res.in_flight_mean = in_flight_mean;
  res.in_flight_max = in_flight_max_val;
  res.in_flight_per_node_max = in_flight_per_node_max_mean;
  res.in_flight_at_switch_frac = in_flight_at_switch_frac_mean;
  return res;
}

int main(int argc, char* argv[]) {
  std::filesystem::path out_dir = std::filesystem::path("../output");
  std::filesystem::create_directories(out_dir);
  std::string test_config = R"(
[Network]
topology = NVSwitch
num_gpus_per_group = 8
num_switches_per_group = 4
num_groups = 8
gpu_nvlink_ports = 18
switches_fully_connected = true
spine_non_blocking = true
routing_algorithm = min
gpu_switch_latency = 1
switch_switch_latency = 1
buffer_size = 128
switch_buffer_size = 256
vc_number = 2
flow_control = credit
credit_return_delay = 2

[Workload]
traffic = single_flow
packet_length = 1
traffic_scale = 1
single_flow_dest = 1

[Simulation]
simulation_time = 2000
threads = 1
issue_width = 32

[Files]
output_file = ../output/test_allreduce_diag.csv
log_file = ../output/test_allreduce_diag.log
)";

  std::filesystem::path ini_path = out_dir / "test_allreduce_diagnosis.ini";
  std::ofstream ofs(ini_path);
  ofs << test_config;
  ofs.close();

  param = new Parameters(ini_path.string());
  param->flow_control = "credit";

  gen.seed(42);
  NVSwitchSystem* system = new NVSwitchSystem();
  network = system;
  system->init_flow_control();

  const int warmup = 500;
  const int measure = 500;

  std::cout << "============================================" << std::endl;
  std::cout << "All-Reduce 饱和点诊断" << std::endl;
  std::cout << "============================================" << std::endl;
  std::cout << "拓扑: 8 group x 8 GPU, 4 leaf/group, spine_non_blocking" << std::endl;
  std::cout << "配置: buffer_size=128, switch_buffer_size=256, credit_return_delay=2 (优化)" << std::endl;
  std::cout << "对比: single_flow vs ring_all_reduce vs uniform" << std::endl;
  std::cout << "Warmup: " << warmup << " cycles, Measure: " << measure << " cycles" << std::endl;
  std::cout << std::endl;

  DiagResult r_single = run_allreduce_diagnosis("single_flow", 18.0, system, warmup, measure);
  DiagResult r_ar = run_allreduce_diagnosis("ring_all_reduce", 5.0, system, warmup, measure);
  DiagResult r_uniform = run_allreduce_diagnosis("uniform", 5.0, system, warmup, measure);

  std::cout << "\n--- 诊断结果（基础指标）---\n" << std::endl;
  std::cout << "| Scenario       | InjRate | Thr(node) | Inj/cyc(全网) | Inj<Offered% | Zero% | NoCreditBlock | IdlePort% | UnusedAvail% |" << std::endl;
  std::cout << "|----------------|---------|-----------|---------------|--------------|-------|---------------|-----------|--------------|" << std::endl;

  auto print_row = [](const DiagResult& r) {
    double offered = (r.scenario == "single_flow") ? r.injection_rate : (r.injection_rate * 64);
    printf("| %-14s | %6.1f  | %9.2f | %13.1f | %12.1f | %5.1f | %14lu | %9.1f | %12.1f |\n",
           r.scenario.c_str(), r.injection_rate, r.throughput, r.inj_mean, r.inj_under_frac,
           r.credit_zero_frac, r.no_credit_blocked, r.idle_port_frac_mean, r.unused_avail_mean);
  };
  print_row(r_single);
  print_row(r_ar);
  print_row(r_uniform);

  std::cout << "\n--- 在途包诊断（定位拥塞点）---\n" << std::endl;
  std::cout << "| Scenario       | InFlight(mean) | InFlight(max) | MaxPerNode | AtSwitch% |" << std::endl;
  std::cout << "|----------------|----------------|---------------|------------|-----------|" << std::endl;
  auto print_inflight = [](const DiagResult& r) {
    printf("| %-14s | %14.1f | %13.1f | %10.1f | %9.1f |\n",
           r.scenario.c_str(), r.in_flight_mean, r.in_flight_max,
           r.in_flight_per_node_max, r.in_flight_at_switch_frac);
  };
  print_inflight(r_single);
  print_inflight(r_ar);
  print_inflight(r_uniform);

  std::cout << "\n--- 解读 ---" << std::endl;
  std::cout << "• NoCreditBlock 高 → 大量 VC 分配因无 credit 失败，说明 credit 饥饿严重" << std::endl;
  std::cout << "• Zero% / IdlePort% 高 → per-link credit 不均衡，部分端口常无 credit" << std::endl;
  std::cout << "• Inj/cyc 低、Inj<Offered% 高 → 实际注入远低于 offered load，接收/credit 瓶颈" << std::endl;
  std::cout << "• UnusedAvail% 高 → 有 credit 但未用，可能是调度/争用导致" << std::endl;
  std::cout << "• InFlight 高、MaxPerNode 高 → 网络内部积压严重，拥塞点在交换机或接收端" << std::endl;
  std::cout << "• AtSwitch% 高 → 大量包滞留在 switch 缓冲区，交换机是瓶颈" << std::endl;
  std::cout << "• uniform vs ring_all_reduce 对比 → 可区分是 All-Reduce 模式特性还是通用多流争用" << std::endl;

  delete system;
  delete param;
  return 0;
}
