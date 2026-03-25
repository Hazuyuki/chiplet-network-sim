/**
 * GPU Port 按 Leaf 分组统计：验证来自每个 Leaf 的流量是否均匀分布在对应的 GPU port 上
 */
#include "config.h"
#include "nvswitch.h"
#include "packet.h"
#include "traffic_manager.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

Parameters* param = nullptr;
TrafficManager* TM = nullptr;
System* network = nullptr;
boost::mt19937 gen;

static void run_one_cycle(std::vector<Packet*>& vec_pkts, System* system, uint64_t cyc) {
  param->current_simulation_cycle = cyc;
  network->process_pending_credits(cyc);
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
    network->update(*vec_pkts[i]);
}

static int get_leaf_id_from_port(int port, int num_switches) {
  if (num_switches == 4) {
    if (port <= 4) return 0;
    if (port <= 9) return 1;
    if (port <= 13) return 2;
    return 3;
  }
  return port / (18 / num_switches);
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
output_file = ../output/gpu18port.csv
log_file = ../output/gpu18port.log
)";

  std::filesystem::path ini_path = out_dir / "test_gpu_port_per_leaf.ini";
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
  const double inj_rate = 5.0;
  const int num_gpus = network->num_cores_;
  const int num_leaves = system->num_switches_per_server_;
  const int gpu_radix = 18;

  param->traffic = "ring_all_reduce";
  param->traffic_scale = num_gpus;
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
    run_one_cycle(packets, system, cyc);
  }

  const auto& usage = network->get_diag_gpu_port_usage();
  delete TM;
  TM = nullptr;
  network->enable_diagnostics(false);

  std::vector<uint64_t> per_leaf_raw(num_leaves, 0);
  uint64_t total = 0;
  for (const auto& kv : usage) {
    int port = kv.first.second;
    uint64_t c = kv.second;
    int leaf = get_leaf_id_from_port(port, num_leaves);
    if (leaf >= 0 && leaf < num_leaves) {
      per_leaf_raw[leaf] += c;
      total += c;
    }
  }

  std::cout << "============================================" << std::endl;
  std::cout << "GPU Port 按 Leaf 分组统计" << std::endl;
  std::cout << "============================================" << std::endl;
  std::cout << "配置: " << num_gpus << " GPUs, " << num_leaves << " Leaves" << std::endl;
  std::cout << "Port->Leaf 映射: ";
  if (num_leaves == 4) {
    std::cout << "Leaf0=ports0-4, Leaf1=ports5-9, Leaf2=ports10-13, Leaf3=ports14-17" << std::endl;
  }
  std::cout << std::endl;

  std::vector<std::vector<uint64_t>> gpu_leaf_dist(num_gpus, std::vector<uint64_t>(num_leaves, 0));
  for (const auto& kv : usage) {
    int gpu = kv.first.first;
    int port = kv.first.second;
    uint64_t c = kv.second;
    int leaf = get_leaf_id_from_port(port, num_leaves);
    if (gpu >= 0 && gpu < num_gpus && leaf >= 0 && leaf < num_leaves) {
      gpu_leaf_dist[gpu][leaf] += c;
    }
  }

  std::cout << "--- 每个 GPU 的入 Port 分布 (按 Leaf 分组) ---" << std::endl;
  for (int gpu = 0; gpu < std::min(16, num_gpus); gpu++) {
    uint64_t gpu_total = 0;
    for (int leaf = 0; leaf < num_leaves; leaf++) gpu_total += gpu_leaf_dist[gpu][leaf];
    if (gpu_total == 0) continue;

    std::cout << "GPU " << gpu << ": ";
    for (int leaf = 0; leaf < num_leaves; leaf++) {
      double pct = 100.0 * gpu_leaf_dist[gpu][leaf] / gpu_total;
      std::cout << "Leaf" << leaf << "=" << gpu_leaf_dist[gpu][leaf] << "(" << pct << "%) ";
    }
    std::cout << std::endl;
  }
  std::cout << std::endl;

  std::cout << "--- 全网汇总: 每个 Leaf 收到的包数 ---" << std::endl;
  double expected_per_leaf = 100.0 / num_leaves;
  double max_dev = 0;

  for (int leaf = 0; leaf < num_leaves; leaf++) {
    double pct = (total > 0) ? (100.0 * per_leaf_raw[leaf] / total) : 0;
    double dev = std::abs(pct - expected_per_leaf);
    max_dev = std::max(max_dev, dev);
    std::cout << "  Leaf " << leaf << ": " << per_leaf_raw[leaf] << " (" << pct << "%), 期望 " << expected_per_leaf << "%" << std::endl;
  }
  std::cout << std::endl;

  std::cout << "--- 结论 ---" << std::endl;
  if (max_dev < 10) {
    std::cout << "✅ 每个 Leaf 收到的包数较均匀（偏差 < 10%），包泼洒正常工作。" << std::endl;
  } else if (max_dev < 20) {
    std::cout << "⚠️ 每个 Leaf 收到的包数有偏差（" << max_dev << "%），可能有轻微不均匀。" << std::endl;
  } else {
    std::cout << "❌ 每个 Leaf 收到的包数严重不均匀（偏差 " << max_dev << "%）！" << std::endl;
  }

  delete system;
  delete param;
  return 0;
}
