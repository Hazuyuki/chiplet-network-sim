/**
 * NVSwitch 拓扑：Leaf-Spine 结构，支持 NVL72/NVL256 风格无阻塞网络
 * 参见 docs/NVL256无阻塞网络结构.md
 * spine_non_blocking=true 时 num_spine_switches=max_gpu_ports，leaf-spine 无 oversubscription
 */
#include "nvswitch.h"
#include "traffic_manager.h"
#include <algorithm>
#include <random>

NVSwitchSpineGroup::NVSwitchSpineGroup(int num_spine_sw, int spine_radix, int vc_num,
                                       int switch_buffer_size, Channel switch_switch_channel)
    : Group(), num_spine_switches_(num_spine_sw) {
  num_nodes_ = num_spine_sw;
  num_cores_ = 0;
  for (int i = 0; i < num_spine_sw; i++) {
    nodes_.push_back(new Node(spine_radix, vc_num, switch_buffer_size, switch_switch_channel));
  }
}

NVSwitchSpineGroup::~NVSwitchSpineGroup() {
  for (auto node : nodes_) {
    delete node;
  }
  nodes_.clear();
}

void NVSwitchSpineGroup::set_group(System* system, int group_id) {
  system_ = system;
  group_id_ = group_id;
  for (int i = 0; i < num_spine_switches_; i++) {
    nodes_[i]->set_node(this, NodeID(i, group_id));
  }
}

NVSwitchGroup::NVSwitchGroup(int num_gpus, int num_switches, int gpu_nvlink_ports, int leaf_switch_radix,
                             int vc_num, int gpu_buffer_size, int switch_buffer_size,
                             Channel gpu_switch_channel, Channel switch_switch_channel,
                             const std::vector<int>& links_per_switch)
    : Group(), num_gpus_(num_gpus), num_switches_(num_switches), gpu_nvlink_ports_(gpu_nvlink_ports),
      leaf_switch_radix_(leaf_switch_radix), links_per_switch_(links_per_switch),
      gpu_switch_channel_(gpu_switch_channel), switch_switch_channel_(switch_switch_channel) {
  num_nodes_ = num_gpus_ + num_switches_;
  num_cores_ = num_gpus_;  // Only GPUs are cores (can inject/receive packets)

  // Create GPU nodes: each GPU has gpu_nvlink_ports links to switches (packet spraying)
  for (int i = 0; i < num_gpus_; i++) {
    nodes_.push_back(new Node(gpu_nvlink_ports_, vc_num, gpu_buffer_size, gpu_switch_channel));
  }

  // Create Leaf NVSwitch nodes（NVL256 等 Spine-Leaf 下 Switch 可配更大 buffer）
  for (int i = 0; i < num_switches_; i++) {
    nodes_.push_back(new Node(leaf_switch_radix, vc_num, switch_buffer_size, switch_switch_channel));
  }
}

NVSwitchGroup::~NVSwitchGroup() {
  for (auto node : nodes_) {
    delete node;
  }
  nodes_.clear();
}

void NVSwitchGroup::set_group(System* system, int group_id) {
  // Set system and group_id first
  system_ = system;
  group_id_ = group_id;
  
  // Set node IDs for all nodes (override base class behavior)
  // GPU nodes: 0 to num_gpus_-1
  for (int i = 0; i < num_gpus_; i++) {
    nodes_[i]->set_node(this, NodeID(i, group_id));
  }
  // Switch nodes: num_gpus_ to num_gpus_ + num_switches_ - 1
  for (int i = 0; i < num_switches_; i++) {
    nodes_[num_gpus_ + i]->set_node(this, NodeID(num_gpus_ + i, group_id));
  }
  
  // Connections will be set up by NVSwitchSystem::connect_gpus_to_switches()
  // and NVSwitchSystem::connect_switches()
}

