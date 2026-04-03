/**
 * 诊断测试：分析包泼洒峰值吞吐与理论值差距的原因
 */

#include "config.h"
#include "nvswitch.h"
#include "packet.h"
#include "traffic_manager.h"
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

Parameters* param = nullptr;
TrafficManager* TM = nullptr;
System* network = nullptr;
boost::mt19937 gen;

// 统计信息
struct Stats {
  std::map<int, int> gpu_port_usage;   // GPU 出端口使用次数
  std::map<int, int> sw_port_in;       // Switch 入端口流量
  std::map<int, int> sw_port_out;      // Switch 出端口流量
  int total_injected = 0;
  int total_arrived = 0;
  int stall_no_credit = 0;
  int stall_link_busy = 0;
};
Stats stats;

static void run_one_cycle(std::vector<Packet*>& vec_pkts, System* system, uint64_t cyc) {
  uint64_t j = 0;
  param->current_simulation_cycle = cyc;
  network->process_pending_credits(cyc);

  for (uint64_t i = 0; i < vec_pkts.size(); ++i) {
    Packet*& pkt = vec_pkts[i];
    if (pkt->releaselink_) {
      pkt->tail_trace().buffer->release_in_link(*pkt);
      if (pkt->leaving_vc_.buffer != nullptr)
        pkt->leaving_vc_.buffer->release_sw_link();
      pkt->releaselink_ = false;
    }
    if (pkt->finished_) {
      stats.total_arrived++;
      delete pkt;
    } else {
      vec_pkts[j++] = pkt;
    }
  }
  vec_pkts.resize(j);
  for (size_t i = 0; i < vec_pkts.size(); i++)
    system->update(*vec_pkts[i]);
}

// 测试 1：理想注入测试 - 每周期尝试注入 N 个包，看实际能注入多少
static void test_injection_rate(NVSwitchSystem* system) {
  std::cout << "\n=== 测试 1: 注入率测试 ===" << std::endl;
  
  const int cycles = 100;
  const int inject_per_cycle = 18;  // 尝试每周期注入 18 个包
  
  TM = new TrafficManager();
  std::vector<Packet*> packets;
  
  int injected_count = 0;
  int arrived_count = 0;
  
  for (int cyc = 0; cyc < cycles; cyc++) {
    param->current_simulation_cycle = cyc;
    network->process_pending_credits(cyc);
    
    // 尝试注入 inject_per_cycle 个包
    for (int k = 0; k < inject_per_cycle; k++) {
      packets.push_back(new Packet(network->int_to_nodeid(0), network->int_to_nodeid(1), 1));
      injected_count++;
    }
    
    run_one_cycle(packets, system, cyc);
    arrived_count = TM->message_arrived_.load();
  }
  
  // 继续跑直到所有包完成
  int extra_cycles = 0;
  while (!packets.empty() && extra_cycles < 1000) {
    run_one_cycle(packets, system, cycles + extra_cycles);
    extra_cycles++;
  }
  arrived_count = TM->message_arrived_.load();
  
  std::cout << "  注入包数: " << injected_count << std::endl;
  std::cout << "  到达包数: " << arrived_count << std::endl;
  std::cout << "  总周期: " << (cycles + extra_cycles) << std::endl;
  std::cout << "  实际吞吐: " << (double)arrived_count / (cycles + extra_cycles) << " flits/cycle" << std::endl;
  std::cout << "  理论峰值: " << inject_per_cycle << " flits/cycle" << std::endl;
  
  delete TM;
  TM = nullptr;
}

// 测试 2：分析每个环节的延迟
static void test_per_hop_latency(NVSwitchSystem* system) {
  std::cout << "\n=== 测试 2: 单包延迟分析 ===" << std::endl;
  
  TM = new TrafficManager();
  
  // 注入一个包并跟踪
  Packet* pkt = new Packet(network->int_to_nodeid(0), network->int_to_nodeid(1), 1);
  std::vector<Packet*> packets = {pkt};
  
  std::cout << "  processing_time: " << param->processing_time << std::endl;
  std::cout << "  gpu_switch_latency: " << param->params_ptree.get<int>("Network.gpu_switch_latency", 1) << std::endl;
  
  int cycle = 0;
  while (!packets.empty() && cycle < 100) {
    run_one_cycle(packets, system, cycle);
    cycle++;
  }
  
  std::cout << "  单包完成周期: " << cycle << std::endl;
  std::cout << "  理论最小延迟: " << (2 + 1 + 1 + 1 + 1) << " (process=2 + 2hop×2)" << std::endl;
  
  delete TM;
  TM = nullptr;
}

// 测试 3：检查 Switch 端口是否均匀使用
static void test_port_balance(NVSwitchSystem* system) {
  std::cout << "\n=== 测试 3: 端口均衡测试 ===" << std::endl;
  
  TM = new TrafficManager();
  std::vector<Packet*> packets;
  
  // 注入 1000 个包
  for (int k = 0; k < 1000; k++) {
    packets.push_back(new Packet(network->int_to_nodeid(0), network->int_to_nodeid(1), 1));
  }
  
  // 统计每个包选择的 GPU 出端口
  std::map<int, int> port_count;
  
  int cycle = 0;
  while (!packets.empty() && cycle < 2000) {
    run_one_cycle(packets, system, cycle);
    cycle++;
  }
  
  std::cout << "  1000 包完成周期: " << cycle << std::endl;
  std::cout << "  平均吞吐: " << 1000.0 / cycle << " flits/cycle" << std::endl;
  
  delete TM;
  TM = nullptr;
}

