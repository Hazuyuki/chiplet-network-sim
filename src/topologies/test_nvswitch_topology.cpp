/**
 * NVSwitch 拓扑测试程序
 * 验证：节点数、GPU-Switch 连接、Switch-Switch 连接、upstream/credit 初始化、路由与可达性
 *
 * 用法: test_nvswitch_topology [config.ini]
 *       不传参数时使用 input/nvswitch_test.ini
 */

#include "config.h"
#include "nvswitch.h"
#include "packet.h"
#include "traffic_manager.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>

Parameters* param = nullptr;
TrafficManager* TM = nullptr;
System* network = nullptr;
boost::mt19937 gen;

static void test_topology(const std::string& config_path) {
  std::cout << "使用配置: " << config_path << std::endl;

  param = new Parameters(config_path);
  if (param->topology != "NVSwitch") {
    std::cerr << "错误: 配置中 topology 应为 NVSwitch，当前为 " << param->topology << std::endl;
    std::exit(1);
  }

  NVSwitchSystem* system = new NVSwitchSystem();
  network = system;

  // 初始化流控（设置 upstream、credit）
  system->init_flow_control();

  const int num_gpus = system->num_gpus_per_server_;
  const int num_switches = system->num_switches_per_server_;
  const int num_servers = system->num_servers_per_super_node_;

  std::cout << "\n--- 1. 基本参数 ---" << std::endl;
  std::cout << "  servers=" << num_servers
            << " GPUs/server=" << num_gpus
            << " switches/server=" << num_switches
            << " num_nodes=" << system->num_nodes_
            << " num_cores=" << system->num_cores_ << std::endl;

  assert(system->num_nodes_ == num_servers * (num_gpus + num_switches));
  assert(system->num_cores_ == num_servers * num_gpus);
  std::cout << "  ✓ 节点数/核心数正确" << std::endl;

  for (int sid = 0; sid < num_servers; sid++) {
    NVSwitchGroup* group = system->get_group(sid);
    assert(group != nullptr);
    assert(group->num_gpus_ == num_gpus);
    assert(group->num_switches_ == num_switches);

    std::cout << "\n--- 2. Server " << sid << " GPU↔Switch 连接 ---" << std::endl;
    std::vector<int> gpu_port_base(num_switches);
    for (int sw = 1; sw < num_switches; sw++) {
      gpu_port_base[sw] = gpu_port_base[sw - 1] + system->links_per_switch_[sw - 1];
    }
    for (int gpu_id = 0; gpu_id < num_gpus; gpu_id++) {
      Node* gpu = group->get_gpu(gpu_id);
      assert(gpu->radix_ == system->gpu_nvlink_ports_);
      assert(gpu->id_.node_id == gpu_id);
      assert(gpu->id_.group_id == sid);

      for (int sw_id = 0; sw_id < num_switches; sw_id++) {
        int gpu_port = gpu_port_base[sw_id];
        NodeID linked = gpu->link_nodes_[gpu_port];
        assert(linked.node_id == num_gpus + sw_id);
        assert(linked.group_id == sid);

        Node* sw = group->get_nvswitch(sw_id);
        int n_links = system->links_per_switch_[sw_id];
        int sw_port = gpu_id * n_links;
        assert(sw->link_nodes_[sw_port].node_id == gpu_id);
        assert(sw->link_nodes_[sw_port].group_id == sid);
        assert(gpu->link_buffers_[gpu_port] == sw->in_buffers_[sw_port]);
        assert(sw->link_buffers_[sw_port] == gpu->in_buffers_[gpu_port]);
      }
    }
    std::cout << "  ✓ 所有 GPU 与 Switch 双向连接正确 (gpu_nvlink_ports=" << system->gpu_nvlink_ports_ << ")" << std::endl;

    if (!system->switches_fully_connected_ || num_switches < 2) {
      std::cout << "\n--- 3. Server " << sid << " Switch↔Switch 连接 --- 跳过(未启用或仅1个Switch)" << std::endl;
      continue;
    }

    std::cout << "\n--- 3. Server " << sid << " Switch↔Switch 连接 ---" << std::endl;
    for (int sw1_id = 0; sw1_id < num_switches; sw1_id++) {
      Node* sw1 = group->get_nvswitch(sw1_id);
      int sw1_port_offset = num_gpus * system->links_per_switch_[sw1_id];
      for (int sw2_id = sw1_id + 1; sw2_id < num_switches; sw2_id++) {
        Node* sw2 = group->get_nvswitch(sw2_id);
        int sw2_port_offset = num_gpus * system->links_per_switch_[sw2_id];
        int sw1_port = sw1_port_offset + sw2_id - 1;
        int sw2_port = sw2_port_offset + sw1_id;

        if (sw1_port >= sw1->radix_ || sw2_port >= sw2->radix_) {
          std::cout << "  警告: leaf_switch_radix 过小，部分 Switch-Switch 未连接" << std::endl;
          continue;
        }
        assert(sw1->link_nodes_[sw1_port].node_id == num_gpus + sw2_id);
        assert(sw2->link_nodes_[sw2_port].node_id == num_gpus + sw1_id);
        assert(sw1->link_buffers_[sw1_port] == sw2->in_buffers_[sw2_port]);
        assert(sw2->link_buffers_[sw2_port] == sw1->in_buffers_[sw1_port]);
      }
    }
    std::cout << "  ✓ Switch 间全连接正确" << std::endl;
  }

  std::cout << "\n--- 4. 流控初始化 (upstream) ---" << std::endl;

  for (int sid = 0; sid < num_servers; sid++) {
    NVSwitchGroup* group = system->get_group(sid);
    for (int i = 0; i < group->num_nodes_; i++) {
      Node* node = group->get_node(i);
      for (int port = 0; port < node->radix_; port++) {
        if (node->link_buffers_[port] != nullptr) {
          Buffer* buf = node->link_buffers_[port];
          assert(buf->upstream_node_ != nullptr && buf->upstream_port_ >= 0);
          assert(buf->upstream_node_ == node);
          assert(buf->upstream_port_ == port);
        }
      }
    }
  }
  std::cout << "  ✓ 所有下游 buffer 的 upstream 已正确设置" << std::endl;

  std::cout << "\n--- 5. 路由与可达性 ---" << std::endl;
  NodeID src = NodeID(0, 0);
  NodeID dst = NodeID(num_gpus - 1, 0);
  Packet pkt(src, dst, 1);
  pkt.flit_trace_[0] = VCInfo(nullptr, 0, src);

  system->routing_algorithm(pkt);
  assert(!pkt.candidate_channels_.empty());
  std::cout << "  ✓ GPU0→GPU" << (num_gpus - 1) << " 路由得到候选通道" << std::endl;

  // 短时传输测试：单包从 src 到 dst（输出到当前目录，避免 ../../output 权限问题）
  param->output_file = "test_nvswitch_output.csv";
  param->log_file = "test_nvswitch_log.txt";
  TM = new TrafficManager();
  int cycle = 0;
  const int max_cycles = 500;
  while (!pkt.finished_ && cycle < max_cycles) {
    system->update(pkt);
    cycle++;
  }
  delete TM;
  TM = nullptr;

  if (pkt.finished_) {
    std::cout << "  ✓ 单包传输成功，周期=" << cycle << " hops=" << pkt.hops_ << std::endl;
  } else {
    std::cout << "  ✗ 单包在 " << max_cycles << " 周期内未到达" << std::endl;
    assert(false && "packet did not finish");
  }

  std::cout << "\n========================================" << std::endl;
  std::cout << "NVSwitch 拓扑测试全部通过" << std::endl;
  std::cout << "========================================\n" << std::endl;

  delete system;
  network = nullptr;
  delete param;
  param = nullptr;
}

int main(int argc, char* argv[]) {
  std::string config_path = "input/nvswitch_test.ini";
  if (argc > 1)
    config_path = argv[1];

  try {
    test_topology(config_path);
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "错误: " << e.what() << std::endl;
    return 1;
  }
}