NVSwitchSystem::NVSwitchSystem() {
  read_config();
  
  num_nodes_ = num_servers_per_super_node_ * (num_gpus_per_server_ + num_switches_per_server_);
  num_cores_ = num_servers_per_super_node_ * num_gpus_per_server_;
  // Set base class num_groups_ for print_config()
  System::num_groups_ = num_servers_per_super_node_;

  // GPU 与 Switch 可分别设 buffer：Switch 更大以匹配 NVL256 等两级无阻塞胖树
  int gpu_buf = param->buffer_size;
  int sw_buf = (param->switch_buffer_size > 0) ? param->switch_buffer_size : param->buffer_size;

  // Create groups (machine groups with GPUs + leaf switches)
  groups_.reserve(num_servers_per_super_node_ + (num_spine_switches_ > 0 ? 1 : 0));
  for (int group_id = 0; group_id < num_servers_per_super_node_; group_id++) {
    groups_.push_back(new NVSwitchGroup(num_gpus_per_server_, num_switches_per_server_,
                                       gpu_nvlink_ports_, leaf_switch_radix_,
                                       param->vc_number, gpu_buf, sw_buf,
                                       gpu_switch_channel_, switch_switch_channel_,
                                       links_per_switch_));
    groups_[group_id]->set_group(this, group_id);
  }

  // Create spine group (for inter-group via spine)
  if (num_spine_switches_ > 0) {
    int spine_radix =
        num_servers_per_super_node_ * num_switches_per_server_ * spine_leaf_links_per_pair_;
    groups_.push_back(new NVSwitchSpineGroup(num_spine_switches_, spine_radix,
                                            param->vc_number, sw_buf,
                                            switch_switch_channel_));
    groups_[num_servers_per_super_node_]->set_group(this, num_servers_per_super_node_);
    num_nodes_ += num_spine_switches_;
  }

  // Connect GPUs to switches
  connect_gpus_to_switches();
  
  // Connect switches (leaf-leaf within group, and leaf-leaf between groups if no spine)
  connect_switches();

  // Connect leaf to spine (when spine exists)
  if (num_spine_switches_ > 0) {
    connect_leaf_to_spine();
  }

  print_config();
}

NVSwitchSystem::~NVSwitchSystem() {
  for (auto group : groups_) {
    delete group;
  }
  groups_.clear();
}

void NVSwitchSystem::read_config() {
  // 新参数名 (Super Node 架构)
  num_gpus_per_server_ = param->params_ptree.get<int>("Network.num_gpus_per_server", -1);
  num_switches_per_server_ = param->params_ptree.get<int>("Network.num_switches_per_server", -1);
  num_servers_per_super_node_ = param->params_ptree.get<int>("Network.num_servers_per_super_node", -1);
  
  // 兼容旧参数名 (group 架构)
  if (num_gpus_per_server_ <= 0) {
    num_gpus_per_server_ = param->params_ptree.get<int>("Network.num_gpus_per_group", 8);
  }
  if (num_switches_per_server_ <= 0) {
    num_switches_per_server_ = param->params_ptree.get<int>("Network.num_switches_per_group", 4);
  }
  if (num_servers_per_super_node_ <= 0) {
    num_servers_per_super_node_ = param->params_ptree.get<int>("Network.num_groups", 1);
  }
  
  gpu_nvlink_ports_ = param->params_ptree.get<int>("Network.gpu_nvlink_ports", 18);
  num_spine_switches_ = param->params_ptree.get<int>("Network.num_spine_switches", 0);
  spine_leaf_links_per_pair_ =
      param->params_ptree.get<int>("Network.spine_leaf_links_per_pair", 1);

  switches_fully_connected_ = param->params_ptree.get<bool>("Network.switches_fully_connected", true);
  inter_group_sw_connect_ = param->params_ptree.get<bool>("Network.inter_group_sw_connect", false);

  algorithm_ = param->params_ptree.get<std::string>("Network.routing_algorithm", "direct");

  int gpu_switch_latency = param->params_ptree.get<int>("Network.gpu_switch_latency", 1);
  int switch_switch_latency = param->params_ptree.get<int>("Network.switch_switch_latency", 1);

  gpu_switch_channel_ = Channel(1, gpu_switch_latency);
  switch_switch_channel_ = Channel(1, switch_switch_latency);

  // Distribute gpu_nvlink_ports across switches (e.g. 18 ports, 4 switches -> 5,5,4,4)
  int base = gpu_nvlink_ports_ / num_switches_per_server_;
  int remainder = gpu_nvlink_ports_ % num_switches_per_server_;
  links_per_switch_.resize(num_switches_per_server_);
  for (int sw = 0; sw < num_switches_per_server_; sw++) {
    links_per_switch_[sw] = base + (sw < remainder ? 1 : 0);
  }

  // Leaf switch radix: GPU links + intra-group switch links + spine uplinks (or inter-group direct)
  int max_gpu_ports = 0;
  for (int sw = 0; sw < num_switches_per_server_; sw++) {
    int gpu_ports = num_gpus_per_server_ * links_per_switch_[sw];
    if (gpu_ports > max_gpu_ports) max_gpu_ports = gpu_ports;
  }
  int intra_sw_ports = num_switches_per_server_ - 1;
  bool spine_non_blocking =
      param->params_ptree.get<bool>("Network.spine_non_blocking", false);
  if (spine_non_blocking && num_servers_per_super_node_ > 1) {
    num_spine_switches_ = max_gpu_ports;
    inter_group_sw_connect_ = false;
    printf("Spine non-blocking: num_spine_switches = %d (max_gpu_ports)\n",
           num_spine_switches_);
  }
  int spine_ports = num_spine_switches_ * spine_leaf_links_per_pair_;
  int inter_group_ports = 0;
  if (num_spine_switches_ == 0 && inter_group_sw_connect_ && num_servers_per_super_node_ > 1) {
    // Port indices inter_offset + group2_id for group2_id=1..num_servers_per_super_node_-1,
    // max index = inter_offset + num_servers_per_super_node_ - 1, so need num_servers_per_super_node_ extra ports
    inter_group_ports = num_servers_per_super_node_;
  }
  leaf_switch_radix_ = max_gpu_ports + intra_sw_ports + spine_ports + inter_group_ports;

  printf("NVSwitch Topology: %d groups, %d GPUs/group, %d Leaf switches/group, "
         "gpu_nvlink_ports=%d, leaf_switch_radix=%d, spine=%d, spine_leaf_links_per_pair=%d\n",
         num_servers_per_super_node_, num_gpus_per_server_, num_switches_per_server_,
         gpu_nvlink_ports_, leaf_switch_radix_, num_spine_switches_,
         spine_leaf_links_per_pair_);
}