// 测试 4：检查 Switch 内部是否有瓶颈
static void test_switch_capacity(NVSwitchSystem* system) {
  std::cout << "\n=== 测试 4: Switch 容量测试 ===" << std::endl;
  
  NVSwitchGroup* group = system->get_group(0);
  Node* sw = group->get_nvswitch(0);
  
  std::cout << "  Switch radix: " << sw->radix_ << std::endl;
  std::cout << "  GPU0 到 Switch 链路数: " << system->gpu_nvlink_ports_ << std::endl;
  std::cout << "  Switch 到 GPU1 链路数: " << system->gpu_nvlink_ports_ << std::endl;
  
  // 检查 buffer 配置
  std::cout << "  Buffer size per VC: " << param->buffer_size << std::endl;
  std::cout << "  VC number: " << param->vc_number << std::endl;
  std::cout << "  总 credit 容量 (per link): " << param->buffer_size * param->vc_number << std::endl;
}

// 测试 5：逐步增加注入率，找到饱和点
static void test_saturation_point(NVSwitchSystem* system) {
  std::cout << "\n=== 测试 5: 饱和点测试 ===" << std::endl;
  
  std::cout << "| 注入率 | 实际吞吐 | 效率 |" << std::endl;
  std::cout << "|--------|----------|------|" << std::endl;
  
  for (int inject_rate = 1; inject_rate <= 20; inject_rate += 1) {
    // 重置系统
    system->reset();
    
    TM = new TrafficManager();
    std::vector<Packet*> packets;
    
    const int cycles = 200;
    int total_injected = 0;
    
    for (int cyc = 0; cyc < cycles; cyc++) {
      param->current_simulation_cycle = cyc;
      network->process_pending_credits(cyc);
      
      // 注入 inject_rate 个包
      for (int k = 0; k < inject_rate; k++) {
        packets.push_back(new Packet(network->int_to_nodeid(0), network->int_to_nodeid(1), 1));
        total_injected++;
      }
      
      run_one_cycle(packets, system, cyc);
    }
    
    // 等待完成
    int extra = 0;
    while (!packets.empty() && extra < 500) {
      run_one_cycle(packets, system, cycles + extra);
      extra++;
    }
    
    int arrived = TM->message_arrived_.load();
    double throughput = (double)arrived / (cycles + extra);
    double efficiency = throughput / inject_rate * 100;
    
    printf("| %6d | %8.2f | %4.0f%% |\n", inject_rate, throughput, efficiency);
    
    delete TM;
    TM = nullptr;
  }
}

// 测试 6：检查 credit 使用情况
static void test_credit_utilization(NVSwitchSystem* system) {
  std::cout << "\n=== 测试 6: Credit 利用率测试 ===" << std::endl;
  
  NVSwitchGroup* group = system->get_group(0);
  Node* gpu0 = group->get_gpu(0);
  
  std::cout << "  GPU0 radix: " << gpu0->radix_ << std::endl;
  std::cout << "  初始 credit (port 0, vc 0): " << gpu0->get_credit(0, 0) << std::endl;
  
  TM = new TrafficManager();
  std::vector<Packet*> packets;
  
  // 注入大量包
  for (int k = 0; k < 100; k++) {
    packets.push_back(new Packet(network->int_to_nodeid(0), network->int_to_nodeid(1), 1));
  }
  
  // 跑 10 个周期后检查 credit
  for (int cyc = 0; cyc < 10; cyc++) {
    run_one_cycle(packets, system, cyc);
  }
  
  std::cout << "  10 周期后各端口 credit:" << std::endl;
  int total_used = 0;
  int total_avail = 0;
  for (int p = 0; p < gpu0->radix_; p++) {
    int c0 = gpu0->get_credit(p, 0);
    int c1 = gpu0->get_credit(p, 1);
    total_avail += param->buffer_size * 2;
    total_used += (param->buffer_size * 2 - c0 - c1);
    if (p < 5) {
      std::cout << "    port " << p << ": vc0=" << c0 << ", vc1=" << c1 << std::endl;
    }
  }
  std::cout << "  Credit 使用率: " << (100.0 * total_used / total_avail) << "%" << std::endl;
  
  // 继续跑完
  int cyc = 10;
  while (!packets.empty() && cyc < 500) {
    run_one_cycle(packets, system, cyc++);
  }
  
  delete TM;
  TM = nullptr;
}

int main(int argc, char* argv[]) {
  std::string config_path = "input/nvswitch_single_flow_exp.ini";
  if (argc > 1) config_path = argv[1];
  
  // 使用 RTT=0 的配置
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
simulation_time = 100
timeout_threshold = 1000
timeout_limit = 100
threads = 1
issue_width = 32

[Files]
output_file = test_bottleneck_output.csv
log_file = test_bottleneck.log
)";
  
  // 写入临时配置
  std::ofstream ofs("output/test_bottleneck.ini");
  ofs << test_config;
  ofs.close();
  
  param = new Parameters("output/test_bottleneck.ini");
  
  std::cout << "============================================" << std::endl;
  std::cout << "吞吐瓶颈诊断测试" << std::endl;
  std::cout << "============================================" << std::endl;
  std::cout << "配置: 2 GPU, 1 Switch, gpu_nvlink_ports=18" << std::endl;
  std::cout << "理论峰值: 18 flits/cycle" << std::endl;
  
  NVSwitchSystem* system = new NVSwitchSystem();
  network = system;
  system->init_flow_control();
  
  test_switch_capacity(system);
  test_per_hop_latency(system);
  
  system->reset();
  test_credit_utilization(system);
  
  system->reset();
  test_port_balance(system);
  
  system->reset();
  test_saturation_point(system);
  
  std::cout << "\n============================================" << std::endl;
  std::cout << "诊断完成" << std::endl;
  std::cout << "============================================" << std::endl;
  
  delete system;
  delete param;
  return 0;
}
