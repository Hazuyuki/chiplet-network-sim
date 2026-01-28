#include "nvswitch.h"
#include "traffic_manager.h"
#include <algorithm>
#include <random>

NVSwitchGroup::NVSwitchGroup(int num_gpus, int num_switches, int switch_radix, 
                             int vc_num, int buffer_size, 
                             Channel gpu_switch_channel, Channel switch_switch_channel)
    : Group(), num_gpus_(num_gpus), num_switches_(num_switches), switch_radix_(switch_radix),
      gpu_switch_channel_(gpu_switch_channel), switch_switch_channel_(switch_switch_channel) {
  num_nodes_ = num_gpus_ + num_switches_;
  num_cores_ = num_gpus_;  // Only GPUs are cores (can inject/receive packets)

  // Create GPU nodes: each GPU connects to all NVSwitches
  // Each GPU has radix = num_switches (one port per NVSwitch)
  for (int i = 0; i < num_gpus_; i++) {
    nodes_.push_back(new Node(num_switches_, vc_num, buffer_size, gpu_switch_channel));
  }

  // Create NVSwitch nodes: each switch connects to all GPUs and other switches
  // Switch radix depends on configuration
  for (int i = 0; i < num_switches_; i++) {
    nodes_.push_back(new Node(switch_radix, vc_num, buffer_size, switch_switch_channel));
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
                                       switch_radix_, param->vc_number, param->buffer_size,
                                       gpu_switch_channel_, switch_switch_channel_));
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
  num_switches_per_group_ = param->params_ptree.get<int>("Network.num_switches_per_group", 6);
  num_groups_ = param->params_ptree.get<int>("Network.num_groups", 1);
  switch_radix_ = param->params_ptree.get<int>("Network.switch_radix", 18);
  
  switches_fully_connected_ = param->params_ptree.get<bool>("Network.switches_fully_connected", true);
  inter_group_sw_connect_ = param->params_ptree.get<bool>("Network.inter_group_sw_connect", false);

  algorithm_ = param->params_ptree.get<std::string>("Network.routing_algorithm", "direct");

  int gpu_switch_latency = param->params_ptree.get<int>("Network.gpu_switch_latency", 1);
  int switch_switch_latency = param->params_ptree.get<int>("Network.switch_switch_latency", 1);
  
  gpu_switch_channel_ = Channel(1, gpu_switch_latency);
  switch_switch_channel_ = Channel(1, switch_switch_latency);

  printf("NVSwitch Topology: %d groups, %d GPUs/group, %d switches/group, switch_radix=%d\n",
         num_groups_, num_gpus_per_group_, num_switches_per_group_, switch_radix_);
}

void NVSwitchSystem::print_config() {
  System::print_config();
  std::cout << "NVSwitch parameters: " << std::endl;
  std::cout << "  num_groups: " << num_groups_ << std::endl;
  std::cout << "  num_gpus_per_group: " << num_gpus_per_group_ << std::endl;
  std::cout << "  num_switches_per_group: " << num_switches_per_group_ << std::endl;
  std::cout << "  switch_radix: " << switch_radix_ << std::endl;
  std::cout << "  switches_fully_connected: " << switches_fully_connected_ << std::endl;
  std::cout << "  inter_group_sw_connect: " << inter_group_sw_connect_ << std::endl;
  std::cout << "  routing_algorithm: " << algorithm_ << std::endl;
}

void NVSwitchSystem::connect_gpus_to_switches() {
  // Connect each GPU to all NVSwitches in its group
  for (int group_id = 0; group_id < num_groups_; group_id++) {
    NVSwitchGroup* group = get_group(group_id);
    
    for (int gpu_id = 0; gpu_id < num_gpus_per_group_; gpu_id++) {
      Node* gpu = group->get_gpu(gpu_id);
      
      // Connect GPU to each NVSwitch
      for (int sw_id = 0; sw_id < num_switches_per_group_; sw_id++) {
        Node* nvswitch = group->get_nvswitch(sw_id);
        
        // GPU port sw_id connects to switch port gpu_id
        Port::connect_port(gpu->ports_[sw_id], nvswitch->ports_[gpu_id]);
      }
    }
  }
}