void NVSwitchSystem::print_config() {
  System::print_config();
  std::cout << "NVSwitch parameters: " << std::endl;
  std::cout << "  [Super Node Architecture]" << std::endl;
  std::cout << "  num_servers_per_super_node: " << num_servers_per_super_node_ << std::endl;
  std::cout << "  num_gpus_per_server: " << num_gpus_per_server_ << std::endl;
  std::cout << "  num_switches_per_server: " << num_switches_per_server_ << std::endl;
  std::cout << "  gpu_nvlink_ports: " << gpu_nvlink_ports_ << std::endl;
  std::cout << "  leaf_switch_radix: " << leaf_switch_radix_ << std::endl;
  std::cout << "  num_spine_switches: " << num_spine_switches_ << std::endl;
  std::cout << "  spine_leaf_links_per_pair: " << spine_leaf_links_per_pair_ << std::endl;
  std::cout << "  switches_fully_connected: " << switches_fully_connected_ << std::endl;
  std::cout << "  inter_group_sw_connect: " << inter_group_sw_connect_ << std::endl;
  std::cout << "  spine_non_blocking: "
            << param->params_ptree.get<bool>("Network.spine_non_blocking", false)
            << std::endl;
  if (param->switch_buffer_size > 0)
    std::cout << "  switch_buffer_size: " << param->switch_buffer_size << std::endl;
  std::cout << "  routing_algorithm: " << algorithm_ << std::endl;
}

void NVSwitchSystem::connect_gpus_to_switches() {
  // GPU port layout: ports [gpu_port_base[sw], gpu_port_base[sw]+links_per_switch_[sw]) go to switch sw
  std::vector<int> gpu_port_base(num_switches_per_server_);
  for (int sw = 1; sw < num_switches_per_server_; sw++) {
    gpu_port_base[sw] = gpu_port_base[sw - 1] + links_per_switch_[sw - 1];
  }

  for (int group_id = 0; group_id < num_servers_per_super_node_; group_id++) {
    NVSwitchGroup* group = get_group(group_id);
    for (int gpu_id = 0; gpu_id < num_gpus_per_server_; gpu_id++) {
      Node* gpu = group->get_gpu(gpu_id);
      for (int sw_id = 0; sw_id < num_switches_per_server_; sw_id++) {
        Node* nvswitch = group->get_nvswitch(sw_id);
        int n_links = links_per_switch_[sw_id];
        for (int k = 0; k < n_links; k++) {
          int gpu_port = gpu_port_base[sw_id] + k;
          int sw_port = gpu_id * n_links + k;
          Port::connect_port(gpu->ports_[gpu_port], nvswitch->ports_[sw_port]);
        }
      }
    }
  }
}

