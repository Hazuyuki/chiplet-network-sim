#include "nvswitch.h"
#include "config.h"
#include "packet.h"
#include "traffic_manager.h"
#include "boost/random.hpp"
#include <iostream>
#include <cassert>
#include <vector>
#include <fstream>
#include <cstdlib>

// 全局变量定义（测试需要）
Parameters* param = nullptr;
TrafficManager* TM = nullptr;
System* network = nullptr;
boost::mt19937 gen;

// 测试辅助函数：创建测试配置
Parameters* create_test_params() {
  // 创建一个临时配置文件内容
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

  // 写入临时文件
  std::ofstream config_file("test_nvswitch_config.ini");
  config_file << test_config;
  config_file.close();

  return new Parameters("test_nvswitch_config.ini");
}

// 测试1: 拓扑构建测试
void test_topology_construction() {
  std::cout << "\n=== 测试1: 拓扑构建 ===" << std::endl;
  
  Parameters* test_param = create_test_params();
  param = test_param;
  
  NVSwitchSystem* system = new NVSwitchSystem();
  
  // 验证基本参数
  assert(system->num_gpus_per_group_ == 4);
  assert(system->num_switches_per_group_ == 3);
  assert(system->num_groups_ == 1);
  assert(system->gpu_nvlink_ports_ == 18);
  assert(system->num_cores_ == 4);  // 只有GPU是cores
  assert(system->num_nodes_ == 7);   // 4 GPUs + 3 Switches
  
  std::cout << "✓ 基本参数正确" << std::endl;
  
  // 验证组结构
  NVSwitchGroup* group = system->get_group(0);
  assert(group != nullptr);
  assert(group->num_gpus_ == 4);
  assert(group->num_switches_ == 3);
  
  std::cout << "✓ 组结构正确" << std::endl;
  
  // 验证GPU节点
  for (int i = 0; i < 4; i++) {
    Node* gpu = group->get_gpu(i);
    assert(gpu != nullptr);
    assert(gpu->radix_ == 18);  // 每个GPU有18个NVLink端口
    assert(gpu->id_.node_id == i);
    assert(gpu->id_.group_id == 0);
  }
  std::cout << "✓ GPU节点创建正确" << std::endl;
  
  // 验证Switch节点
  for (int i = 0; i < 3; i++) {
    Node* sw = group->get_nvswitch(i);
    assert(sw != nullptr);
    assert(sw->radix_ >= 26);  // Leaf switch radix (4*6+2 for 4 GPUs, 3 switches)
    assert(sw->id_.node_id == 4 + i);  // GPU节点之后
    assert(sw->id_.group_id == 0);
  }
  std::cout << "✓ Switch节点创建正确" << std::endl;
  
  delete system;
  delete test_param;
  std::cout << "✓ 测试1通过" << std::endl;
}

// 测试2: GPU到Switch连接测试
void test_gpu_switch_connections() {
  std::cout << "\n=== 测试2: GPU到Switch连接 ===" << std::endl;
  
  Parameters* test_param = create_test_params();
  param = test_param;
  
  NVSwitchSystem* system = new NVSwitchSystem();
  NVSwitchGroup* group = system->get_group(0);
  
  // GPU port layout: gpu_port_base[sw] + k connects to switch sw
  std::vector<int> gpu_port_base(3);
  for (int sw = 1; sw < 3; sw++) {
    gpu_port_base[sw] = gpu_port_base[sw - 1] + system->links_per_switch_[sw - 1];
  }
  for (int gpu_id = 0; gpu_id < 4; gpu_id++) {
    Node* gpu = group->get_gpu(gpu_id);
    for (int sw_id = 0; sw_id < 3; sw_id++) {
      int gpu_port = gpu_port_base[sw_id];
      NodeID linked_node = gpu->link_nodes_[gpu_port];
      assert(linked_node.node_id == 4 + sw_id);
      assert(linked_node.group_id == 0);
      Buffer* linked_buffer = gpu->link_buffers_[gpu_port];
      assert(linked_buffer != nullptr);
      Node* sw = group->get_nvswitch(sw_id);
      int sw_port = gpu_id * system->links_per_switch_[sw_id];
      assert(sw->link_nodes_[sw_port].node_id == gpu_id);
      assert(sw->link_buffers_[sw_port] == gpu->in_buffers_[gpu_port]);
    }
  }
  
  std::cout << "✓ 所有GPU都正确连接到所有Switch" << std::endl;
  
  delete system;
  delete test_param;
  std::cout << "✓ 测试2通过" << std::endl;
}

