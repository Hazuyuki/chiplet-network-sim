/**
 * GPU Port 实际映射诊断：打印 GPU port -> Switch 的实际连接关系
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

  std::filesystem::path ini_path = out_dir / "test_gpu_port_mapping.ini";
  std::ofstream ofs(ini_path);
  ofs << test_config;
  ofs.close();

  param = new Parameters(ini_path.string());
  param->flow_control = "credit";
  gen.seed(42);
  NVSwitchSystem* system = new NVSwitchSystem();
  network = system;
  system->init_flow_control();

  const int num_gpus = network->num_cores_;
  const int num_servers = system->num_servers_per_super_node_;
  const int gpus_per_server = system->num_gpus_per_server_;
  const int num_leaves = system->num_switches_per_server_;
  const int gpu_radix = 18;

  std::cout << "============================================" << std::endl;
  std::cout << "GPU Port 实际连接拓扑诊断" << std::endl;
  std::cout << "============================================" << std::endl;
  std::cout << "配置: " << num_gpus << " GPUs, " << num_servers << " servers, "
            << gpus_per_server << " GPUs/server, " << num_leaves << " leaves" << std::endl;

  // 获取 links_per_switch 信息
  const auto& links_per_switch = system->get_group(0)->links_per_switch_;
  std::cout << "\n每个 Leaf Switch 的 GPU 连接数 (links_per_switch):" << std::endl;
  int total_links = 0;
  for (int sw = 0; sw < num_leaves; sw++) {
    std::cout << "  Leaf " << sw << ": " << links_per_switch[sw] << " links per GPU" << std::endl;
    total_links += links_per_switch[sw];
  }
  std::cout << "  总计: " << total_links << " links per GPU" << std::endl;
  std::cout << "  GPU radix: " << gpu_radix << std::endl;

  // 分析 GPU port 到 Leaf 的映射
  std::cout << "\nGPU Port -> Leaf Switch 映射:" << std::endl;
  std::cout << "（这是根据连接关系的推测，需要验证）" << std::endl;

  // 计算累积
  std::vector<int> port_base(num_leaves, 0);
  for (int sw = 1; sw < num_leaves; sw++) {
    port_base[sw] = port_base[sw-1] + links_per_switch[sw-1];
  }

  std::cout << "\n累积端口映射:" << std::endl;
  for (int sw = 0; sw < num_leaves; sw++) {
    int start = port_base[sw];
    int end = start + links_per_switch[sw] - 1;
    std::cout << "  Leaf " << sw << ": ports " << start << " - " << end << std::endl;
  }

  // 统计 GPU port 使用（从诊断数据）
  system->reset();
  network->reset_diagnostics();
  network->enable_diagnostics(true);

  param->traffic = "ring_all_reduce";
  param->traffic_scale = num_gpus;
  TM = new TrafficManager();
  TM->traffic_ = "ring_all_reduce";
  TM->traffic_scale_ = param->traffic_scale;
  TM->injection_rate_ = 5.0;

  std::vector<Packet*> packets;
  const int warmup = 500;
  const int measure = 1000;

  for (uint64_t cyc = 0; cyc < (uint64_t)(warmup + measure); cyc++) {
    param->current_simulation_cycle = cyc;
    network->process_pending_credits(cyc);
    TM->genMes(packets, cyc);

    size_t j = 0;
    for (size_t i = 0; i < packets.size(); ++i) {
      Packet*& pkt = packets[i];
      if (pkt->finished_) {
        delete pkt;
      } else {
        packets[j++] = pkt;
      }
    }
    packets.resize(j);
    for (size_t i = 0; i < packets.size(); i++)
      network->update(*packets[i]);
  }

  const auto& usage = network->get_diag_gpu_port_usage();
  network->enable_diagnostics(false);

  // 按推测的映射统计
  std::vector<uint64_t> per_leaf(num_leaves, 0);
  uint64_t total = 0;
  for (const auto& kv : usage) {
    int port = kv.first.second;
    uint64_t c = kv.second;
    int leaf = -1;
    for (int sw = 0; sw < num_leaves; sw++) {
      if (port >= port_base[sw] && port < port_base[sw] + links_per_switch[sw]) {
        leaf = sw;
        break;
      }
    }
    if (leaf >= 0 && leaf < num_leaves) {
      per_leaf[leaf] += c;
      total += c;
    }
  }

  std::cout << "\n--- 使用推测映射统计的 Leaf 分布 ---" << std::endl;
  for (int sw = 0; sw < num_leaves; sw++) {
    double pct = (total > 0) ? (100.0 * per_leaf[sw] / total) : 0;
    std::cout << "  Leaf " << sw << ": " << per_leaf[sw] << " (" << pct << "%)" << std::endl;
  }

  delete TM;
  delete system;
  delete param;
  return 0;
}
