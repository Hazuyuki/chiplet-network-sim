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
  int dest1;
  int num_groups = network->num_groups_;
  int cores_per_group = (num_groups > 0 && network->groups_[0] != nullptr)
                            ? network->groups_[0]->num_cores_
                            : traffic_scale_;

  for (pkt_for_injection_ += message_per_cycle(); pkt_for_injection_ > traffic_scale_;
       pkt_for_injection_ -= traffic_scale_) {
    for (int src = 0; src < traffic_scale_; src++) {
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
        dest1 = topo_aware_next_in_ring(src, traffic_scale_, param->topology, num_groups,
                                        cores_per_group);
      } else {
        dest1 = (src + 1) % traffic_scale_;
      }
      Packet* mess =
          new Packet(network->int_to_nodeid(src), network->int_to_nodeid(dest1), message_length_);
      packets.push_back(mess);
      all_message_num_ += 1;
    }
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
