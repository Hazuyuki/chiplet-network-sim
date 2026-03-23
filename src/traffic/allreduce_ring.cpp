/**
 * Ring All-Reduce: 拓扑感知 collective，适配 NVSwitch Leaf-Spine 胖树等
 *
 * - NVSwitch: group-contiguous + snake 交替排序，最小化跨 group 跳数，分散 spine 负载
 * - DragonflyChiplet / RailX2DHyperX: 保持原有拓扑专用逻辑
 * - 其他: 回退到简单 (src+1)%N
 */

#include "traffic_manager.h"
#include <string>

// NVSwitch 拓扑感知：snake 交替排序，rank <-> ring_pos 映射
// group 偶数为正序，奇数为逆序，使 inter-group 桥接分散到不同 leaf
static int nvswitch_rank_to_ring_pos(int rank, int num_groups, int cores_per_group) {
  int g = rank / cores_per_group;
  int local = rank % cores_per_group;
  if (g % 2 == 0)
    return g * cores_per_group + local;
  return g * cores_per_group + (cores_per_group - 1 - local);
}

static int nvswitch_ring_pos_to_rank(int pos, int num_groups, int cores_per_group) {
  int g = pos / cores_per_group;
  int offset = pos % cores_per_group;
  if (g % 2 == 0)
    return g * cores_per_group + offset;
  return g * cores_per_group + (cores_per_group - 1 - offset);
}

// 拓扑感知：src 在 ring 中的下一跳
static int topo_aware_next_in_ring(int src, int n, const std::string& topology, int num_groups,
                                   int cores_per_group) {
  if (topology == "NVSwitch" && num_groups > 1 && cores_per_group > 0) {
    int pos = nvswitch_rank_to_ring_pos(src, num_groups, cores_per_group);
    return nvswitch_ring_pos_to_rank((pos + 1) % n, num_groups, cores_per_group);
  }
  return (src + 1) % n;
}

static int topo_aware_prev_in_ring(int src, int n, const std::string& topology, int num_groups,
                                   int cores_per_group) {
  if (topology == "NVSwitch" && num_groups > 1 && cores_per_group > 0) {
    int pos = nvswitch_rank_to_ring_pos(src, num_groups, cores_per_group);
    return nvswitch_ring_pos_to_rank((pos - 1 + n) % n, num_groups, cores_per_group);
  }
  return (src - 1 + n) % n;
}

void TrafficManager::ring_all_reduce_mess(std::vector<Packet*>& packets) {
  // Ring All-Reduce 需要 N-1 轮 Reduce-Scatter + N-1 轮 All-Gather = 2(N-1) 轮
  int total_stages = (traffic_scale_ - 1) * 2;

  // 获取拓扑参数
  int num_groups = network->num_groups_;
  int cores_per_group = (num_groups > 0 && network->groups_[0] != nullptr)
                            ? network->groups_[0]->num_cores_
                            : traffic_scale_;

  if (stage < total_stages) {
    for (int src = 0; src < traffic_scale_; src++) {
      int dest1;
      if (param->topology == "DragonflyChiplet") {
        if (src % 16 == 0 || src % 16 == 1 || src % 16 == 4 || src % 16 == 5)
          dest1 = (src + 2) % traffic_scale_;
        else if (src % 16 == 2 || src % 16 == 3 || src % 16 == 6 || src % 16 == 7)
          dest1 = (src + 8) % traffic_scale_;
        else if (src % 16 == 8 || src % 16 == 9 || src % 16 == 12 || src % 16 == 13)
          dest1 = (src + 8) % traffic_scale_;
        else if (src % 16 == 10 || src % 16 == 11 || src % 16 == 14 || src % 16 == 15)
          dest1 = (src - 2) % traffic_scale_;
      } else if (param->topology == "NVSwitch") {
        // 前 N-1 轮是 Reduce-Scatter，后 N-1 轮是 All-Gather
        if (stage < traffic_scale_ - 1) {
          // Reduce-Scatter: 向 ring 中下一个节点发送
          dest1 = topo_aware_next_in_ring(src, traffic_scale_, param->topology, num_groups,
                                          cores_per_group);
        } else {
          // All-Gather: 向 ring 中上一个节点发送
          dest1 = topo_aware_prev_in_ring(src, traffic_scale_, param->topology, num_groups,
                                          cores_per_group);
        }
      } else {
        if (stage < traffic_scale_ - 1) {
          dest1 = (src + 1) % traffic_scale_;
        } else {
          dest1 = (src - 1 + traffic_scale_) % traffic_scale_;
        }
      }
      Packet* mess =
          new Packet(network->int_to_nodeid(src), network->int_to_nodeid(dest1), message_length_);
      packets.push_back(mess);
      all_message_num_ += 1;
    }
    stage++;
  } else if (stage == total_stages) {
    // 完成
    stage = 0;
    is_done = true;
    throughput = (double)data_size / cycles;
  }
}