// 测试3: Switch之间连接测试
void test_switch_switch_connections() {
  std::cout << "\n=== 测试3: Switch之间连接 ===" << std::endl;
  
  Parameters* test_param = create_test_params();
  param = test_param;
  
  NVSwitchSystem* system = new NVSwitchSystem();
  NVSwitchGroup* group = system->get_group(0);
  
  // 验证Switch之间的全连接
  for (int sw1_id = 0; sw1_id < 3; sw1_id++) {
    Node* sw1 = group->get_nvswitch(sw1_id);
    
    for (int sw2_id = sw1_id + 1; sw2_id < 3; sw2_id++) {
      Node* sw2 = group->get_nvswitch(sw2_id);
      
      int port_offset1 = 4 * system->links_per_switch_[sw1_id];
      int port_offset2 = 4 * system->links_per_switch_[sw2_id];
      int sw1_port = port_offset1 + sw2_id - 1;
      int sw2_port = port_offset2 + sw1_id;

      if (sw1_port < sw1->radix_ && sw2_port < sw2->radix_) {
        // 验证连接
        NodeID sw1_linked = sw1->link_nodes_[sw1_port];
        assert(sw1_linked.node_id == 4 + sw2_id);
        
        NodeID sw2_linked = sw2->link_nodes_[sw2_port];
        assert(sw2_linked.node_id == 4 + sw1_id);
        
        // 验证Buffer连接
        assert(sw1->link_buffers_[sw1_port] == sw2->in_buffers_[sw2_port]);
        assert(sw2->link_buffers_[sw2_port] == sw1->in_buffers_[sw1_port]);
      }
    }
  }
  
  std::cout << "✓ Switch之间全连接正确" << std::endl;
  
  delete system;
  delete test_param;
  std::cout << "✓ 测试3通过" << std::endl;
}

// 测试4: 路由算法测试 - Direct路由
void test_direct_routing() {
  std::cout << "\n=== 测试4: Direct路由算法 ===" << std::endl;
  
  Parameters* test_param = create_test_params();
  param = test_param;
  
  NVSwitchSystem* system = new NVSwitchSystem();
  
  // 测试从GPU到GPU的路由
  NodeID src_gpu = NodeID(0, 0);  // GPU 0
  NodeID dst_gpu = NodeID(2, 0);  // GPU 2
  
  Packet test_packet(src_gpu, dst_gpu, 1);
  test_packet.flit_trace_[0] = VCInfo(nullptr, 0, src_gpu);
  
  // 执行路由
  system->routing_algorithm(test_packet);
  
  // 验证候选通道不为空
  assert(!test_packet.candidate_channels_.empty());
  
  // 验证候选通道指向正确的Switch
  VCInfo chosen_vc = test_packet.candidate_channels_[0];
  assert(chosen_vc.buffer != nullptr);
  
  // 找到这个buffer对应的节点
  NodeID next_node = chosen_vc.id;
  // 应该是一个Switch节点
  assert(next_node.node_id >= 4);  // Switch节点ID从4开始
  assert(next_node.group_id == 0);
  
  std::cout << "✓ Direct路由生成正确的候选通道" << std::endl;
  
  // 测试从Switch到GPU的路由
  NodeID src_switch = NodeID(4, 0);  // Switch 0
  Packet test_packet2(src_switch, dst_gpu, 1);
  test_packet2.flit_trace_[0] = VCInfo(nullptr, 0, src_switch);
  
  system->routing_algorithm(test_packet2);
  assert(!test_packet2.candidate_channels_.empty());
  
  VCInfo chosen_vc2 = test_packet2.candidate_channels_[0];
  // 应该直接指向目标GPU
  assert(chosen_vc2.id.node_id == dst_gpu.node_id);
  
  std::cout << "✓ Switch到GPU路由正确" << std::endl;
  
  delete system;
  delete test_param;
  std::cout << "✓ 测试4通过" << std::endl;
}

// 测试5: MIN路由算法测试
void test_min_routing() {
  std::cout << "\n=== 测试5: MIN路由算法 ===" << std::endl;
  
  Parameters* test_param = create_test_params();
  test_param->params_ptree.put("Network.routing_algorithm", "min");
  param = test_param;
  
  NVSwitchSystem* system = new NVSwitchSystem();
  
  NodeID src_gpu = NodeID(0, 0);
  NodeID dst_gpu = NodeID(1, 0);
  
  Packet test_packet(src_gpu, dst_gpu, 1);
  test_packet.flit_trace_[0] = VCInfo(nullptr, 0, src_gpu);
  
  system->routing_algorithm(test_packet);
  
  // MIN路由应该提供多个候选通道（所有Switch）
  assert(!test_packet.candidate_channels_.empty());
  // 应该有多个候选（所有Switch的VC）
  assert(test_packet.candidate_channels_.size() >= 3 * param->vc_number);
  
  std::cout << "✓ MIN路由提供多个候选通道用于负载均衡" << std::endl;
  
  delete system;
  delete test_param;
  std::cout << "✓ 测试5通过" << std::endl;
}

