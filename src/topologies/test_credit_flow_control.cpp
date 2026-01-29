/**
 * 基于 Credit 的流控测试程序
 * 验证：flow_control=credit 时多包能正常完成、无死锁、credit 回报正确（drain 后恢复初始值）
 *
 * 用法: test_credit_flow_control [config.ini]
 *       不传参数时使用 input/nvswitch_test.ini（需保证其中 flow_control=credit）
 */

#include "config.h"
#include "nvswitch.h"
#include "packet.h"
#include "traffic_manager.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

Parameters* param = nullptr;
TrafficManager* TM = nullptr;
System* network = nullptr;
boost::mt19937 gen;

// 单线程单周期：先释放链路并删除已到达包，再 update 所有未完成包
static void run_one_cycle_single(std::vector<Packet*>& vec_pkts, System* system) {
  uint64_t j = 0;
  uint64_t vecsize = vec_pkts.size();
  for (uint64_t i = 0; i < vecsize; ++i) {
    Packet*& pkt = vec_pkts[i];
    if (pkt->releaselink_) {
      pkt->tail_trace().buffer->release_in_link(*pkt);
      if (pkt->leaving_vc_.buffer != nullptr)
        pkt->leaving_vc_.buffer->release_sw_link();
      else
        assert(pkt->leaving_vc_.id == pkt->source_);
      pkt->releaselink_ = false;
    }
    if (pkt->finished_) {
      delete pkt;
    } else {
      vec_pkts[j] = pkt;
      j++;
    }
  }
  vec_pkts.resize(j);
  for (size_t i = 0; i < vec_pkts.size(); i++)
    system->update(*vec_pkts[i]);
}

// 检查所有节点的 credit 是否恢复为初始值（每个 port/vcb = buffer_size）
static bool credits_restored(NVSwitchSystem* system, int buffer_size) {
  for (int gid = 0; gid < system->num_groups_; gid++) {
    NVSwitchGroup* group = system->get_group(gid);
    for (int i = 0; i < group->num_nodes_; i++) {
      Node* node = group->get_node(i);
      if (node->get_credit(0, 0) < 0) continue;  // 未启用 credit 的节点跳过
      for (int port = 0; port < node->radix_; port++) {
        for (int vcb = 0; vcb < node->vc_num_; vcb++) {
          int c = node->get_credit(port, vcb);
          if (c != buffer_size) return false;
        }
      }
    }
  }
  return true;
}

static void test_credit_flow_control(const std::string& config_path) {
  std::cout << "使用配置: " << config_path << std::endl;

  param = new Parameters(config_path);
  if (param->topology != "NVSwitch") {
    std::cerr << "错误: 本测试需要 topology=NVSwitch，当前为 " << param->topology << std::endl;
    std::exit(1);
  }
  if (param->flow_control != "credit") {
    std::cerr << "警告: 配置中 flow_control 不是 credit，将强制设为 credit 进行测试" << std::endl;
    param->flow_control = "credit";
  }

  NVSwitchSystem* system = new NVSwitchSystem();
  network = system;
  system->init_flow_control();

  param->output_file = "test_credit_output.csv";
  param->log_file = "test_credit_log.txt";
  TM = new TrafficManager();

  const int num_cores = system->num_cores_;
  const int buf_size = param->buffer_size;
  const int pkt_len = param->packet_length;
  const int num_packets = 40;
  const uint64_t max_cycles = 10000;

  std::cout << "\n--- 1. 初始 credit 检查 ---" << std::endl;
  assert(credits_restored(system, buf_size));
  std::cout << "  ✓ 初始时各 (port,vcb) credit = buffer_size=" << buf_size << std::endl;

  std::cout << "\n--- 2. 注入 " << num_packets << " 个包并跑至完成 ---" << std::endl;
  TM->traffic_scale_ = num_cores;
  TM->message_length_ = pkt_len;
  TM->traffic_ = "uniform";
  gen.seed(42);

  std::vector<Packet*> packets;
  for (int k = 0; k < num_packets; k++) {
    int src, dest;
    do {
      src = gen() % num_cores;
      dest = gen() % num_cores;
    } while (dest == src);
    packets.push_back(new Packet(network->int_to_nodeid(src), network->int_to_nodeid(dest), pkt_len));
  }

  uint64_t cycle = 0;
  while (!packets.empty() && cycle < max_cycles) {
    run_one_cycle_single(packets, system);
    cycle++;
  }

  std::cout << "  完成周期: " << cycle << "  剩余包数: " << packets.size() << std::endl;
  std::cout << "  到达: " << TM->message_arrived_.load() << "  超时: " << TM->message_timeout_.load() << std::endl;

  assert(packets.empty() && "所有包应在 max_cycles 内完成");
  assert(TM->message_timeout_.load() == 0 && "不应有超时");
  assert(TM->message_arrived_.load() == (uint64_t)num_packets && "到达数应等于注入数");
  std::cout << "  ✓ 所有包在 " << cycle << " 周期内完成，无超时" << std::endl;

  std::cout << "\n--- 3. Drain 后 credit 恢复检查 ---" << std::endl;
  assert(credits_restored(system, buf_size));
  std::cout << "  ✓ 各节点 (port,vcb) credit 已恢复为 buffer_size，无泄漏" << std::endl;

  std::cout << "\n========================================" << std::endl;
  std::cout << "基于 Credit 的流控测试全部通过" << std::endl;
  std::cout << "========================================\n" << std::endl;

  delete TM;
  TM = nullptr;
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
    test_credit_flow_control(config_path);
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "错误: " << e.what() << std::endl;
    return 1;
  }
}