void TrafficManager::ring_all_reduce_bi_mess(std::vector<Packet*>& packets) {
  int num_groups = network->num_groups_;
  int cores_per_group = (num_groups > 0 && network->groups_[0] != nullptr)
                            ? network->groups_[0]->num_cores_
                            : traffic_scale_;

  int dest1, dest2;
  for (pkt_for_injection_ += message_per_cycle(); pkt_for_injection_ > traffic_scale_ * 2;
       pkt_for_injection_ -= traffic_scale_ * 2) {
    for (int src = 0; src < traffic_scale_; src++) {
      if (param->topology == "DragonflyChiplet") {
        if (src % 16 == 0 || src % 16 == 1 || src % 16 == 4 || src % 16 == 5) {
          dest1 = (src + 2) % traffic_scale_;
          dest2 = (src - 8 + traffic_scale_) % traffic_scale_;
        } else if (src % 16 == 2 || src % 16 == 3 || src % 16 == 6 || src % 16 == 7) {
          dest1 = (src + 8) % traffic_scale_;
          dest2 = (src - 2) % traffic_scale_;
        } else if (src % 16 == 8 || src % 16 == 9 || src % 16 == 12 || src % 16 == 13) {
          dest1 = (src + 8) % traffic_scale_;
          dest2 = (src + 2) % traffic_scale_;
        } else if (src % 16 == 10 || src % 16 == 11 || src % 16 == 14 || src % 16 == 15) {
          dest1 = (src - 2) % traffic_scale_;
          dest2 = (src - 8) % traffic_scale_;
        }
      } else if (param->topology == "RailX2DHyperX") {
        int node_per_group = network->groups_[0]->num_cores_;
        assert(traffic_scale_ == 16 && node_per_group == 16);
        switch (src) {
          case 0:
            dest1 = 1;
            dest2 = 4;
            break;
          case 1:
            dest1 = 0;
            dest2 = 2;
            break;
          case 2:
            dest1 = 1;
            dest2 = 3;
            break;
          case 3:
            dest1 = 2;
            dest2 = 7;
            break;
          case 4:
            dest1 = 0;
            dest2 = 8;
            break;
          case 5:
            dest1 = 6;
            dest2 = 9;
            break;
          case 6:
            dest1 = 5;
            dest2 = 7;
            break;
          case 7:
            dest1 = 3;
            dest2 = 6;
            break;
          case 8:
            dest1 = 4;
            dest2 = 12;
            break;
          case 9:
            dest1 = 5;
            dest2 = 10;
            break;
          case 10:
            dest1 = 9;
            dest2 = 11;
            break;
          case 11:
            dest1 = 10;
            dest2 = 15;
            break;
          case 12:
            dest1 = 8;
            dest2 = 13;
            break;
          case 13:
            dest1 = 12;
            dest2 = 14;
            break;
          case 14:
            dest1 = 13;
            dest2 = 15;
            break;
          case 15:
            dest1 = 11;
            dest2 = 14;
            break;
          default:
            dest1 = (src + 1) % traffic_scale_;
            dest2 = (src - 1 + traffic_scale_) % traffic_scale_;
            break;
        }
      } else if (param->topology == "NVSwitch") {
        dest1 = topo_aware_next_in_ring(src, traffic_scale_, param->topology, num_groups,
                                        cores_per_group);
        dest2 = topo_aware_prev_in_ring(src, traffic_scale_, param->topology, num_groups,
                                        cores_per_group);
      } else {
        dest1 = (src + 1) % traffic_scale_;
        dest2 = (src - 1 + traffic_scale_) % traffic_scale_;
      }
      Packet* mess =
          new Packet(network->int_to_nodeid(src), network->int_to_nodeid(dest1), message_length_);
      packets.push_back(mess);
      mess = new Packet(network->int_to_nodeid(src), network->int_to_nodeid(dest2), message_length_);
      packets.push_back(mess);
      all_message_num_ += 2;
    }
  }
}