void NVSwitchSystem::connect_switches() {
  // Connect Leaf switches within each group
  if (switches_fully_connected_) {
    for (int group_id = 0; group_id < num_servers_per_super_node_; group_id++) {
      NVSwitchGroup* group = get_group(group_id);
      for (int sw1_id = 0; sw1_id < num_switches_per_server_; sw1_id++) {
        Node* sw1 = group->get_nvswitch(sw1_id);
        int sw1_gpu_ports = num_gpus_per_server_ * links_per_switch_[sw1_id];
        int sw1_port_offset = sw1_gpu_ports;

        for (int sw2_id = sw1_id + 1; sw2_id < num_switches_per_server_; sw2_id++) {
          Node* sw2 = group->get_nvswitch(sw2_id);
          int sw2_gpu_ports = num_gpus_per_server_ * links_per_switch_[sw2_id];
          int sw2_port_offset = sw2_gpu_ports;

          int sw1_port = sw1_port_offset + sw2_id - 1;
          int sw2_port = sw2_port_offset + sw1_id;
          if (sw1_port < sw1->radix_ && sw2_port < sw2->radix_) {
            Port::connect_port(sw1->ports_[sw1_port], sw2->ports_[sw2_port]);
          }
        }
      }
    }
  }

  // Connect leaf to spine (Leaf-Spine topology for inter-group)
  // Handled in connect_leaf_to_spine() when num_spine_switches_ > 0

  // Connect switches between groups (legacy direct leaf-leaf, when no Spine)
  if (inter_group_sw_connect_ && num_servers_per_super_node_ > 1 && num_spine_switches_ == 0) {
    for (int group1_id = 0; group1_id < num_servers_per_super_node_; group1_id++) {
      for (int group2_id = group1_id + 1; group2_id < num_servers_per_super_node_; group2_id++) {
        NVSwitchGroup* group1 = get_group(group1_id);
        NVSwitchGroup* group2 = get_group(group2_id);
        for (int sw_id = 0; sw_id < num_switches_per_server_; sw_id++) {
          Node* sw1 = group1->get_nvswitch(sw_id);
          Node* sw2 = group2->get_nvswitch(sw_id);
          int inter_offset = num_gpus_per_server_ * links_per_switch_[sw_id] +
                            num_switches_per_server_ - 1;
          int sw1_port = inter_offset + group2_id;
          int sw2_port = inter_offset + group1_id;
          if (sw1_port < sw1->radix_ && sw2_port < sw2->radix_) {
            Port::connect_port(sw1->ports_[sw1_port], sw2->ports_[sw2_port]);
          }
        }
      }
    }
  }
}

void NVSwitchSystem::connect_leaf_to_spine() {
  NVSwitchSpineGroup* spine_group = get_spine_group();
  if (spine_group == nullptr) return;

  int intra_sw_ports = num_switches_per_server_ - 1;
  for (int spine_id = 0; spine_id < num_spine_switches_; spine_id++) {
    Node* spine = spine_group->get_spine_switch(spine_id);
    for (int group_id = 0; group_id < num_servers_per_super_node_; group_id++) {
      NVSwitchGroup* group = get_group(group_id);
      for (int sw_id = 0; sw_id < num_switches_per_server_; sw_id++) {
        Node* leaf = group->get_nvswitch(sw_id);
        int gpu_ports_end = num_gpus_per_server_ * links_per_switch_[sw_id];
        int leaf_spine_base = gpu_ports_end + intra_sw_ports + spine_id * spine_leaf_links_per_pair_;
        int spine_port_base = (group_id * num_switches_per_server_ + sw_id) * spine_leaf_links_per_pair_;
        for (int link_idx = 0; link_idx < spine_leaf_links_per_pair_; link_idx++) {
          int leaf_spine_port = leaf_spine_base + link_idx;
          int spine_port = spine_port_base + link_idx;
          if (leaf_spine_port < leaf->radix_ && spine_port < spine->radix_) {
            Port::connect_port(leaf->ports_[leaf_spine_port], spine->ports_[spine_port]);
          }
        }
      }
    }
  }
}

void NVSwitchSystem::routing_algorithm(Packet& s) const {
  if (algorithm_ == "direct")
    direct_routing(s);
  else if (algorithm_ == "min")
    min_routing(s);
  else
    std::cerr << "Unknown routing algorithm: " << algorithm_ << std::endl;
}

