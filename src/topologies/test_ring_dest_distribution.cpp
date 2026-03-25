/**
 * Ring 目的分布诊断：检查每个源 GPU 发往的目的 GPU 是否分布在多个 Leaf
 * 在 NVSwitch 拓扑中，同一 server 内的 GPU 共享同一个 Leaf
 */
#include "config.h"
#include "nvswitch.h"
#include "packet.h"
#include "traffic_manager.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <unordered_map>
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
    system->update(*vec_pkts[i]);
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
output_file = ../output/ring_dest_diag.csv
log_file = ../output/ring_dest_diag.log
)";

  std::filesystem::path ini_path = out_dir / "test_ring_dest_diag.ini";
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
  const int gpus_per_server = system->num_gpus_per_server_;
  const int num_servers = num_gpus / gpus_per_server;
  const int num_leaves = system->num_switches_per_server_;
  
  std::cout << "============================================" << std::endl;
  std::cout << "Ring 目的分布诊断" << std::endl;
  std::cout << "============================================" << std::endl;
  std::cout << "配置: " << num_gpus << " GPUs, " << num_servers << " servers, " 
            << gpus_per_server << " GPUs/server" << std::endl;
  std::cout << "Leaf switches per server: " << num_leaves << std::endl;
  std::cout << std::endl;

  // 检查每个源 GPU 发往的目的 GPU 分布
  std::cout << "=== 分析 Ring 路由 (src -> dest) ===" << std::endl;
  
  for (int src = 0; src < std::min(16, num_gpus); src += 8) {
    int dest = (src + 1) % num_gpus;
    int src_server = src / gpus_per_server;
    int src_gpu_in_server = src % gpus_per_server;
    int dest_server = dest / gpus_per_server;
    int dest_gpu_in_server = dest % gpus_per_server;
    
    std::cout << "GPU " << src << " -> GPU " << dest << std::endl;
    std::cout << "  源: Server " << src_server << ", GPU " << src_gpu_in_server << " in server" << std::endl;
    std::cout << "  目的: Server " << dest_server << ", GPU " << dest_gpu_in_server << " in server" << std::endl;
    std::cout << "  同 Server? " << (src_server == dest_server ? "是" : "否") << std::endl;
    std::cout << std::endl;
  }

  // 运行一小段仿真，统计实际发送的包的目的分布
  param->traffic = "collective_ring_all_reduce";
  param->traffic_scale = num_gpus;
  system->reset();
  network->reset_diagnostics();
  network->enable_diagnostics(true);

  TM = new TrafficManager();
  TM->traffic_ = "collective_ring_all_reduce";
  TM->traffic_scale_ = param->traffic_scale;
  TM->data_size = 8192;

  const int warmup = 100;
  const int measure = 100;
  std::vector<Packet*> packets;
  std::unordered_map<int, int> dest_counts;  // 统计每个目的 GPU 收到的包数

  for (uint64_t cyc = 0; cyc < (uint64_t)(warmup + measure); cyc++) {
    param->current_simulation_cycle = cyc;
    network->process_pending_credits(cyc);
    TM->genMes(packets, cyc);
    
    // 统计本周期生成的目的分布
    for (Packet* pkt : packets) {
      int dest_id = pkt->destination_.node_id;
      dest_counts[dest_id]++;
    }
    
    run_one_cycle(packets, system, cyc);
  }

  // 分析目的分布
  std::cout << "=== 实际发送包的目的 GPU 分布 ===" << std::endl;
  int total_packets = 0;
  for (const auto& kv : dest_counts) {
    total_packets += kv.second;
  }
  
  if (total_packets > 0) {
    std::cout << "总包数: " << total_packets << std::endl;
    
    // 按 Server 分组统计
    std::vector<int> server_recv(num_servers, 0);
    for (const auto& kv : dest_counts) {
      int dest = kv.first;
      int server = dest / gpus_per_server;
      server_recv[server] += kv.second;
    }
    
    std::cout << "各 Server 收到包数:" << std::endl;
    for (int s = 0; s < num_servers; s++) {
      double pct = 100.0 * server_recv[s] / total_packets;
      std::cout << "  Server " << s << ": " << server_recv[s] << " (" << pct << "%)" << std::endl;
    }
    
    // 检查是否均匀分布
    int max_recv = *max_element(server_recv.begin(), server_recv.end());
    int min_recv = *min_element(server_recv.begin(), server_recv.end());
    double expected = total_packets / (double)num_servers;
    std::cout << std::endl;
    std::cout << "期望每 Server: " << expected << std::endl;
    std::cout << "最大/最小: " << max_recv << " / " << min_recv << std::endl;
    
    if (max_recv > 2 * min_recv) {
      std::cout << "警告: 目的分布不均匀！某些 Server 收到包明显多于其他。" << std::endl;
    }
  }

  delete TM;
  delete system;
  delete param;
  return 0;
}