/**
 * Hierarchical All-Reduce: 适配 Super Node 架构
 *
 * 算法流程（以 64 GPU 为例，8 GPU/server，8 servers/super-node）：
 * - Stage 1-7: Server 内 Ring Reduce-Scatter (每 server 内部 8 GPU ring)
 * - Stage 8-14: Server 间 Ring Reduce-Scatter (8 servers ring)
 * - Stage 15-21: Server 间 Ring All-Gather
 * - Stage 22-28: Server 内 Ring All-Gather
 *
 * 总共 (gpus_per_server - 1) + (num_servers - 1) + (num_servers - 1) + (gpus_per_server - 1) 轮
 */
void TrafficManager::hierarchical_all_reduce_mess(std::vector<Packet*>& packets, uint64_t cyc) {
  // 使用类成员 stage 替代静态变量

  // 获取拓扑参数
  int num_groups = network->num_groups_;  // num_servers_per_super_node
  int cores_per_group = (num_groups > 0 && network->groups_[0] != nullptr)
                          ? network->groups_[0]->num_cores_  // num_gpus_per_server
                          : traffic_scale_;

  int num_servers = num_groups;
  int gpus_per_server = cores_per_group;
  int total_gpus = traffic_scale_;

  // 验证参数
  if (total_gpus != num_servers * gpus_per_server) {
    std::cerr << "Warning: hierarchical_all_reduce requires total_gpus=" << num_servers << "*"
              << gpus_per_server << "=" << (num_servers * gpus_per_server)
              << ", but got " << total_gpus << ". Falling back to ring_all_reduce." << std::endl;
    ring_all_reduce_mess(packets);
    return;
  }

  // 计算总阶段数
  int intra_reduce_stages = gpus_per_server - 1;      // Server 内 Reduce-Scatter
  int inter_reduce_stages = num_servers - 1;           // Server 间 Reduce-Scatter
  int inter_gather_stages = num_servers - 1;           // Server 间 All-Gather
  int intra_gather_stages = gpus_per_server - 1;       // Server 内 All-Gather
  int total_stages = intra_reduce_stages + inter_reduce_stages + inter_gather_stages + intra_gather_stages;

  if (stage < total_stages) {
    for (int src = 0; src < traffic_scale_; src++) {
      int server_id = src / gpus_per_server;
      int gpu_in_server = src % gpus_per_server;
      int dest = src;

      if (stage < intra_reduce_stages) {
        // Stage 1: Server 内 Reduce-Scatter - 每个 server 内部 ring
        int server_base = server_id * gpus_per_server;
        dest = server_base + (gpu_in_server + 1) % gpus_per_server;
      } else if (stage < intra_reduce_stages + inter_reduce_stages) {
        // Stage 2: Server 间 Reduce-Scatter - server 间 ring，使用 gpu 0 作为代表
        if (gpu_in_server == 0) {
          int next_server = (server_id + 1) % num_servers;
          dest = next_server * gpus_per_server;
        } else {
          // 其他 GPU 原地等待
          continue;
        }
      } else if (stage < intra_reduce_stages + inter_reduce_stages + inter_gather_stages) {
        // Stage 3: Server 间 All-Gather - server 间 ring 反向
        if (gpu_in_server == 0) {
          int prev_server = (server_id - 1 + num_servers) % num_servers;
          dest = prev_server * gpus_per_server;
        } else {
          continue;
        }
      } else {
        // Stage 4: Server 内 All-Gather - 每个 server 内部 ring 反向
        int server_base = server_id * gpus_per_server;
        dest = server_base + (gpu_in_server - 1 + gpus_per_server) % gpus_per_server;
      }

      // 确保 src != dest
      if (src != dest) {
        Packet* mess = new Packet(network->int_to_nodeid(src), network->int_to_nodeid(dest), message_length_);
        packets.push_back(mess);
        all_message_num_ += 1;
      }
    }
    stage++;
  } else if (stage == total_stages) {
    stage = 0;
    is_done = true;
    throughput = (double)data_size / cycles;
  }
}
