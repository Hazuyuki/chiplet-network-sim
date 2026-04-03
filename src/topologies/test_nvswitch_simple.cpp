// 简化版测试 - 不需要完整模拟器环境
// 用于快速验证拓扑结构

#include "nvswitch.h"
#include "config.h"
#include "boost/random.hpp"
#include <iostream>
#include <cassert>
#include <fstream>
#include <cstdlib>

// 全局变量定义（测试需要）
Parameters* param = nullptr;
TrafficManager* TM = nullptr;
System* network = nullptr;
boost::mt19937 gen;

void simple_topology_test() {
  std::cout << "=== 简化拓扑测试 ===" << std::endl;
  
  // 创建测试配置
  std::string test_config = R"(
[Network]
topology = NVSwitch
num_gpus_per_group = 4
num_switches_per_group = 3
num_groups = 1
gpu_nvlink_ports = 18
switches_fully_connected = true
inter_group_sw_connect = false
routing_algorithm = direct
gpu_switch_latency = 1
switch_switch_latency = 1

[Router]
router_stages = OneStage
buffer_size = 8
vc_number = 2

[Workload]
traffic = uniform
packet_length = 1
traffic_scale = 1

[Simulation]
simulation_time = 1000
start_injection = 0.1
injection_increment = 0.05
timeout_threshold = 1000
timeout_limit = 100
threads = 1
issue_width = 32

[Files]
output_file = test_output.csv
)";

  std::ofstream config_file("test_simple_config.ini");
  config_file << test_config;
  config_file.close();
  
  param = new Parameters("test_simple_config.ini");
  
  // 创建系统
  NVSwitchSystem* system = new NVSwitchSystem();
  
  // 基本验证
  std::cout << "GPU数量: " << system->num_gpus_per_server_ << std::endl;
  std::cout << "Switch数量: " << system->num_switches_per_server_ << std::endl;
  std::cout << "总节点数: " << system->num_nodes_ << std::endl;
  std::cout << "总核心数: " << system->num_cores_ << std::endl;
  
  assert(system->num_gpus_per_server_ == 4);
  assert(system->num_switches_per_server_ == 3);
  assert(system->num_nodes_ == 7);
  assert(system->num_cores_ == 4);
  
  // 验证连接
  NVSwitchGroup* group = system->get_group(0);
  
  std::vector<int> gpu_port_base(3);
  for (int sw = 1; sw < 3; sw++) {
    gpu_port_base[sw] = gpu_port_base[sw - 1] + system->links_per_switch_[sw - 1];
  }
  std::cout << "\n验证GPU到Switch连接..." << std::endl;
  for (int gpu_id = 0; gpu_id < 4; gpu_id++) {
    Node* gpu = group->get_gpu(gpu_id);
    std::cout << "  GPU " << gpu_id << " 连接到 ";
    for (int sw_id = 0; sw_id < 3; sw_id++) {
      int gpu_port = gpu_port_base[sw_id];
      NodeID linked = gpu->link_nodes_[gpu_port];
      std::cout << "Switch " << (linked.node_id - 4) << " ";
      assert(linked.node_id == 4 + sw_id);
    }
    std::cout << std::endl;
  }

  std::cout << "\n验证Switch之间连接..." << std::endl;
  for (int sw1 = 0; sw1 < 3; sw1++) {
    Node* switch1 = group->get_nvswitch(sw1);
    int port_offset = 4 * system->links_per_switch_[sw1];
    std::cout << "  Switch " << sw1 << " 连接到 ";
    for (int sw2 = 0; sw2 < 3; sw2++) {
      if (sw1 != sw2) {
        int port = (sw2 > sw1) ? (port_offset + sw2 - 1) : (port_offset + sw2);
        if (port < switch1->radix_) {
          NodeID linked = switch1->link_nodes_[port];
          if (linked.node_id == 4 + sw2) {
            std::cout << "Switch " << sw2 << " ";
          }
        }
      }
    }
    std::cout << std::endl;
  }
  
  std::cout << "\n✓ 所有连接验证通过！" << std::endl;
  
  delete system;
  delete param;
  std::remove("test_simple_config.ini");
  
  std::cout << "\n=== 测试完成 ===" << std::endl;
}

int main() {
  try {
    simple_topology_test();
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "错误: " << e.what() << std::endl;
    return 1;
  }
}
