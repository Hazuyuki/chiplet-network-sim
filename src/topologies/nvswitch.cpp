#include "nvswitch.h"
#include "traffic_manager.h"
#include <algorithm>
#include <random>

NVSwitchGroup::NVSwitchGroup(int num_gpus, int num_switches, int gpu_nvlink_ports, int leaf_switch_radix,
                             int vc_num, int buffer_size,
                             Channel gpu_switch_channel, Channel switch_switch_channel,
                             const std::vector<int>& links_per_switch)
    : Group(), num_gpus_(num_gpus), num_switches_(num_switches), gpu_nvlink_ports_(gpu_nvlink_ports),
      leaf_switch_radix_(leaf_switch_radix), links_per_switch_(links_per_switch),
      gpu_switch_channel_(gpu_switch_channel), switch_switch_channel_(switch_switch_channel) {
  num_nodes_ = num_gpus_ + num_switches_;
  num_cores_ = num_gpus_;  // Only GPUs are cores (can inject/receive packets)

  // Create GPU nodes: each GPU has gpu_nvlink_ports links to switches (packet spraying)
  for (int i = 0; i < num_gpus_; i++) {
    nodes_.push_back(new Node(gpu_nvlink_ports_, vc_num, buffer_size, gpu_switch_channel));
  }

  // Create Leaf NVSwitch nodes
  for (int i = 0; i < num_switches_; i++) {
    nodes_.push_back(new Node(leaf_switch_radix, vc_num, buffer_size, switch_switch_channel));
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
  
  num_nodes_ = num_groups_ * (num_gpus_per_group_ + num_switches_per_group_);
  num_cores_ = num_groups_ * num_gpus_per_group_;
  // Set base class num_groups_ for print_config()
  System::num_groups_ = num_groups_;

  // Create groups
  groups_.reserve(num_groups_);
  for (int group_id = 0; group_id < num_groups_; group_id++) {
    groups_.push_back(new NVSwitchGroup(num_gpus_per_group_, num_switches_per_group_,
                                       gpu_nvlink_ports_, leaf_switch_radix_,
                                       param->vc_number, param->buffer_size,
                                       gpu_switch_channel_, switch_switch_channel_,
                                       links_per_switch_));
    groups_[group_id]->set_group(this, group_id);
  }

  // Connect GPUs to switches
  connect_gpus_to_switches();
  
  // Connect switches
  connect_switches();

  print_config();
}

NVSwitchSystem::~NVSwitchSystem() {
  for (auto group : groups_) {
    delete group;
  }
  groups_.clear();
}

void NVSwitchSystem::read_config() {
  num_gpus_per_group_ = param->params_ptree.get<int>("Network.num_gpus_per_group", 8);
  num_switches_per_group_ = param->params_ptree.get<int>("Network.num_switches_per_group", 4);
  num_groups_ = param->params_ptree.get<int>("Network.num_groups", 1);
  gpu_nvlink_ports_ = param->params_ptree.get<int>("Network.gpu_nvlink_ports", 18);
  num_spine_switches_ = param->params_ptree.get<int>("Network.num_spine_switches", 0);

  switches_fully_connected_ = param->params_ptree.get<bool>("Network.switches_fully_connected", true);
  inter_group_sw_connect_ = param->params_ptree.get<bool>("Network.inter_group_sw_connect", false);

  algorithm_ = param->params_ptree.get<std::string>("Network.routing_algorithm", "direct");

  int gpu_switch_latency = param->params_ptree.get<int>("Network.gpu_switch_latency", 1);
  int switch_switch_latency = param->params_ptree.get<int>("Network.switch_switch_latency", 1);

  gpu_switch_channel_ = Channel(1, gpu_switch_latency);
  switch_switch_channel_ = Channel(1, switch_switch_latency);

  // Distribute gpu_nvlink_ports across switches (e.g. 18 ports, 4 switches -> 5,5,4,4)
  int base = gpu_nvlink_ports_ / num_switches_per_group_;
  int remainder = gpu_nvlink_ports_ % num_switches_per_group_;
  links_per_switch_.resize(num_switches_per_group_);
  for (int sw = 0; sw < num_switches_per_group_; sw++) {
    links_per_switch_[sw] = base + (sw < remainder ? 1 : 0);
  }

  // Leaf switch radix: GPU links + intra-group switch links + optional spine uplinks
  int max_gpu_ports = 0;
  for (int sw = 0; sw < num_switches_per_group_; sw++) {
    int gpu_ports = num_gpus_per_group_ * links_per_switch_[sw];
    if (gpu_ports > max_gpu_ports) max_gpu_ports = gpu_ports;
  }
  int intra_sw_ports = num_switches_per_group_ - 1;
  int spine_ports = num_spine_switches_;
  leaf_switch_radix_ = max_gpu_ports + intra_sw_ports + spine_ports;

  printf("NVSwitch Topology: %d groups, %d GPUs/group, %d Leaf switches/group, "
         "gpu_nvlink_ports=%d, leaf_switch_radix=%d, spine=%d\n",
         num_groups_, num_gpus_per_group_, num_switches_per_group_,
         gpu_nvlink_ports_, leaf_switch_radix_, num_spine_switches_);
}

void NVSwitchSystem::print_config() {
  System::print_config();
  std::cout << "NVSwitch parameters: " << std::endl;
  std::cout << "  num_groups: " << num_groups_ << std::endl;
  std::cout << "  num_gpus_per_group: " << num_gpus_per_group_ << std::endl;
  std::cout << "  num_switches_per_group: " << num_switches_per_group_ << std::endl;
  std::cout << "  gpu_nvlink_ports: " << gpu_nvlink_ports_ << std::endl;
  std::cout << "  leaf_switch_radix: " << leaf_switch_radix_ << std::endl;
  std::cout << "  num_spine_switches: " << num_spine_switches_ << std::endl;
  std::cout << "  switches_fully_connected: " << switches_fully_connected_ << std::endl;
  std::cout << "  inter_group_sw_connect: " << inter_group_sw_connect_ << std::endl;
  std::cout << "  routing_algorithm: " << algorithm_ << std::endl;
}

void NVSwitchSystem::connect_gpus_to_switches() {
  // GPU port layout: ports [gpu_port_base[sw], gpu_port_base[sw]+links_per_switch_[sw]) go to switch sw
  std::vector<int> gpu_port_base(num_switches_per_group_);
  for (int sw = 1; sw < num_switches_per_group_; sw++) {
    gpu_port_base[sw] = gpu_port_base[sw - 1] + links_per_switch_[sw - 1];
  }

  for (int group_id = 0; group_id < num_groups_; group_id++) {
    NVSwitchGroup* group = get_group(group_id);
    for (int gpu_id = 0; gpu_id < num_gpus_per_group_; gpu_id++) {
      Node* gpu = group->get_gpu(gpu_id);
      for (int sw_id = 0; sw_id < num_switches_per_group_; sw_id++) {
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
    for (int group_id = 0; group_id < num_groups_; group_id++) {
      NVSwitchGroup* group = get_group(group_id);
      for (int sw1_id = 0; sw1_id < num_switches_per_group_; sw1_id++) {
        Node* sw1 = group->get_nvswitch(sw1_id);
        int sw1_gpu_ports = num_gpus_per_group_ * links_per_switch_[sw1_id];
        int sw1_port_offset = sw1_gpu_ports;

        for (int sw2_id = sw1_id + 1; sw2_id < num_switches_per_group_; sw2_id++) {
          Node* sw2 = group->get_nvswitch(sw2_id);
          int sw2_gpu_ports = num_gpus_per_group_ * links_per_switch_[sw2_id];
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

  // Connect switches between groups (legacy, when no Spine)
  if (inter_group_sw_connect_ && num_groups_ > 1 && num_spine_switches_ == 0) {
    for (int group1_id = 0; group1_id < num_groups_; group1_id++) {
      for (int group2_id = group1_id + 1; group2_id < num_groups_; group2_id++) {
        NVSwitchGroup* group1 = get_group(group1_id);
        NVSwitchGroup* group2 = get_group(group2_id);
        for (int sw_id = 0; sw_id < num_switches_per_group_; sw_id++) {
          Node* sw1 = group1->get_nvswitch(sw_id);
          Node* sw2 = group2->get_nvswitch(sw_id);
          int inter_offset = num_gpus_per_group_ * links_per_switch_[sw_id] +
                            num_switches_per_group_ - 1;
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

  std::vector<int> gpu_port_base(num_switches_per_group_);
  for (int sw = 1; sw < num_switches_per_group_; sw++) {
    gpu_port_base[sw] = gpu_port_base[sw - 1] + links_per_switch_[sw - 1];
  }

  // If current node is a GPU
  if (cur_node_id < num_gpus_per_group_) {
    if (cur_group_id == dest_group_id) {
      int switch_id = dest_node_id % num_switches_per_group_;
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
      for (int sw_id = 0; sw_id < num_switches_per_group_; sw_id++) {
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
    int switch_id = cur_node_id - num_gpus_per_group_;
    int n_links = links_per_switch_[switch_id];
    int gpu_ports_end = num_gpus_per_group_ * n_links;
    int intra_sw_offset = gpu_ports_end;

    if (cur_group_id == dest_group_id) {
      for (int k = 0; k < n_links; k++) {
        int port_id = dest_node_id * n_links + k;
        if (port_id >= cur_node->radix_) break;
        Buffer* next_buffer = cur_node->link_buffers_[port_id];
        if (next_buffer != nullptr) {
          for (int i = 0; i < param->vc_number; i++) {
            s.candidate_channels_.push_back(VCInfo(next_buffer, i));
          }
          break;
        }
      }
    }
    if (s.candidate_channels_.empty() && cur_group_id != dest_group_id && inter_group_sw_connect_) {
      int inter_offset = intra_sw_offset + num_switches_per_group_ - 1;
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

  std::vector<int> gpu_port_base(num_switches_per_group_);
  for (int sw = 1; sw < num_switches_per_group_; sw++) {
    gpu_port_base[sw] = gpu_port_base[sw - 1] + links_per_switch_[sw - 1];
  }

  // If current node is a GPU: add all ports to all switches (packet spraying)
  if (cur_node_id < num_gpus_per_group_) {
    for (int sw_id = 0; sw_id < num_switches_per_group_; sw_id++) {
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
    int switch_id = cur_node_id - num_gpus_per_group_;
    int n_links = links_per_switch_[switch_id];
    int gpu_ports_end = num_gpus_per_group_ * n_links;
    int intra_sw_offset = gpu_ports_end;

    if (cur_group_id == dest_group_id) {
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
    }
    if (s.candidate_channels_.empty() && cur_group_id != dest_group_id && inter_group_sw_connect_) {
      int inter_offset = intra_sw_offset + num_switches_per_group_ - 1;
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
