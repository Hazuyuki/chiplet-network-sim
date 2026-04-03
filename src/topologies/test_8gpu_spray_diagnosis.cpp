/**
 * 8GPU 包泼洒诊断：快速验证包是否均匀从各 Leaf 进入目的 GPU
 * 用于诊断延迟早期增加是否因包泼洒不均导致
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
  
  // 8 GPU 配置 - 与实验配置一致
  std::string test_config = R"(
[Network]
topology = NVSwitch
num_gpus_per_server = 8
num_switches_per_server = 4
num_servers_per_super_node = 1
gpu_nvlink_ports = 18
switches_fully_connected = true
spine_non_blocking = true
routing_algorithm = min
gpu_switch_latency = 1
switch_switch_latency = 1
buffer_size = 64
switch_buffer_size = 2048
vc_number = 2
flow_control = credit
credit_return_delay = 2

[Workload]
traffic = ring_all_reduce
packet_length = 1
traffic_scale = 0

[Simulation]
simulation_time = 2000
threads = 1
issue_width = 32

[Files]
output_file = ../output/test_8gpu_spray_diag.csv
log_file = ../output/test_8gpu_spray_diag.log
)";

  std::filesystem::path ini_path = out_dir / "test_8gpu_spray_diag.ini";
  std::ofstream ofs(ini_path);
  ofs << test_config;
  ofs.close();

  param = new Parameters(ini_path.string());
  param->flow_control = "credit";

  gen.seed(42);
  NVSwitchSystem* system = new NVSwitchSystem();
  network = system;
  system->init_flow_control();

  const int num_gpus_per_server = system->num_gpus_per_server_;
  const int num_switches_per_server = system->num_switches_per_server_;
  const int num_gpus = network->num_cores_;
  
  std::cout << "============================================" << std::endl;
  std::cout << "8GPU 包泼洒诊断" << std::endl;
  std::cout << "============================================" << std::endl;
  std::cout << "配置: " << num_gpus << " GPUs, " << num_switches_per_server << " Leaf switches" << std::endl;
  std::cout << "拓扑: 单 Server (num_servers_per_super_node=1)" << std::endl;
  std::cout << std::endl;

  // 测试不同注入率下的包泼洒分布
  std::vector<double> test_rates = {5.0, 10.0, 12.0, 13.0};
  
  for (double inj_rate : test_rates) {
    param->traffic = "ring_all_reduce";
    param->traffic_scale = num_gpus;
    system->reset();
    network->reset_diagnostics();
    network->enable_diagnostics(true);

    TM = new TrafficManager();
    TM->traffic_ = "ring_all_reduce";
    TM->traffic_scale_ = param->traffic_scale;
    TM->injection_rate_ = inj_rate;

    const int warmup = 200;
    const int measure = 500;
    std::vector<Packet*> packets;

    for (uint64_t cyc = 0; cyc < (uint64_t)(warmup + measure); cyc++) {
      param->current_simulation_cycle = cyc;
      network->process_pending_credits(cyc);
      TM->genMes(packets, cyc);
      run_one_cycle(packets, system, cyc, false);
    }

    const auto& counts = network->get_diag_ingress_leaf_counts();
    delete TM;
    TM = nullptr;
    network->enable_diagnostics(false);

    // 统计每个 GPU 从各 Leaf 收到的包数
    std::vector<std::vector<uint64_t>> per_dest(num_gpus, std::vector<uint64_t>(num_switches_per_server, 0));
    uint64_t total_recorded = 0;
    for (const auto& kv : counts) {
      int dest_global = kv.first.first;
      int ingress_sw = kv.first.second;
      uint64_t c = kv.second;
      if (dest_global >= 0 && dest_global < num_gpus && ingress_sw >= 0 && ingress_sw < num_switches_per_server) {
        per_dest[dest_global][ingress_sw] += c;
        total_recorded += c;
      }
    }

    // 全网汇总
    std::vector<uint64_t> sum_per_leaf(num_switches_per_server, 0);
    for (int g = 0; g < num_gpus; g++)
      for (int sw = 0; sw < num_switches_per_server; sw++)
        sum_per_leaf[sw] += per_dest[g][sw];
    
    uint64_t sum_all = 0;
    for (uint64_t c : sum_per_leaf) sum_all += c;

    std::cout << "--- Injection Rate: " << inj_rate << " ---" << std::endl;
    if (sum_all == 0) {
      std::cout << "  无包到达记录（可能未启用诊断）" << std::endl;
    } else {
      std::cout << "  总记录包数: " << sum_all << std::endl;
      std::cout << "  全网 Leaf 分布:" << std::endl;
      for (int sw = 0; sw < num_switches_per_server; sw++) {
        double pct = 100.0 * sum_per_leaf[sw] / sum_all;
        printf("    Leaf %d: %6lu (%.1f%%)\n", sw, sum_per_leaf[sw], pct);
      }
      
      // 检验均匀性
      double max_pct = 0, min_pct = 100;
      for (int sw = 0; sw < num_switches_per_server; sw++) {
        double pct = 100.0 * sum_per_leaf[sw] / sum_all;
        max_pct = std::max(max_pct, pct);
        min_pct = std::min(min_pct, pct);
      }
      printf("  均匀性检验: max=%.1f%%, min=%.1f%%, 偏差=%.1f%%\n", 
             max_pct, min_pct, (max_pct - min_pct));
      
      if (max_pct - min_pct < 20) {
        std::cout << "  -> 包泼洒较均匀" << std::endl;
      } else {
        std::cout << "  -> 包泼洒不均匀！可能导致拥塞" << std::endl;
      }
    }
    std::cout << std::endl;
  }

  delete system;
  delete param;
  return 0;
}
