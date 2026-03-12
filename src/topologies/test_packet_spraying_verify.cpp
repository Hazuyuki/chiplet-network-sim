/**
 * 包泼洒验证实验：统计到达每个 GPU 的包来自哪个 Leaf（ingress）
 * 若包泼洒正确应用，流量应从 4 个 Leaf 较均匀地汇聚到同一 GPU（各约 25%）
 * 若某 Leaf 占比远高于 25%，说明多数包仍经单 Leaf 进 GPU，泼洒未生效
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
output_file = ../output/spray_verify.csv
log_file = ../output/spray_verify.log
)";

  std::filesystem::path ini_path = out_dir / "test_packet_spraying_verify.ini";
  std::ofstream ofs(ini_path);
  ofs << test_config;
  ofs.close();

  param = new Parameters(ini_path.string());
  param->flow_control = "credit";

  gen.seed(42);
  NVSwitchSystem* system = new NVSwitchSystem();
  network = system;
  system->init_flow_control();

  const int warmup = 500;
  const int measure = 1000;
  const double inj_rate = 5.0;
  const int num_gpus_per_group = system->num_gpus_per_server_;
  const int num_switches_per_group = system->num_switches_per_server_;
  const int num_gpus = network->num_cores_;

  param->traffic = "ring_all_reduce";
  param->traffic_scale = num_gpus;
  system->reset();
  network->reset_diagnostics();
  network->enable_diagnostics(true);

  TM = new TrafficManager();
  TM->traffic_ = "ring_all_reduce";
  TM->traffic_scale_ = param->traffic_scale;
  TM->injection_rate_ = inj_rate;

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

  // 按目的 GPU 聚合：每个 GPU 从 Leaf 0,1,2,3 各收到多少
  std::vector<std::vector<uint64_t>> per_dest(num_gpus, std::vector<uint64_t>(num_switches_per_group, 0));
  uint64_t total_recorded = 0;
  for (const auto& kv : counts) {
    int dest_global = kv.first.first;
    int ingress_sw = kv.first.second;
    uint64_t c = kv.second;
    if (dest_global >= 0 && dest_global < num_gpus && ingress_sw >= 0 && ingress_sw < num_switches_per_group) {
      per_dest[dest_global][ingress_sw] += c;
      total_recorded += c;
    }
  }

  std::cout << "============================================" << std::endl;
  std::cout << "包泼洒验证 (64 卡 ring_all_reduce, inj=" << inj_rate << ")" << std::endl;
  std::cout << "============================================" << std::endl;
  std::cout << "统计: 到达每个 GPU 的包来自哪个 Leaf（ingress）。若泼洒正确，4 个 Leaf 应各约 25%。" << std::endl;
  std::cout << "Warmup: " << warmup << ", Measure: " << measure << " cycles" << std::endl;
  std::cout << "总记录包数（到达 GPU 且上游为 Leaf）: " << total_recorded << std::endl;
  std::cout << std::endl;

  // 汇总：全网的 Leaf0/1/2/3 占比
  std::vector<uint64_t> sum_per_leaf(num_switches_per_group, 0);
  for (int g = 0; g < num_gpus; g++)
    for (int sw = 0; sw < num_switches_per_group; sw++)
      sum_per_leaf[sw] += per_dest[g][sw];
  uint64_t sum_all = 0;
  for (uint64_t c : sum_per_leaf) sum_all += c;

  std::cout << "--- 全网：到达 GPU 的包按 ingress Leaf 分布 ---" << std::endl;
  std::cout << "| Ingress Leaf | 包数    | 占比(%) | 期望(均匀) |" << std::endl;
  std::cout << "|--------------|--------|--------|------------|" << std::endl;
  for (int sw = 0; sw < num_switches_per_group; sw++) {
    double pct = (sum_all > 0) ? (100.0 * sum_per_leaf[sw] / sum_all) : 0;
    printf("| Leaf %d         | %6lu | %6.1f | 25.0       |\n", sw, sum_per_leaf[sw], pct);
  }
  std::cout << std::endl;

  // 抽样几个 GPU 看分布
  std::cout << "--- 抽样 GPU 的 ingress 分布（每 GPU 应约 25% 来自各 Leaf）---" << std::endl;
  for (int gpu : {0, 1, 32, 63}) {
    if (gpu >= num_gpus) continue;
    uint64_t tot = 0;
    for (int sw = 0; sw < num_switches_per_group; sw++) tot += per_dest[gpu][sw];
    if (tot == 0) continue;
    std::cout << "  GPU " << gpu << " (group " << (gpu / num_gpus_per_group) << ", local " << (gpu % num_gpus_per_group)
              << "): total=" << tot;
    for (int sw = 0; sw < num_switches_per_group; sw++) {
      double pct = 100.0 * per_dest[gpu][sw] / tot;
      std::cout << "  Leaf" << sw << "=" << std::fixed << std::setprecision(1) << pct << "%";
    }
    std::cout << std::endl;
  }

  std::cout << std::endl << "--- 结论 ---" << std::endl;
  double max_frac = 0, min_frac = 100;
  for (int sw = 0; sw < num_switches_per_group; sw++) {
    double pct = (sum_all > 0) ? (100.0 * sum_per_leaf[sw] / sum_all) : 0;
    if (pct > max_frac) max_frac = pct;
    if (pct < min_frac) min_frac = pct;
  }
  if (max_frac < 35 && min_frac > 15) {
    std::cout << "各 Leaf 占比在 15%~35% 之间，包泼洒较均匀，多 Leaf 汇聚已生效。" << std::endl;
  } else {
    std::cout << "存在 Leaf 占比过高(" << max_frac << "%)或过低(" << min_frac
              << "%)，泼洒可能未充分应用，或流量模式导致不均。" << std::endl;
  }

  std::filesystem::path csv_path = out_dir / "packet_spraying_ingress_by_leaf.csv";
  std::ofstream csv(csv_path);
  csv << "ingress_leaf,packet_count,pct\n";
  for (int sw = 0; sw < num_switches_per_group; sw++) {
    double pct = (sum_all > 0) ? (100.0 * sum_per_leaf[sw] / sum_all) : 0;
    csv << sw << "," << sum_per_leaf[sw] << "," << pct << "\n";
  }
  csv.close();
  std::cout << "结果已写入: " << csv_path << std::endl;

  delete system;
  delete param;
  return 0;
}
