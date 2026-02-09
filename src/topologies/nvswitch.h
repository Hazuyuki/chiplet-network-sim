#pragma once
#include "system.h"

// NVSwitchSpineGroup holds spine switches for inter-group connectivity (Leaf-Spine topology)
class NVSwitchSpineGroup : public Group {
 public:
  NVSwitchSpineGroup(int num_spine_sw, int spine_radix, int vc_num, int switch_buffer_size,
                     Channel switch_switch_channel);
  ~NVSwitchSpineGroup();
  void set_group(System* system, int group_id) override;
  inline Node* get_spine_switch(int spine_id) const { return nodes_[spine_id]; }
  int num_spine_switches_;
};

// NVSwitchGroup represents a group (node) containing GPUs and Leaf NVSwitches
// Each GPU has gpu_nvlink_ports links distributed across switches for packet spraying
class NVSwitchGroup : public Group {
 public:
  NVSwitchGroup(int num_gpus, int num_switches, int gpu_nvlink_ports, int leaf_switch_radix,
                int vc_num, int gpu_buffer_size, int switch_buffer_size,
                Channel gpu_switch_channel, Channel switch_switch_channel,
                const std::vector<int>& links_per_switch);
  ~NVSwitchGroup();

  void set_group(System* system, int group_id) override;

  inline Node* get_gpu(int gpu_id) const {
    return nodes_[gpu_id];
  }
  inline Node* get_nvswitch(int switch_id) const {
    return nodes_[num_gpus_ + switch_id];
  }

  int num_gpus_;
  int num_switches_;
  int gpu_nvlink_ports_;
  int leaf_switch_radix_;
  std::vector<int> links_per_switch_;  // links_per_switch[sw] = GPU ports to this switch
  Channel gpu_switch_channel_;
  Channel switch_switch_channel_;
};

// NVSwitchSystem represents a network with NVSwitch topology
// Supports multiple groups (e.g., multiple baseboards in DGX-2)
class NVSwitchSystem : public System {
 public:
  NVSwitchSystem();
  ~NVSwitchSystem();

  void read_config() override;
  void print_config() override;

  void connect_gpus_to_switches();  // Connect GPUs to NVSwitches
  void connect_switches();           // Connect NVSwitches (within group and between groups)

  void routing_algorithm(Packet& s) const override;
  void direct_routing(Packet& s) const;  // Direct routing: GPU -> Switch -> GPU
  void min_routing(Packet& s) const;     // MIN routing for load balancing

  inline NVSwitchGroup* get_group(int group_id) const {
    return static_cast<NVSwitchGroup*>(System::get_group(group_id));
  }
  inline NVSwitchSpineGroup* get_spine_group() const {
    return num_spine_switches_ > 0
               ? static_cast<NVSwitchSpineGroup*>(groups_[num_groups_])
               : nullptr;
  }
  void connect_leaf_to_spine();

  std::string algorithm_;

  // Configuration parameters
  int num_gpus_per_group_;      // Number of GPUs per group
  int num_switches_per_group_;   // Number of NVSwitches per group
  int num_groups_;               // Number of groups (e.g., baseboards)
  int gpu_nvlink_ports_;         // NVLink ports per GPU
  int leaf_switch_radix_;        // Radix of each Leaf NVSwitch
  int num_spine_switches_;       // 0 = no spine; >0 = Leaf-Spine inter-node
  int spine_leaf_links_per_pair_; // NVL256 式：每对 (leaf, spine) 的链路数，>1 为无阻塞多链路
  bool switches_fully_connected_; // Whether switches are fully connected
  bool inter_group_sw_connect_;   // Whether switches connect between groups
  std::vector<int> links_per_switch_;  // links_per_switch[sw] = GPU ports to this switch

  Channel gpu_switch_channel_;   // Channel between GPU and NVSwitch
  Channel switch_switch_channel_; // Channel between NVSwitches

  // Note: groups_ is inherited from System, no need for reference member
};
