/**
 * 链路瓶颈诊断：统计因「出链路本周期已被占用」而未能发送的次数，按链路类型聚合
 * 用于定位 64 卡 8 机 ring all-reduce 时到底卡在 GPU→Leaf / Leaf→Spine / Spine→Leaf / Leaf→GPU / Leaf→Leaf 哪一段
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
#include <string>
#include <vector>

Parameters* param = nullptr;
TrafficManager* TM = nullptr;
System* network = nullptr;
boost::mt19937 gen;

enum class LinkType {
  GPU_LEAF,
  LEAF_GPU,
  LEAF_LEAF,
  LEAF_SPINE,
  SPINE_LEAF,
  UNKNOWN
};

static const char* link_type_name(LinkType t) {
  switch (t) {
    case LinkType::GPU_LEAF: return "GPU->Leaf";
    case LinkType::LEAF_GPU: return "Leaf->GPU";
    case LinkType::LEAF_LEAF: return "Leaf->Leaf";
    case LinkType::LEAF_SPINE: return "Leaf->Spine";
    case LinkType::SPINE_LEAF: return "Spine->Leaf";
    default: return "Unknown";
  }
}

static void run_one_cycle(std::vector<Packet*>& vec_pkts, System* system, uint64_t cyc,
                          bool do_credit_return = true) {
  param->current_simulation_cycle = cyc;
  if (do_credit_return) network->process_pending_credits(cyc);

  size_t j = 0;
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

// 对 NVSwitch 拓扑，按 (node, port) 统计 link blocked，并聚合为链路类型
static void aggregate_link_blocked_by_type(NVSwitchSystem* sys,
                                          std::vector<uint64_t>& count_per_type) {
  count_per_type.assign(6, 0);  // GPU_LEAF, LEAF_GPU, LEAF_LEAF, LEAF_SPINE, SPINE_LEAF, UNKNOWN
  const int ng = sys->num_groups_;
  const int gpu_per_group = sys->num_gpus_per_server_;
  const int sw_per_group = sys->num_switches_per_server_;
  const int n_spine = sys->num_spine_switches_;
  const int links_per_pair = sys->spine_leaf_links_per_pair_;
  const auto& links_per_switch = sys->links_per_switch_;

  // GPU 节点：group 0..ng-1, node_id 0..gpu_per_group-1，所有端口 GPU->Leaf
  for (int g = 0; g < ng; g++) {
    NVSwitchGroup* grp = sys->get_group(g);
    for (int i = 0; i < gpu_per_group; i++) {
      Node* node = grp->get_gpu(i);
      for (int p = 0; p < node->radix_; p++) {
        uint64_t c = network->get_diag_link_blocked_count(node, p);
        count_per_type[static_cast<int>(LinkType::GPU_LEAF)] += c;
      }
    }
  }

  // Leaf 节点：同 group，node_id = gpu_per_group..gpu_per_group+sw_per_group-1
  const int intra_ports = sw_per_group - 1;
  const int spine_ports = n_spine * links_per_pair;
  for (int g = 0; g < ng; g++) {
    NVSwitchGroup* grp = sys->get_group(g);
    for (int sw = 0; sw < sw_per_group; sw++) {
      Node* leaf = grp->get_nvswitch(sw);
      int n_links = links_per_switch[sw];
      int gpu_ports_end = gpu_per_group * n_links;
      int spine_base = gpu_ports_end + intra_ports;
      for (int p = 0; p < leaf->radix_; p++) {
        uint64_t c = network->get_diag_link_blocked_count(leaf, p);
        LinkType t = LinkType::UNKNOWN;
        if (p < gpu_ports_end)
          t = LinkType::LEAF_GPU;
        else if (p < spine_base)
          t = LinkType::LEAF_LEAF;
        else if (p < spine_base + spine_ports)
          t = LinkType::LEAF_SPINE;
        if (t != LinkType::UNKNOWN)
          count_per_type[static_cast<int>(t)] += c;
        else
          count_per_type[static_cast<int>(LinkType::UNKNOWN)] += c;
      }
    }
  }

  // Spine 节点：group_id == ng, node_id 0..n_spine-1，所有端口 Spine->Leaf
  if (n_spine > 0) {
    NVSwitchSpineGroup* spine_grp = sys->get_spine_group();
    if (spine_grp) {
      for (int i = 0; i < n_spine; i++) {
        Node* spine = spine_grp->get_spine_switch(i);
        for (int p = 0; p < spine->radix_; p++) {
          uint64_t c = network->get_diag_link_blocked_count(spine, p);
          count_per_type[static_cast<int>(LinkType::SPINE_LEAF)] += c;
        }
      }
    }
  }
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
spine_leaf_links_per_pair = 4
routing_algorithm = min
gpu_switch_latency = 1
switch_switch_latency = 1
buffer_size = 128
switch_buffer_size = 256
vc_number = 2
flow_control = credit
credit_return_delay = 2

[Workload]
traffic = ring_all_reduce
packet_length = 1
traffic_scale = 0

[Simulation]
simulation_time = 1
start_injection = 0.1
injection_increment = 0.5
max_injection = 18
timeout_threshold = 2000
timeout_limit = 200
threads = 1
issue_width = 32

[Files]
output_file = ../output/link_bottleneck.csv
log_file = ../output/link_bottleneck.log
)";

  std::filesystem::path ini_path = out_dir / "test_link_bottleneck_diagnosis.ini";
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
  const int measure = 1000;
  const double inj_rate = 5.0;  // 饱和点附近

  param->traffic = "ring_all_reduce";
  param->traffic_scale = network->num_cores_;
  system->reset();
  network->reset_diagnostics();
  network->enable_diagnostics(true);

  TM = new TrafficManager();
  TM->traffic_ = "ring_all_reduce";
  TM->traffic_scale_ = param->traffic_scale;
  TM->injection_rate_ = inj_rate;

  std::vector<Packet*> packets;
  for (uint64_t cyc = 0; cyc < (uint64_t)(warmup + measure); cyc++) {
    param->current_simulation_cycle = cyc;
    network->process_pending_credits(cyc);
    TM->genMes(packets, cyc);
    run_one_cycle(packets, system, cyc, false);
  }

  std::vector<uint64_t> count_per_type;
  aggregate_link_blocked_by_type(system, count_per_type);
  uint64_t total = 0;
  for (uint64_t c : count_per_type) total += c;

  delete TM;
  TM = nullptr;
  network->enable_diagnostics(false);

  std::cout << "============================================" << std::endl;
  std::cout << "链路瓶颈诊断 (64 卡 8 机 ring_all_reduce, inj=" << inj_rate << ")" << std::endl;
  std::cout << "============================================" << std::endl;
  std::cout << "统计: 因「出链路本周期已被占用」导致未能发送的次数（按链路类型聚合）" << std::endl;
  std::cout << "Warmup: " << warmup << " cycles, Measure: " << measure << " cycles" << std::endl;
  std::cout << std::endl;

  const std::vector<LinkType> types = {
      LinkType::GPU_LEAF, LinkType::LEAF_GPU, LinkType::LEAF_LEAF,
      LinkType::LEAF_SPINE, LinkType::SPINE_LEAF, LinkType::UNKNOWN};

  std::cout << "| 链路类型      | Blocked 次数 | 占比(%) |" << std::endl;
  std::cout << "|---------------|--------------|--------|" << std::endl;
  for (LinkType t : types) {
    int idx = static_cast<int>(t);
    uint64_t c = count_per_type[idx];
    double pct = (total > 0) ? (100.0 * c / total) : 0;
    printf("| %-13s | %12lu | %6.1f |\n", link_type_name(t), c, pct);
  }
  std::cout << "| 合计          | " << total << " | 100.0 |" << std::endl;

  std::cout << "\n--- 解读 ---" << std::endl;
  std::cout << "• 占比最高的链路类型即为瓶颈段：多流争用该段出端口导致本周期无法发送。" << std::endl;
  std::cout << "• Leaf->GPU 高 → 交换机到 GPU 下行争用（目的 GPU 入口满/链路忙）" << std::endl;
  std::cout << "• GPU->Leaf 高 → 注入端到 Leaf 上行争用" << std::endl;
  std::cout << "• Leaf->Spine / Spine->Leaf 高 → 跨组 spine 段争用" << std::endl;
  std::cout << "• Leaf->Leaf 高 → 组内 leaf 间争用" << std::endl;

  std::filesystem::path csv_path = out_dir / "link_bottleneck_by_type.csv";
  std::ofstream csv(csv_path);
  csv << "link_type,blocked_count,pct\n";
  for (LinkType t : types) {
    int idx = static_cast<int>(t);
    uint64_t c = count_per_type[idx];
    double pct = (total > 0) ? (100.0 * c / total) : 0;
    csv << link_type_name(t) << "," << c << "," << pct << "\n";
  }
  csv.close();
  std::cout << "\n结果已写入: " << csv_path << std::endl;

  delete system;
  delete param;
  return 0;
}
