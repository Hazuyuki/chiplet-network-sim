/**
 * GPU 18 口使用实验：统计到达每个 GPU 的包是从该 GPU 的哪个入端口(0..17)进入的
 * 若包泼洒正确，流量应从 4 个 Leaf 经 18 条链路进 GPU，18 个 port 应较均匀使用（各约 1/18）
 */
#include "config.h"
#include "nvswitch.h"
#include "packet.h"
#include "traffic_manager.h"
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
output_file = ../output/gpu18port.csv
log_file = ../output/gpu18port.log
)";

  std::filesystem::path ini_path = out_dir / "test_gpu_18port_usage.ini";
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
  const int num_gpus_per_group = system->num_gpus_per_group_;
  const int num_gpus = network->num_cores_;
  const int gpu_radix = 18;

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

  const auto& usage = network->get_diag_gpu_port_usage();
  delete TM;
  TM = nullptr;
  network->enable_diagnostics(false);

  // 全网按 port 聚合：port 0..17 各收到多少包
  std::vector<uint64_t> per_port(gpu_radix, 0);
  uint64_t total = 0;
  for (const auto& kv : usage) {
    int port = kv.first.second;
    if (port >= 0 && port < gpu_radix) {
      per_port[port] += kv.second;
      total += kv.second;
    }
  }

  std::cout << "============================================" << std::endl;
  std::cout << "GPU 18 口使用实验 (64 卡 ring_all_reduce, inj=" << inj_rate << ")" << std::endl;
  std::cout << "============================================" << std::endl;
  std::cout << "统计: 到达 GPU 的包从该 GPU 的哪个入端口(0..17)进入。若 18 口用满，各约 1/18。" << std::endl;
  std::cout << "Warmup: " << warmup << ", Measure: " << measure << " cycles" << std::endl;
  std::cout << "总到达包数(记录到端口的): " << total << std::endl;
  std::cout << std::endl;

  std::cout << "--- 全网：各 GPU 入端口(0..17) 收到的包数及占比 ---" << std::endl;
  std::cout << "| Port | 包数    | 占比(%) | 期望(1/18) |" << std::endl;
  std::cout << "|------|--------|--------|------------|" << std::endl;
  const double expect_pct = 100.0 / gpu_radix;
  int ports_used = 0;
  double min_pct = 100, max_pct = 0;
  for (int p = 0; p < gpu_radix; p++) {
    uint64_t c = per_port[p];
    if (c > 0) ports_used++;
    double pct = (total > 0) ? (100.0 * c / total) : 0;
    if (pct < min_pct) min_pct = pct;
    if (pct > max_pct) max_pct = pct;
    printf("| %4d | %6lu | %6.2f | %6.2f     |\n", p, c, pct, expect_pct);
  }
  std::cout << std::endl;

  std::cout << "--- 结论 ---" << std::endl;
  std::cout << "有流量的端口数: " << ports_used << " / " << gpu_radix << std::endl;
  std::cout << "占比范围: " << std::fixed << std::setprecision(2) << min_pct << "% ~ " << max_pct << "% (理想均匀 " << expect_pct << "%)" << std::endl;
  if (ports_used >= 16 && min_pct >= expect_pct * 0.5 && max_pct <= expect_pct * 1.5) {
    std::cout << "18 口均有使用且分布较均匀，GPU 侧 18 口已用满。" << std::endl;
  } else if (ports_used < gpu_radix) {
    std::cout << "仅 " << ports_used << " 个端口有流量，18 口未完全用满。" << std::endl;
  } else {
    std::cout << "18 口均有流量，但分布不均（可能受路由/调度影响）。" << std::endl;
  }

  std::filesystem::path csv_path = out_dir / "gpu_18port_usage.csv";
  std::ofstream csv(csv_path);
  csv << "port,packet_count,pct\n";
  for (int p = 0; p < gpu_radix; p++)
    csv << p << "," << per_port[p] << "," << (total > 0 ? 100.0 * per_port[p] / total : 0) << "\n";
  csv.close();
  std::cout << "结果已写入: " << csv_path << std::endl;

  delete system;
  delete param;
  return 0;
}