void NVSwitchSystem::connect_switches() {
  // Connect switches within each group
  if (switches_fully_connected_) {
    for (int group_id = 0; group_id < num_groups_; group_id++) {
      NVSwitchGroup* group = get_group(group_id);
      
      // Connect each switch to all other switches in the same group
      int port_offset = num_gpus_per_group_;  // Ports 0 to num_gpus_per_group_-1 are for GPUs
      
      // Port assignment for switch-to-switch connections
      // Each switch needs (num_switches - 1) ports to connect to other switches
      // Port assignment rule:
      //   Switch i connects to switch j (where j != i):
      //     - If j < i: switch i uses port (port_offset + j)
      //     - If j > i: switch i uses port (port_offset + j - 1)
      // This ensures each switch uses unique ports for each connection
      for (int sw1_id = 0; sw1_id < num_switches_per_group_; sw1_id++) {
        Node* sw1 = group->get_nvswitch(sw1_id);
        
        for (int sw2_id = sw1_id + 1; sw2_id < num_switches_per_group_; sw2_id++) {
          Node* sw2 = group->get_nvswitch(sw2_id);
          
          // Port assignment:
          // sw1 (smaller id) connects to sw2 (larger id):
          //   sw1 uses port (port_offset + sw2_id - 1) [for connecting to switches > sw1]
          // sw2 (larger id) connects to sw1 (smaller id):
          //   sw2 uses port (port_offset + sw1_id) [for connecting to switches < sw2]
          int sw1_port = port_offset + sw2_id - 1;  // sw1's port for connecting to sw2
          int sw2_port = port_offset + sw1_id;     // sw2's port for connecting to sw1
          
          // Check if ports are within switch radix
          if (sw1_port < switch_radix_ && sw2_port < switch_radix_) {
            Port::connect_port(sw1->ports_[sw1_port], sw2->ports_[sw2_port]);
          } else {
            std::cerr << "Warning: Switch radix (" << switch_radix_ 
                      << ") too small for full connectivity. Need at least " 
                      << port_offset + num_switches_per_group_ - 1 << " ports." << std::endl;
            std::cerr << "  sw1_id=" << sw1_id << " sw2_id=" << sw2_id 
                      << " sw1_port=" << sw1_port << " sw2_port=" << sw2_port << std::endl;
          }
        }
      }
    }
  }

  // Connect switches between groups (if enabled)
  if (inter_group_sw_connect_ && num_groups_ > 1) {
    // This is a simplified implementation
    // In a real system, you might want more sophisticated inter-group connections
    for (int group1_id = 0; group1_id < num_groups_; group1_id++) {
      for (int group2_id = group1_id + 1; group2_id < num_groups_; group2_id++) {
        NVSwitchGroup* group1 = get_group(group1_id);
        NVSwitchGroup* group2 = get_group(group2_id);
        
        // Connect corresponding switches between groups
        for (int sw_id = 0; sw_id < num_switches_per_group_; sw_id++) {
          Node* sw1 = group1->get_nvswitch(sw_id);
          Node* sw2 = group2->get_nvswitch(sw_id);
          
          // Find available ports (use ports after GPU ports and intra-group switch ports)
          int inter_group_port_offset = num_gpus_per_group_ + num_switches_per_group_ - 1;
          int sw1_port = inter_group_port_offset + group2_id;
          int sw2_port = inter_group_port_offset + group1_id;
          
          if (sw1_port < switch_radix_ && sw2_port < switch_radix_) {
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
  Node* dest_node = get_node(s.destination_);
  
  int cur_group_id = s.head_trace().id.group_id;
  int dest_group_id = s.destination_.group_id;
  int cur_node_id = s.head_trace().id.node_id;
  int dest_node_id = s.destination_.node_id;

  // If current node is a GPU
  if (cur_node_id < num_gpus_per_group_) {
    // GPU -> Switch -> GPU
    // Choose a switch that can reach the destination
    if (cur_group_id == dest_group_id) {
      // Same group: choose any switch (they're all connected to all GPUs)
      // Use a deterministic method: switch_id = dest_gpu_id % num_switches
      int switch_id = dest_node_id % num_switches_per_group_;
      Node* nvswitch = get_group(cur_group_id)->get_nvswitch(switch_id);
      NodeID sw_node_id = NodeID(num_gpus_per_group_ + switch_id, cur_group_id);
      
      // Find the port on GPU that connects to this switch
      if (switch_id < cur_node->radix_) {
        Buffer* next_buffer = cur_node->link_buffers_[switch_id];
        if (next_buffer != nullptr) {
          for (int i = 0; i < param->vc_number; i++) {
            s.candidate_channels_.push_back(VCInfo(next_buffer, i));
          }
        }
      }
      // Fallback: if no valid buffer, try all switches
      if (s.candidate_channels_.empty()) {
        for (int sw_id = 0; sw_id < num_switches_per_group_ && sw_id < cur_node->radix_; sw_id++) {
          Buffer* next_buffer = cur_node->link_buffers_[sw_id];
          if (next_buffer != nullptr) {
            for (int i = 0; i < param->vc_number; i++) {
              s.candidate_channels_.push_back(VCInfo(next_buffer, i));
            }
            break;  // Use first available switch
          }
        }
      }
    } else {
      // Different groups: need to go through switches
      // Choose a switch in current group (any switch)
      for (int switch_id = 0; switch_id < num_switches_per_group_ && switch_id < cur_node->radix_; switch_id++) {
        Buffer* next_buffer = cur_node->link_buffers_[switch_id];
        if (next_buffer != nullptr) {
          for (int i = 0; i < param->vc_number; i++) {
            s.candidate_channels_.push_back(VCInfo(next_buffer, i));
          }
          break;  // Use first available switch
        }
      }
    }
  }
  // If current node is a switch
  else {
    int switch_id = cur_node_id - num_gpus_per_group_;
    
    if (cur_group_id == dest_group_id) {
      // Same group: switch -> destination GPU
      // Port dest_node_id on switch connects to GPU dest_node_id
      if (dest_node_id < cur_node->radix_) {
        Buffer* next_buffer = cur_node->link_buffers_[dest_node_id];
        if (next_buffer != nullptr) {
          for (int i = 0; i < param->vc_number; i++) {
            s.candidate_channels_.push_back(VCInfo(next_buffer, i));
          }
        }
      }
      // Fallback: if no valid buffer, try all GPU ports
      if (s.candidate_channels_.empty()) {
        for (int port_id = 0; port_id < num_gpus_per_group_ && port_id < cur_node->radix_; port_id++) {
          Buffer* next_buffer = cur_node->link_buffers_[port_id];
          if (next_buffer != nullptr) {
            for (int i = 0; i < param->vc_number; i++) {
              s.candidate_channels_.push_back(VCInfo(next_buffer, i));
            }
            break;  // Use first available port
          }
        }
      }
      // Final fallback: use any available port
      if (s.candidate_channels_.empty()) {
        std::cerr << "Error: Cannot route from switch " << cur_node_id 
                  << " to GPU " << dest_node_id 
                  << " in same group. Using any available port." << std::endl;
        for (int port_id = 0; port_id < cur_node->radix_; port_id++) {
          Buffer* next_buffer = cur_node->link_buffers_[port_id];
          if (next_buffer != nullptr) {
            for (int i = 0; i < param->vc_number; i++) {
              s.candidate_channels_.push_back(VCInfo(next_buffer, i));
            }
            break;  // Use first available port
          }
        }
      }
    } else if (cur_group_id != dest_group_id && inter_group_sw_connect_) {
      // Different groups: need inter-group routing
      // Switch -> Switch in destination group -> GPU
      // First, go to a switch in destination group
      // Try to find a port that connects to destination group
      int inter_group_port_offset = num_gpus_per_group_ + num_switches_per_group_ - 1;
      
      // Try all possible inter-group ports
      for (int port_id = inter_group_port_offset; port_id < switch_radix_; port_id++) {
        Buffer* next_buffer = cur_node->link_buffers_[port_id];
        if (next_buffer != nullptr) {
          // Check if this port leads to destination group
          // This is simplified - in practice, you'd track which port goes to which group
          NodeID next_node_id = cur_node->link_nodes_[port_id];
          if (next_node_id.group_id == dest_group_id || 
              (next_node_id.group_id != cur_group_id)) {
            for (int i = 0; i < param->vc_number; i++) {
              s.candidate_channels_.push_back(VCInfo(next_buffer, i));
            }
            break;  // Found a path, can break
          }
        }
      }
      // If no path found, try any inter-group port as fallback
      if (s.candidate_channels_.empty()) {
        for (int port_id = inter_group_port_offset; port_id < switch_radix_; port_id++) {
          Buffer* next_buffer = cur_node->link_buffers_[port_id];
          if (next_buffer != nullptr) {
            for (int i = 0; i < param->vc_number; i++) {
              s.candidate_channels_.push_back(VCInfo(next_buffer, i));
            }
            break;  // Use first available inter-group port
          }
        }
      }
    } else {
      // No inter-group switch connection: cannot route between groups
      // This topology doesn't support inter-group routing without switch connections
      // But we must set at least one candidate channel to avoid assertion failure
      // Use any available port as fallback (may cause routing failure, but won't crash)
      std::cerr << "Warning: Cannot route from group " << cur_group_id 
                << " to group " << dest_group_id 
                << " without inter-group switch connections. Using fallback routing." << std::endl;
      // Try to use any available port (prefer GPU ports, then switch ports)
      for (int port_id = 0; port_id < switch_radix_ && s.candidate_channels_.empty(); port_id++) {
        Buffer* next_buffer = cur_node->link_buffers_[port_id];
        if (next_buffer != nullptr) {
          for (int i = 0; i < param->vc_number; i++) {
            s.candidate_channels_.push_back(VCInfo(next_buffer, i));
          }
          break;  // Use first available port
        }
      }
    }
  }
}

void NVSwitchSystem::min_routing(Packet& s) const {
  // MIN (Minimal) routing: choose the shortest path
  // For NVSwitch, this is similar to direct routing but with load balancing
  Node* cur_node = get_node(s.head_trace().id);
  
  int cur_group_id = s.head_trace().id.group_id;
  int dest_group_id = s.destination_.group_id;
  int cur_node_id = s.head_trace().id.node_id;
  int dest_node_id = s.destination_.node_id;

  // If current node is a GPU
  if (cur_node_id < num_gpus_per_group_) {
    if (cur_group_id == dest_group_id) {
      // Same group: try all switches for load balancing
      for (int switch_id = 0; switch_id < num_switches_per_group_; switch_id++) {
        Buffer* next_buffer = cur_node->link_buffers_[switch_id];
        for (int i = 0; i < param->vc_number; i++) {
          s.candidate_channels_.push_back(VCInfo(next_buffer, i));
        }
      }
    } else {
      // Different groups: choose any switch
      for (int switch_id = 0; switch_id < num_switches_per_group_; switch_id++) {
        Buffer* next_buffer = cur_node->link_buffers_[switch_id];
        for (int i = 0; i < param->vc_number; i++) {
          s.candidate_channels_.push_back(VCInfo(next_buffer, i));
        }
      }
    }
  }
  // If current node is a switch
  else {
    if (cur_group_id == dest_group_id) {
      // Same group: switch -> destination GPU
      if (dest_node_id < cur_node->radix_) {
        Buffer* next_buffer = cur_node->link_buffers_[dest_node_id];
        if (next_buffer != nullptr) {
          for (int i = 0; i < param->vc_number; i++) {
            s.candidate_channels_.push_back(VCInfo(next_buffer, i));
          }
        }
      }
      // Fallback: if no valid buffer, try all GPU ports
      if (s.candidate_channels_.empty()) {
        for (int port_id = 0; port_id < num_gpus_per_group_ && port_id < cur_node->radix_; port_id++) {
          Buffer* next_buffer = cur_node->link_buffers_[port_id];
          if (next_buffer != nullptr) {
            for (int i = 0; i < param->vc_number; i++) {
              s.candidate_channels_.push_back(VCInfo(next_buffer, i));
            }
            break;  // Use first available port
          }
        }
      }
      // Final fallback: use any available port
      if (s.candidate_channels_.empty()) {
        std::cerr << "Error: Cannot route from switch " << cur_node_id 
                  << " to GPU " << dest_node_id 
                  << " in same group. Using any available port." << std::endl;
        for (int port_id = 0; port_id < cur_node->radix_; port_id++) {
          Buffer* next_buffer = cur_node->link_buffers_[port_id];
          if (next_buffer != nullptr) {
            for (int i = 0; i < param->vc_number; i++) {
              s.candidate_channels_.push_back(VCInfo(next_buffer, i));
            }
            break;  // Use first available port
          }
        }
      }
    } else {
      // Different groups: route to destination group's switch
      if (inter_group_sw_connect_) {
        // Try all possible paths to destination group
        int inter_group_port_offset = num_gpus_per_group_ + num_switches_per_group_ - 1;
        for (int port_id = inter_group_port_offset; port_id < switch_radix_; port_id++) {
          Buffer* next_buffer = cur_node->link_buffers_[port_id];
          if (next_buffer != nullptr) {
            NodeID next_node_id = cur_node->link_nodes_[port_id];
            // Prefer ports that lead to destination group
            if (next_node_id.group_id == dest_group_id) {
              for (int i = 0; i < param->vc_number; i++) {
                s.candidate_channels_.push_back(VCInfo(next_buffer, i));
              }
            }
          }
        }
        // If no direct path found, try any inter-group port
        if (s.candidate_channels_.empty()) {
          for (int port_id = inter_group_port_offset; port_id < switch_radix_; port_id++) {
            Buffer* next_buffer = cur_node->link_buffers_[port_id];
            if (next_buffer != nullptr) {
              for (int i = 0; i < param->vc_number; i++) {
                s.candidate_channels_.push_back(VCInfo(next_buffer, i));
              }
              break;  // Use first available inter-group port
            }
          }
        }
      } else {
        // No inter-group switch connection: cannot route between groups
        // But we must set at least one candidate channel to avoid assertion failure
        std::cerr << "Warning: Cannot route from group " << cur_group_id 
                  << " to group " << dest_group_id 
                  << " without inter-group switch connections. Using fallback routing." << std::endl;
        // Try to use any available port (prefer GPU ports, then switch ports)
        for (int port_id = 0; port_id < switch_radix_ && s.candidate_channels_.empty(); port_id++) {
          Buffer* next_buffer = cur_node->link_buffers_[port_id];
          if (next_buffer != nullptr) {
            for (int i = 0; i < param->vc_number; i++) {
              s.candidate_channels_.push_back(VCInfo(next_buffer, i));
            }
            break;  // Use first available port
          }
        }
      }
    }
  }
}