void NVSwitchSystem::direct_routing(Packet& s) const {
  Node* cur_node = get_node(s.head_trace().id);
  
  int cur_group_id = s.head_trace().id.group_id;
  int dest_group_id = s.destination_.group_id;
  int cur_node_id = s.head_trace().id.node_id;
  int dest_node_id = s.destination_.node_id;

  std::vector<int> gpu_port_base(num_switches_per_server_);
  for (int sw = 1; sw < num_switches_per_server_; sw++) {
    gpu_port_base[sw] = gpu_port_base[sw - 1] + links_per_switch_[sw - 1];
  }

  // If current node is a spine switch: route down to *all* leafs in dest group (包泼洒，使流量可从多 leaf 汇聚到同一 GPU)
  if (num_spine_switches_ > 0 && cur_group_id == num_servers_per_super_node_) {
    for (int dest_sw_id = 0; dest_sw_id < num_switches_per_server_; dest_sw_id++) {
      int spine_port_base = (dest_group_id * num_switches_per_server_ + dest_sw_id) * spine_leaf_links_per_pair_;
      for (int link_idx = 0; link_idx < spine_leaf_links_per_pair_; link_idx++) {
        int spine_port = spine_port_base + link_idx;
        if (spine_port >= cur_node->radix_) break;
        Buffer* next_buffer = cur_node->link_buffers_[spine_port];
        if (next_buffer != nullptr) {
          for (int i = 0; i < param->vc_number; i++) {
            s.candidate_channels_.push_back(VCInfo(next_buffer, i));
          }
        }
      }
    }
    return;
  }

  // If current node is a GPU
  if (cur_node_id < num_gpus_per_server_) {
    if (cur_group_id == dest_group_id) {
      int switch_id = dest_node_id % num_switches_per_server_;
      for (int k = 0; k < links_per_switch_[switch_id]; k++) {
        int gpu_port = gpu_port_base[switch_id] + k;
        Buffer* next_buffer = cur_node->link_buffers_[gpu_port];
        if (next_buffer != nullptr) {
          for (int i = 0; i < param->vc_number; i++) {
            s.candidate_channels_.push_back(VCInfo(next_buffer, i));
          }
          break;
        }
      }
    }
    if (s.candidate_channels_.empty()) {
      for (int sw_id = 0; sw_id < num_switches_per_server_; sw_id++) {
        for (int k = 0; k < links_per_switch_[sw_id]; k++) {
          int gpu_port = gpu_port_base[sw_id] + k;
          if (gpu_port >= cur_node->radix_) break;
          Buffer* next_buffer = cur_node->link_buffers_[gpu_port];
          if (next_buffer != nullptr) {
            for (int i = 0; i < param->vc_number; i++) {
              s.candidate_channels_.push_back(VCInfo(next_buffer, i));
            }
            break;
          }
        }
        if (!s.candidate_channels_.empty()) break;
      }
    }
  }
  // If current node is a switch
  else {
    int switch_id = cur_node_id - num_gpus_per_server_;
    int n_links = links_per_switch_[switch_id];
    int gpu_ports_end = num_gpus_per_server_ * n_links;
    int intra_sw_offset = gpu_ports_end;

    if (cur_group_id == dest_group_id) {
      // 直连：本 Leaf 到目的 GPU 的端口
      for (int k = 0; k < n_links; k++) {
        int port_id = dest_node_id * n_links + k;
        if (port_id >= cur_node->radix_) break;
        Buffer* next_buffer = cur_node->link_buffers_[port_id];
        if (next_buffer != nullptr) {
          for (int i = 0; i < param->vc_number; i++) {
            s.candidate_channels_.push_back(VCInfo(next_buffer, i));
          }
        }
      }
      // 包泼洒：经其他 Leaf 再到目的 GPU，使流量可从多 Leaf 汇聚到同一 node
      for (int other_sw = 0; other_sw < num_switches_per_server_; other_sw++) {
        if (other_sw == switch_id) continue;
        int port_id = intra_sw_offset + (other_sw < switch_id ? other_sw : other_sw - 1);
        if (port_id >= cur_node->radix_) break;
        Buffer* next_buffer = cur_node->link_buffers_[port_id];
        if (next_buffer != nullptr) {
          for (int i = 0; i < param->vc_number; i++) {
            s.candidate_channels_.push_back(VCInfo(next_buffer, i));
          }
        }
      }
    }
    // Cross-group: via spine uplinks or direct leaf-leaf
    if (s.candidate_channels_.empty() && cur_group_id != dest_group_id) {
      if (num_spine_switches_ > 0) {
        // Use spine uplinks (Leaf-Spine): add all links to all spines for load spreading
        int spine_offset = intra_sw_offset + num_switches_per_server_ - 1;
        for (int spine_id = 0; spine_id < num_spine_switches_; spine_id++) {
          for (int link_idx = 0; link_idx < spine_leaf_links_per_pair_; link_idx++) {
            int port_id = spine_offset + spine_id * spine_leaf_links_per_pair_ + link_idx;
            if (port_id >= cur_node->radix_) break;
            Buffer* next_buffer = cur_node->link_buffers_[port_id];
            if (next_buffer != nullptr) {
              for (int i = 0; i < param->vc_number; i++) {
                s.candidate_channels_.push_back(VCInfo(next_buffer, i));
              }
            }
          }
        }
      } else if (inter_group_sw_connect_) {
        int inter_offset = intra_sw_offset + num_switches_per_server_ - 1;
        for (int port_id = inter_offset; port_id < cur_node->radix_; port_id++) {
          Buffer* next_buffer = cur_node->link_buffers_[port_id];
          if (next_buffer != nullptr) {
            NodeID next_node_id = cur_node->link_nodes_[port_id];
            if (next_node_id.group_id == dest_group_id || next_node_id.group_id != cur_group_id) {
              for (int i = 0; i < param->vc_number; i++) {
                s.candidate_channels_.push_back(VCInfo(next_buffer, i));
              }
              break;
            }
          }
        }
        if (s.candidate_channels_.empty()) {
          for (int port_id = inter_offset; port_id < cur_node->radix_; port_id++) {
            Buffer* next_buffer = cur_node->link_buffers_[port_id];
            if (next_buffer != nullptr) {
              for (int i = 0; i < param->vc_number; i++) {
                s.candidate_channels_.push_back(VCInfo(next_buffer, i));
              }
              break;
            }
          }
        }
      }
    }
    if (s.candidate_channels_.empty()) {
      std::cerr << "Warning: Cannot route from group " << cur_group_id
                << " to group " << dest_group_id << ". Using fallback." << std::endl;
      for (int port_id = 0; port_id < cur_node->radix_; port_id++) {
        Buffer* next_buffer = cur_node->link_buffers_[port_id];
        if (next_buffer != nullptr) {
          for (int i = 0; i < param->vc_number; i++) {
            s.candidate_channels_.push_back(VCInfo(next_buffer, i));
          }
          break;
        }
      }
    }
  }
}