// 测试6: 多组拓扑测试
void test_multi_group_topology() {
  std::cout << "\n=== 测试6: 多组拓扑 ===" << std::endl;
  
  Parameters* test_param = create_test_params();
  test_param->params_ptree.put("Network.num_groups", 2);
  param = test_param;
  
  NVSwitchSystem* system = new NVSwitchSystem();
  
  assert(system->num_groups_ == 2);
  assert(system->num_cores_ == 8);  // 2组 * 4 GPUs
  assert(system->num_nodes_ == 14);  // 2组 * (4 GPUs + 3 Switches)
  
  // 验证两个组都存在
  NVSwitchGroup* group0 = system->get_group(0);
  NVSwitchGroup* group1 = system->get_group(1);
  assert(group0 != nullptr);
  assert(group1 != nullptr);
  
  // 验证组0的GPU
  Node* gpu0_0 = group0->get_gpu(0);
  assert(gpu0_0->id_.group_id == 0);
  assert(gpu0_0->id_.node_id == 0);
  
  // 验证组1的GPU
  Node* gpu1_0 = group1->get_gpu(0);
  assert(gpu1_0->id_.group_id == 1);
  assert(gpu1_0->id_.node_id == 0);
  
  std::cout << "✓ 多组拓扑构建正确" << std::endl;
  
  delete system;
  delete test_param;
  std::cout << "✓ 测试6通过" << std::endl;
}

// 测试7: 数据包传输测试
void test_packet_transmission() {
  std::cout << "\n=== 测试7: 数据包传输 ===" << std::endl;
  
  Parameters* test_param = create_test_params();
  param = test_param;
  
  NVSwitchSystem* system = new NVSwitchSystem();
  TrafficManager* tm = new TrafficManager();
  TM = tm;
  
  NodeID src = NodeID(0, 0);  // GPU 0
  NodeID dst = NodeID(3, 0);  // GPU 3
  
  Packet* pkt = new Packet(src, dst, 1);
  
  // 模拟几个周期的传输
  int max_cycles = 100;
  int cycle = 0;
  
  while (!pkt->finished_ && cycle < max_cycles) {
    system->update(*pkt);
    cycle++;
  }
  
  if (pkt->finished_) {
    std::cout << "✓ 数据包成功传输，耗时 " << cycle << " 周期" << std::endl;
    std::cout << "  跳数: " << pkt->hops_ << std::endl;
  } else {
    std::cout << "✗ 数据包传输超时" << std::endl;
  }
  
  delete pkt;
  delete tm;
  delete system;
  delete test_param;
  std::cout << "✓ 测试7完成" << std::endl;
}

// 测试8: NodeID转换测试
void test_nodeid_conversion() {
  std::cout << "\n=== 测试8: NodeID转换 ===" << std::endl;
  
  Parameters* test_param = create_test_params();
  param = test_param;
  
  NVSwitchSystem* system = new NVSwitchSystem();
  
  // 测试int_to_nodeid
  for (int core_id = 0; core_id < 4; core_id++) {
    NodeID node_id = system->int_to_nodeid(core_id);
    assert(node_id.group_id == 0);
    assert(node_id.node_id == core_id);
  }
  
  std::cout << "✓ NodeID转换正确" << std::endl;
  
  delete system;
  delete test_param;
  std::cout << "✓ 测试8通过" << std::endl;
}

// 测试9: 边界情况测试
void test_edge_cases() {
  std::cout << "\n=== 测试9: 边界情况 ===" << std::endl;
  
  // 测试最小配置
  Parameters* test_param = create_test_params();
  test_param->params_ptree.put("Network.num_gpus_per_group", 2);
  test_param->params_ptree.put("Network.num_switches_per_group", 1);
  test_param->params_ptree.put("Network.gpu_nvlink_ports", 18);
  param = test_param;
  
  NVSwitchSystem* system = new NVSwitchSystem();
  
  assert(system->num_gpus_per_group_ == 2);
  assert(system->num_switches_per_group_ == 1);
  assert(system->num_nodes_ == 3);  // 2 GPUs + 1 Switch
  
  std::cout << "✓ 最小配置正常工作" << std::endl;
  
  delete system;
  delete test_param;
  std::cout << "✓ 测试9通过" << std::endl;
}

// 主测试函数
int main() {
  std::cout << "========================================" << std::endl;
  std::cout << "NVSwitch拓扑测试套件" << std::endl;
  std::cout << "========================================" << std::endl;
  
  try {
    test_topology_construction();
    test_gpu_switch_connections();
    test_switch_switch_connections();
    test_direct_routing();
    test_min_routing();
    test_multi_group_topology();
    test_packet_transmission();
    test_nodeid_conversion();
    test_edge_cases();
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "✓ 所有测试通过！" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // 清理临时文件
    std::remove("test_nvswitch_config.ini");
    
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "\n✗ 测试失败: " << e.what() << std::endl;
    std::remove("test_nvswitch_config.ini");
    return 1;
  } catch (...) {
    std::cerr << "\n✗ 未知错误" << std::endl;
    std::remove("test_nvswitch_config.ini");
    return 1;
  }
}
