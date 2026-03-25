/**
 * Switch 内部出端口竞争诊断
 * 精确追踪：哪些 Switch 输出端口被多个包同时竞争
 * packet_length=1, 注入率=18, link_aware=true
 */
#include "config.h"
#include "nvswitch.h"
#include "packet.h"
#include "traffic_manager.h"
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

static void run_one_cycle(std::vector<Packet*>& vec_pkts, System* sys, uint64_t cyc) {
  param->current_simulation_cycle = cyc;
  network->process_pending_credits(cyc);
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
    sys->update(*vec_pkts[i]);
}

int main() {
  std::filesystem::path out_dir("../output");
  std::filesystem::create_directories(out_dir);

  std::string config = "[Network]\n"
    "topology = NVSwitch\n"
    "num_gpus_per_server = 8\n"
    "num_switches_per_server = 4\n"
    "num_servers_per_super_node = 8\n"
    "gpu_nvlink_ports = 18\n"
    "switches_fully_connected = true\n"
    "spine_non_blocking = true\n"
    "spine_leaf_links_per_pair = 1\n"
    "routing_algorithm = min\n"
    "buffer_size = 64\n"
    "switch_buffer_size = 2048\n"
    "vc_number = 2\n"
    "flow_control = credit\n"
    "credit_return_delay = 2\n"
    "gpu_switch_link_width = 1\n"
    "switch_switch_link_width = 1\n"
    "vc_alloc_link_aware = true\n"
    "\n[Workload]\n"
    "traffic = collective_ring_all_reduce\n"
    "packet_length = 1\n"
    "traffic_scale = 0\n"
    "\n[Simulation]\n"
    "simulation_time = 2000\n"
    "threads = 1\n"
    "issue_width = 32\n"
    "timeout_threshold = 50000\n"
    "timeout_limit = 500000\n"
    "\n[Files]\n"
    "output_file = ../output/sw_port_diag.csv\n"
    "log_file = ../output/sw_port_diag.log\n";

  std::filesystem::path ini_path = out_dir / "test_sw_port.ini";
  { std::ofstream ofs(ini_path); ofs << config; }

  param = new Parameters(ini_path.string());
  param->flow_control = "credit";
  gen.seed(42);
  NVSwitchSystem* system = new NVSwitchSystem();
  network = system;
  system->init_flow_control();

  int num_gpus = network->num_cores_;
  int gpus_per_server = system->num_gpus_per_server_;
  int num_servers = system->num_servers_per_super_node_;
  int num_leaves = system->num_switches_per_server_;
  int vc_num = param->vc_number;

  std::vector<Node*> gpu_nodes, sw_nodes;
  std::unordered_set<Node*> gpu_set;
  for (int g = 0; g < num_servers; g++) {
    NVSwitchGroup* grp = system->get_group(g);
    for (int i = 0; i < gpus_per_server; i++) {
      Node* gpu = grp->get_gpu(i);
      gpu_nodes.push_back(gpu);
      gpu_set.insert(gpu);
    }
    for (int i = 0; i < num_leaves; i++)
      sw_nodes.push_back(grp->get_nvswitch(i));
  }

  // 拓扑参数
  NVSwitchGroup* grp0 = system->get_group(0);
  std::vector<int> links_per_sw = grp0->links_per_switch_;

  std::cout << "============================================" << std::endl;
  std::cout << "Switch 出端口竞争诊断" << std::endl;
  std::cout << "pkt_len=1, 注入率=18, link_aware=true" << std::endl;
  std::cout << "============================================" << std::endl;
  std::cout << "拓扑: " << num_gpus << " GPUs, " << num_servers << " servers, "
            << gpus_per_server << " GPUs/server, " << num_leaves << " leaves/server" << std::endl;
  printf("links_per_switch: [%d, %d, %d, %d]\n\n",
         links_per_sw[0], links_per_sw[1], links_per_sw[2], links_per_sw[3]);

  // 先打印 Leaf 0 的端口布局
  {
    Node* leaf0 = grp0->get_nvswitch(0);
    int n_links = links_per_sw[0];
    int gpu_ports_end = gpus_per_server * n_links;
    int intra_sw_offset = gpu_ports_end;
    printf("=== Leaf 0 端口布局 (radix=%d, n_links=%d) ===\n", leaf0->radix_, n_links);
    printf("  GPU 端口: port [0, %d) → 8 GPUs × %d links/GPU\n", gpu_ports_end, n_links);
    for (int g = 0; g < gpus_per_server; g++) {
      printf("    GPU %d: ports [%d, %d)\n", g, g * n_links, (g + 1) * n_links);
    }
    printf("  Leaf-Leaf 端口: port [%d, %d) → %d 其他 Leaf\n",
           intra_sw_offset, intra_sw_offset + num_leaves - 1, num_leaves - 1);
    int spine_offset = intra_sw_offset + num_leaves - 1;
    printf("  Spine 端口: port [%d, %d) → %d spine switches\n\n",
           spine_offset, leaf0->radix_, leaf0->radix_ - spine_offset);
  }

  // 运行仿真，同时逐周期追踪 Switch 出端口被请求的情况
  system->reset();
  network->reset_diagnostics();
  network->enable_diagnostics(true);

  TM = new TrafficManager();
  TM->traffic_ = "collective_ring_all_reduce";
  TM->traffic_scale_ = num_gpus;
  TM->data_size = 4096;

  std::vector<Packet*> packets;

  // 统计每个 switch 的每个出端口的阻塞次数
  // 使用诊断系统已有的 link_blocked

  uint64_t total_cycles = 0;
  for (uint64_t cyc = 0; ; cyc++) {
    TM->genMes(packets, cyc);
    run_one_cycle(packets, system, cyc);
    if (TM->is_done) { total_cycles = cyc + 1; break; }
    if (cyc > 10000) { total_cycles = cyc + 1; break; }
  }

  printf("仿真完成: %lu cycles\n\n", total_cycles);

  // 分析 Server 0 的 4 个 Leaf Switch 的逐端口阻塞
  std::cout << "========== Server 0 各 Leaf Switch 逐端口阻塞 ==========" << std::endl;

  uint64_t total_sw_blocked = 0;
  uint64_t gpu_port_blocked = 0;
  uint64_t leaf_port_blocked = 0;
  uint64_t spine_port_blocked = 0;

  for (int sw_id = 0; sw_id < num_leaves; sw_id++) {
    Node* sw = grp0->get_nvswitch(sw_id);
    int n_links = links_per_sw[sw_id];
    int gpu_ports_end = gpus_per_server * n_links;
    int intra_sw_offset = gpu_ports_end;
    int spine_offset = intra_sw_offset + num_leaves - 1;

    printf("\n--- Leaf %d (radix=%d, links/GPU=%d) ---\n", sw_id, sw->radix_, n_links);

    // GPU 出端口
    printf("  [GPU 出端口] (到哪个 GPU 的哪条链路):\n");
    uint64_t sw_gpu_total = 0;
    for (int g = 0; g < gpus_per_server; g++) {
      uint64_t gpu_total = 0;
      for (int k = 0; k < n_links; k++) {
        int port = g * n_links + k;
        uint64_t blk = network->get_diag_link_blocked_count(sw, port);
        gpu_total += blk;
      }
      if (gpu_total > 0)
        printf("    → GPU %d: %lu blocked (avg %.1f/port, %.1f/cycle)\n",
               g, gpu_total, (double)gpu_total / n_links, (double)gpu_total / total_cycles);
      sw_gpu_total += gpu_total;
      gpu_port_blocked += gpu_total;
    }
    printf("    小计: %lu\n", sw_gpu_total);

    // Leaf-Leaf 出端口
    uint64_t sw_leaf_total = 0;
    printf("  [Leaf-Leaf 出端口]:\n");
    for (int other = 0; other < num_leaves - 1; other++) {
      int port = intra_sw_offset + other;
      if (port < sw->radix_) {
        uint64_t blk = network->get_diag_link_blocked_count(sw, port);
        if (blk > 0)
          printf("    → Leaf port %d: %lu blocked (%.1f/cycle)\n", port, blk, (double)blk / total_cycles);
        sw_leaf_total += blk;
      }
    }
    printf("    小计: %lu\n", sw_leaf_total);
    leaf_port_blocked += sw_leaf_total;

    // Spine 出端口
    uint64_t sw_spine_total = 0;
    for (int port = spine_offset; port < sw->radix_; port++) {
      uint64_t blk = network->get_diag_link_blocked_count(sw, port);
      sw_spine_total += blk;
    }
    if (sw_spine_total > 0)
      printf("  [Spine 出端口]: %lu blocked\n", sw_spine_total);
    spine_port_blocked += sw_spine_total;

    total_sw_blocked += sw_gpu_total + sw_leaf_total + sw_spine_total;
  }

  printf("\n========== Server 0 阻塞汇总 ==========\n");
  printf("  Switch→GPU:  %lu (%.1f%%)\n", gpu_port_blocked, 100.0 * gpu_port_blocked / std::max(total_sw_blocked, (uint64_t)1));
  printf("  Switch→Leaf: %lu (%.1f%%)\n", leaf_port_blocked, 100.0 * leaf_port_blocked / std::max(total_sw_blocked, (uint64_t)1));
  printf("  Switch→Spine: %lu (%.1f%%)\n", spine_port_blocked, 100.0 * spine_port_blocked / std::max(total_sw_blocked, (uint64_t)1));

  // 分析核心：Ring 中每个 GPU pair 使用哪些端口
  std::cout << "\n========== Ring 流量路径分析 ==========" << std::endl;
  printf("Ring: GPU_i → GPU_{i+1 mod 64}\n\n");

  printf("Server 0 内部 (Leaf 0, links/GPU=%d):\n", links_per_sw[0]);
  printf("  GPU 0→1: 进入 Leaf0 input ports [0,%d), 目标 Leaf0 output ports [%d,%d) (→GPU1)\n",
         links_per_sw[0], 1 * links_per_sw[0], 2 * links_per_sw[0]);
  printf("  GPU 1→2: 进入 Leaf0 input ports [%d,%d), 目标 Leaf0 output ports [%d,%d) (→GPU2)\n",
         1 * links_per_sw[0], 2 * links_per_sw[0], 2 * links_per_sw[0], 3 * links_per_sw[0]);
  printf("  ...\n");
  printf("  每对 GPU 使用不同的输出端口组 → 理论上无出端口冲突\n");
  printf("  但还有间接路径: Leaf0 → Leaf1 → GPU, 共享 Leaf-Leaf 端口 (仅 1 条/对)\n\n");

  // 关键分析：看 Leaf-Leaf 端口利用率
  printf("========== Leaf-Leaf 端口瓶颈分析 ==========\n");
  for (int sw_id = 0; sw_id < num_leaves; sw_id++) {
    Node* sw = grp0->get_nvswitch(sw_id);
    int n_links = links_per_sw[sw_id];
    int gpu_ports_end = gpus_per_server * n_links;
    int intra_sw_offset = gpu_ports_end;

    printf("Leaf %d → 其他 Leaf:\n", sw_id);
    for (int other = 0; other < num_leaves - 1; other++) {
      int port = intra_sw_offset + other;
      if (port < sw->radix_) {
        uint64_t blk = network->get_diag_link_blocked_count(sw, port);
        // 检查这个端口的 in_link_used 状态分布 — 用阻塞次数估算利用率
        bool is_used = sw->link_buffers_[port] ? sw->link_buffers_[port]->is_in_link_used() : false;
        printf("  port %d (→Leaf %d): blocked=%lu, est_util=%.0f%%\n",
               port, other < sw_id ? other : other + 1, blk,
               std::min(100.0, 100.0 * blk / total_cycles));
      }
    }
  }

  // 带宽分析
  printf("\n========== 带宽需求 vs 供给分析 ==========\n");
  int intra_pairs = gpus_per_server - 1;  // 7 intra-server pairs
  int inter_pairs = 1;                     // 1 inter-server pair (GPU 7→8)
  int inject_per_gpu = 18;  // pkt_len=1

  printf("每 Server %d 个 intra-pair, 每 pair 注入 %d pkts/cycle\n", intra_pairs, inject_per_gpu);
  printf("  总 intra 注入: %d × %d = %d pkts/cycle\n", intra_pairs, inject_per_gpu, intra_pairs * inject_per_gpu);
  printf("  总 inter 注入: %d × %d = %d pkts/cycle\n\n", inter_pairs, inject_per_gpu, inter_pairs * inject_per_gpu);

  printf("每 Leaf Switch GPU 出端口容量:\n");
  for (int sw_id = 0; sw_id < num_leaves; sw_id++) {
    int n = links_per_sw[sw_id];
    printf("  Leaf %d: %d GPUs × %d ports/GPU = %d 出端口, 容量 %d pkts/cycle\n",
           sw_id, gpus_per_server, n, gpus_per_server * n, gpus_per_server * n);
  }

  printf("\nLeaf-Leaf 端口容量:\n");
  printf("  每对 Leaf 之间: 1 条链路, 容量 1 pkt/cycle\n");
  printf("  每个 Leaf 有 %d 条 Leaf-Leaf 链路, 总容量 %d pkts/cycle\n\n", num_leaves - 1, num_leaves - 1);

  // 分析间接路径流量
  printf("========== 直接 vs 间接路径流量分析 ==========\n");
  printf("GPU 0→1 的路由候选 (在 Leaf 0 内):\n");
  printf("  直接路径: Leaf0 → GPU1 (%d 条链路)\n", links_per_sw[0]);
  printf("  间接路径: Leaf0 → Leaf1 (1 条), Leaf0 → Leaf2 (1 条), Leaf0 → Leaf3 (1 条)\n");
  printf("  总候选: %d 直接 + 3 间接 = %d 条\n\n", links_per_sw[0], links_per_sw[0] + 3);

  int n0 = links_per_sw[0];
  printf("问题核心:\n");
  printf("  GPU 0→1 从 Leaf0 通过: 直接 %d 条 (容量 %d/cycle) + 间接 3 条 (容量 3/cycle) = 总 %d/cycle\n",
         n0, n0, n0 + 3);
  printf("  GPU 0 注入 18 pkts/cycle, 分散到 4 个 Leaf, 每 Leaf 约 %.1f pkts/cycle\n",
         18.0 * n0 / 18);
  printf("  但 Leaf0 的 Leaf-Leaf 出端口只有 3 条, 被所有 7 个 intra-pair 共享!\n");
  printf("  7 对 × (每对约 %.1f pkts via 间接) ≈ %.0f pkts 竞争 3 条 Leaf-Leaf 链路\n\n",
         18.0 * 3.0 / (n0 + 3) / 4, 7 * 18.0 * 3.0 / (n0 + 3) / 4);

  // sw_link 阻塞分析 — 输入侧被 sw_link 卡住
  printf("========== Switch crossbar (sw_link) 阻塞 ==========\n");
  uint64_t sw_link_blk_total = 0;
  for (int sw_id = 0; sw_id < num_leaves; sw_id++) {
    Node* sw = grp0->get_nvswitch(sw_id);
    uint64_t sw_blk = 0;
    for (int p = 0; p < sw->radix_; p++) {
      if (sw->in_buffers_[p] && sw->in_buffers_[p]->is_sw_link_used())
        sw_blk++;
    }
    // sw_link_used 是瞬时值，用累积的 link_blocked 更有意义
    // 但 link_blocked 是出端口统计。sw_link 阻塞意味着输入端口的 crossbar 忙
    // 这里用 packet wait_timer 统计更合适
  }
  printf("  (sw_link 阻塞由 link_blocked 间接反映: 出端口忙→入端口等待)\n");

  delete TM;
  delete system;
  delete param;
  return 0;
}