void NVSwitchSystem::min_routing(Packet& s) const {
  // MIN routing: add ALL candidate channels for packet spraying across all links
  Node* cur_node = get_node(s.head_trace().id);
  int cur_group_id = s.head_trace().id.group_id;
  int dest_group_id = s.destination_.group_id;
  int cur_node_id = s.head_trace().id.node_id;
  int dest_node_id = s.destination_.node_id;

  std::vector<int> gpu_port_base(num_switches_per_server_);
  for (int sw = 1; sw < num_switches_per_server_; sw++) {
    gpu_port_base[sw] = gpu_port_base[sw - 1] + links_per_switch_[sw - 1];
  }

  // If current node is a spine switch: route down to *all* leafs in dest group (包泼洒)
  if (num_spine_switches_ > 0 && cur_group_id == num_servers_per_super_node_) {
    for (int dest_sw_id = 0; dest_sw_id < num_switches_per_server_; dest_sw_id++) {
      int spine_port_base = (dest_group_id * num_switches_per_server_ + dest_sw_id) * spine_leaf_links_per_pair_;
      for (int link_idx = 0; link_idx < spine_leaf_links_per_pair_; link_idx++) {
        int spine_port = spine_port_base + link_idx;
        if (spine_port >= cur_node->radix_) break;
        Buffer* next_buffer = cur_node->link_buffers_[spine_port];
        if (next_buffer != nullptr) {
          for (int i = 0; i < param->vc_number; i++) {
            s.candidate_channels_.push_back(VCInfo(next_buffer, i));
          }
        }
      }
    }
    return;
  }

  // If current node is a GPU: add all ports to all switches (packet spraying)
  if (cur_node_id < num_gpus_per_server_) {
    for (int sw_id = 0; sw_id < num_switches_per_server_; sw_id++) {
      for (int k = 0; k < links_per_switch_[sw_id]; k++) {
        int gpu_port = gpu_port_base[sw_id] + k;
        if (gpu_port >= cur_node->radix_) break;
        Buffer* next_buffer = cur_node->link_buffers_[gpu_port];
        if (next_buffer != nullptr) {
          for (int i = 0; i < param->vc_number; i++) {
            s.candidate_channels_.push_back(VCInfo(next_buffer, i));
          }
        }
      }
    }
  }
  // If current node is a switch
  else {
    int switch_id = cur_node_id - num_gpus_per_server_;
    int n_links = links_per_switch_[switch_id];
    int gpu_ports_end = num_gpus_per_server_ * n_links;
    int intra_sw_offset = gpu_ports_end;

    if (cur_group_id == dest_group_id) {
      // 直连：本 Leaf 到目的 GPU 的所有端口
      for (int k = 0; k < n_links; k++) {
        int port_id = dest_node_id * n_links + k;
        if (port_id >= cur_node->radix_) break;
        Buffer* next_buffer = cur_node->link_buffers_[port_id];
        if (next_buffer != nullptr) {
          for (int i = 0; i < param->vc_number; i++) {
            s.candidate_channels_.push_back(VCInfo(next_buffer, i));
          }
        }
      }
      // 包泼洒：经其他 Leaf 再到目的 GPU
      for (int other_sw = 0; other_sw < num_switches_per_server_; other_sw++) {
        if (other_sw == switch_id) continue;
        int port_id = intra_sw_offset + (other_sw < switch_id ? other_sw : other_sw - 1);
        if (port_id >= cur_node->radix_) break;
        Buffer* next_buffer = cur_node->link_buffers_[port_id];
        if (next_buffer != nullptr) {
          for (int i = 0; i < param->vc_number; i++) {
            s.candidate_channels_.push_back(VCInfo(next_buffer, i));
          }
        }
      }
    }
    // Cross-group: via spine uplinks or direct leaf-leaf
    if (s.candidate_channels_.empty() && cur_group_id != dest_group_id) {
      if (num_spine_switches_ > 0) {
        int spine_offset = intra_sw_offset + num_switches_per_server_ - 1;
        for (int spine_id = 0; spine_id < num_spine_switches_; spine_id++) {
          for (int link_idx = 0; link_idx < spine_leaf_links_per_pair_; link_idx++) {
            int port_id = spine_offset + spine_id * spine_leaf_links_per_pair_ + link_idx;
            if (port_id >= cur_node->radix_) break;
            Buffer* next_buffer = cur_node->link_buffers_[port_id];
            if (next_buffer != nullptr) {
              for (int i = 0; i < param->vc_number; i++) {
                s.candidate_channels_.push_back(VCInfo(next_buffer, i));
              }
            }
          }
        }
      } else if (inter_group_sw_connect_) {
        int inter_offset = intra_sw_offset + num_switches_per_server_ - 1;
        for (int port_id = inter_offset; port_id < cur_node->radix_; port_id++) {
          Buffer* next_buffer = cur_node->link_buffers_[port_id];
          if (next_buffer != nullptr) {
            NodeID next_node_id = cur_node->link_nodes_[port_id];
            if (next_node_id.group_id == dest_group_id || next_node_id.group_id != cur_group_id) {
              for (int i = 0; i < param->vc_number; i++) {
                s.candidate_channels_.push_back(VCInfo(next_buffer, i));
              }
            }
          }
        }
        if (s.candidate_channels_.empty()) {
          for (int port_id = inter_offset; port_id < cur_node->radix_; port_id++) {
            Buffer* next_buffer = cur_node->link_buffers_[port_id];
            if (next_buffer != nullptr) {
              for (int i = 0; i < param->vc_number; i++) {
                s.candidate_channels_.push_back(VCInfo(next_buffer, i));
              }
              break;
            }
          }
        }
      }
    }
    if (s.candidate_channels_.empty()) {
      std::cerr << "Warning: min_routing fallback for switch " << cur_node_id << std::endl;
      for (int port_id = 0; port_id < cur_node->radix_; port_id++) {
        Buffer* next_buffer = cur_node->link_buffers_[port_id];
        if (next_buffer != nullptr) {
          for (int i = 0; i < param->vc_number; i++) {
            s.candidate_channels_.push_back(VCInfo(next_buffer, i));
          }
          break;
        }
      }
    }
  }
}
