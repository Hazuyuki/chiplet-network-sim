/**
 * 延迟分解诊断：分析端到端延迟的各个组成部分
 * - 跳数延迟 ( hops * switch_latency )
 * - 处理延迟 ( processing_time )
 * - 队列等待延迟 ( 实际延迟 - 理论最小延迟 )
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
output_file = ../output/test_latency_breakdown.csv
log_file = ../output/test_latency_breakdown.log
)";

  std::filesystem::path ini_path = out_dir / "test_latency_breakdown.ini";
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
  
  std::cout << "============================================" << std::endl;
  std::cout << "延迟分解诊断" << std::endl;
  std::cout << "============================================" << std::endl;
  
  // 理论最小延迟计算
  // 单 Server 内: GPU -> Leaf Switch -> (无 Spine) -> 目的 Leaf Switch -> GPU
  // 跳数 = 2 (GPU->Leaf + Leaf->GPU)
  // 每跳延迟 = 1 (switch_latency)
  // 处理延迟 = 2 (processing_time)
  // Credit 返回延迟 = 2 (credit_return_delay)
  // 理论最小延迟 = 2*1 + 2 + 2 = 6 周期
  const double theoretical_min_latency = 6.0;
  std::cout << "理论最小延迟: " << theoretical_min_latency << " 周期" << std::endl;
  std::cout << "  - 跳数: 2 (GPU->Leaf->GPU)" << std::endl;
  std::cout << "  - Switch 延迟: 2 周期" << std::endl;
  std::cout << "  - 处理延迟: 2 周期" << std::endl;
  std::cout << "  - Credit 返回延迟: 2 周期" << std::endl;
  std::cout << std::endl;

  std::vector<double> test_rates = {5.0, 10.0, 11.0, 12.0, 12.5, 13.0, 14.0, 16.0, 18.0};
  
  std::cout << "| InjRate | 实际延迟 | 理论最小 | 队列等待 | 队列/实际% |" << std::endl;
  std::cout << "|---------|----------|----------|----------|------------|" << std::endl;

  for (double inj_rate : test_rates) {
    param->traffic = "ring_all_reduce";
    param->traffic_scale = num_gpus;
    system->reset();

    TM = new TrafficManager();
    TM->traffic_ = "ring_all_reduce";
    TM->traffic_scale_ = param->traffic_scale;
    TM->injection_rate_ = inj_rate;

    const int warmup = 500;
    const int measure = 1000;
    std::vector<Packet*> packets;

    for (uint64_t cyc = 0; cyc < (uint64_t)(warmup + measure); cyc++) {
      param->current_simulation_cycle = cyc;
      network->process_pending_credits(cyc);
      TM->genMes(packets, cyc);
      run_one_cycle(packets, system, cyc, false);
    }

    double avg_latency = TM->average_latency();
    double queue_wait = avg_latency - theoretical_min_latency;
    double queue_pct = (avg_latency > 0) ? (queue_wait / avg_latency * 100) : 0;
    
    printf("| %7.1f | %8.2f | %8.2f | %8.2f | %10.1f |\n",
           inj_rate, avg_latency, theoretical_min_latency, queue_wait, queue_pct);

    delete TM;
    TM = nullptr;
  }

  std::cout << std::endl;
  std::cout << "--- 解读 ---" << std::endl;
  std::cout << "队列等待 = 实际延迟 - 理论最小延迟 (6 周期)" << std::endl;
  std::cout << "队列等待包括:" << std::endl;
  std::cout << "  - Switch 内部缓冲区排队" << std::endl;
  std::cout << "  - VC 分配等待" << std::endl;
  std::cout << "  - Switch 调度等待" << std::endl;
  std::cout << "  - Credit 不足导致的等待" << std::endl;

  delete system;
  delete param;
  return 0;
}
