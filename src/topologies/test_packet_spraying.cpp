/**
 * 包泼洒 (Packet Spraying) 测试程序
 * 对比 min_routing (包泼洒) vs direct_routing 的完成时间
 *
 * 用法: test_packet_spraying [config.ini] [num_packets]
 *       不传参数时使用 input/nvswitch_test.ini，100 个包
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

static void run_one_cycle_single(std::vector<Packet*>& vec_pkts, System* system, uint64_t cyc) {
  uint64_t j = 0;
  uint64_t vecsize = vec_pkts.size();
  param->current_simulation_cycle = cyc;
  network->process_pending_credits(cyc);

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

// 生成包列表（使用固定种子保证两次调用产生相同流量）
static std::vector<std::pair<int, int>> generate_traffic(int num_packets, int num_cores, unsigned seed) {
  boost::mt19937 rng(seed);
  std::vector<std::pair<int, int>> traffic;
  traffic.reserve(num_packets);
  for (int k = 0; k < num_packets; k++) {
    int src, dest;
    do {
      src = rng() % num_cores;
      dest = rng() % num_cores;
    } while (dest == src);
    traffic.push_back({src, dest});
  }
  return traffic;
}

// 运行仿真并返回完成周期数
static uint64_t run_simulation(NVSwitchSystem* system, const std::vector<std::pair<int, int>>& traffic,
                               int pkt_len, uint64_t max_cycles) {
  TM = new TrafficManager();
  TM->traffic_ = "uniform";
  TM->traffic_scale_ = system->num_cores_;
  TM->message_length_ = pkt_len;

  std::vector<Packet*> packets;
  for (auto& [src, dest] : traffic) {
    packets.push_back(new Packet(network->int_to_nodeid(src), network->int_to_nodeid(dest), pkt_len));
  }
  TM->all_message_num_.store(static_cast<int>(traffic.size()));

  uint64_t cycle = 0;
  while (!packets.empty() && cycle < max_cycles) {
    run_one_cycle_single(packets, system, cycle);
    cycle++;
  }

  uint64_t arrived = TM->message_arrived_.load();
  uint64_t timeout = TM->message_timeout_.load();
  
  // 清理未完成的包
  for (auto* p : packets) delete p;
  
  delete TM;
  TM = nullptr;

  if (arrived != traffic.size() || timeout > 0) {
    std::cerr << "  警告: 未全部完成! 到达=" << arrived << " 超时=" << timeout << std::endl;
  }
  return cycle;
}

static void compare_routing(const std::string& config_path, int num_packets) {
  const uint64_t max_cycles = 100000;
  const unsigned seed = 12345;

  std::cout << "========================================" << std::endl;
  std::cout << "Min Routing (包泼洒) vs Direct Routing 对比" << std::endl;
  std::cout << "配置: " << config_path << std::endl;
  std::cout << "包数: " << num_packets << std::endl;
  std::cout << "========================================\n" << std::endl;

  // --- Direct Routing ---
  std::cout << "--- Direct Routing ---" << std::endl;
  param = new Parameters(config_path);
  param->flow_control = "credit";
  param->params_ptree.put("Network.routing_algorithm", "direct");
  param->output_file = "test_direct_output.csv";
  param->log_file = "test_direct.log";

  NVSwitchSystem* sys_direct = new NVSwitchSystem();
  network = sys_direct;
  sys_direct->init_flow_control();

  auto traffic = generate_traffic(num_packets, sys_direct->num_cores_, seed);
  uint64_t cycle_direct = run_simulation(sys_direct, traffic, param->packet_length, max_cycles);
  std::cout << "  完成周期: " << cycle_direct << std::endl;

  delete sys_direct;
  delete param;

  // --- Min Routing (包泼洒) ---
  std::cout << "\n--- Min Routing (包泼洒) ---" << std::endl;
  param = new Parameters(config_path);
  param->flow_control = "credit";
  param->params_ptree.put("Network.routing_algorithm", "min");
  param->output_file = "test_min_output.csv";
  param->log_file = "test_min.log";

  NVSwitchSystem* sys_min = new NVSwitchSystem();
  network = sys_min;
  sys_min->init_flow_control();

  uint64_t cycle_min = run_simulation(sys_min, traffic, param->packet_length, max_cycles);
  std::cout << "  完成周期: " << cycle_min << std::endl;

  delete sys_min;
  delete param;
  param = nullptr;
  network = nullptr;

  // --- 结果对比 ---
  std::cout << "\n========================================" << std::endl;
  std::cout << "结果对比 (" << num_packets << " 个包)" << std::endl;
  std::cout << "========================================" << std::endl;
  std::cout << "  Direct Routing: " << cycle_direct << " 周期" << std::endl;
  std::cout << "  Min Routing:    " << cycle_min << " 周期" << std::endl;

  if (cycle_min < cycle_direct) {
    double speedup = (double)cycle_direct / cycle_min;
    double reduction = 100.0 * (cycle_direct - cycle_min) / cycle_direct;
    std::cout << "  加速比: " << speedup << "x (减少 " << reduction << "%)" << std::endl;
  } else if (cycle_min > cycle_direct) {
    double slowdown = (double)cycle_min / cycle_direct;
    std::cout << "  Min 比 Direct 慢 " << slowdown << "x" << std::endl;
  } else {
    std::cout << "  两者相同" << std::endl;
  }
  std::cout << "========================================\n" << std::endl;
}

// 多包量对比测试
static void sweep_packet_counts(const std::string& config_path) {
  std::vector<int> counts = {50, 100, 200, 500, 1000, 2000};
  const uint64_t max_cycles = 200000;
  const unsigned seed = 12345;

  std::cout << "\n========================================" << std::endl;
  std::cout << "不同包量下的完成时间对比" << std::endl;
  std::cout << "========================================\n" << std::endl;

  std::cout << "| 包数 | Direct | Min | 加速比 |" << std::endl;
  std::cout << "|------|--------|-----|--------|" << std::endl;

  for (int num_packets : counts) {
    // Direct
    param = new Parameters(config_path);
    param->flow_control = "credit";
    param->params_ptree.put("Network.routing_algorithm", "direct");
    param->output_file = "test_direct_output.csv";
    param->log_file = "test_direct.log";

    NVSwitchSystem* sys_direct = new NVSwitchSystem();
    network = sys_direct;
    sys_direct->init_flow_control();

    auto traffic = generate_traffic(num_packets, sys_direct->num_cores_, seed);
    uint64_t cycle_direct = run_simulation(sys_direct, traffic, param->packet_length, max_cycles);

    delete sys_direct;
    delete param;

    // Min
    param = new Parameters(config_path);
    param->flow_control = "credit";
    param->params_ptree.put("Network.routing_algorithm", "min");
    param->output_file = "test_min_output.csv";
    param->log_file = "test_min.log";

    NVSwitchSystem* sys_min = new NVSwitchSystem();
    network = sys_min;
    sys_min->init_flow_control();

    uint64_t cycle_min = run_simulation(sys_min, traffic, param->packet_length, max_cycles);

    delete sys_min;
    delete param;
    param = nullptr;
    network = nullptr;

    double speedup = (cycle_min > 0) ? (double)cycle_direct / cycle_min : 0;
    std::cout << "| " << num_packets << " | " << cycle_direct << " | " << cycle_min
              << " | " << speedup << "x |" << std::endl;
  }
  std::cout << std::endl;
}

int main(int argc, char* argv[]) {
  std::string config_path = "input/nvswitch_test.ini";
  int num_packets = 100;
  
  if (argc > 1) config_path = argv[1];
  if (argc > 2) num_packets = std::atoi(argv[2]);

  try {
    compare_routing(config_path, num_packets);
    sweep_packet_counts(config_path);
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "错误: " << e.what() << std::endl;
    return 1;
  }
}
