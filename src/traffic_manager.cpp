#include "traffic_manager.h"
#include "railx_2d_hyperx.h"

#include "boost/dynamic_bitset.hpp"

TrafficManager::TrafficManager() {
  injection_rate_ = param->start_injection;
  traffic_ = param->traffic;
  traffic_scale_ = param->traffic_scale;
  if (traffic_scale_ == 0) traffic_scale_ = network->num_cores_;
  message_length_ = param->packet_length;
  if (traffic_ == "sd_trace") {
    trace_.open(param->trace_file, std::fstream::in);
    std::cout << "Trace file is read!" << std::endl;
    std::string head;
    std::getline(trace_, head);
  } else if (traffic_ == "netrace") {
    CTX = new nt_context_t();
    nt_open_trfile(CTX, param->netrace_file.c_str());
    nt_disable_dependencies(CTX);
    nt_print_trheader(CTX);
  }
  std::filesystem::path dirPath = std::filesystem::path(param->output_file).parent_path();
  if (!dirPath.empty() && !std::filesystem::exists(dirPath)) 
    std::filesystem::create_directories(dirPath);
  output_.open(param->output_file, std::fstream::out);
  dirPath = std::filesystem::path(param->log_file).parent_path();
  if (!dirPath.empty() && !std::filesystem::exists(dirPath)) 
    std::filesystem::create_directories(dirPath);
  log_.open(param->log_file, std::fstream::out);

  pkt_for_injection_ = 0;
  cycles = 0;
  is_done = false;
  data_size = 1;
  throughput = 0;
  
  // Load alltoall traffic matrix if specified
  if (traffic_ == "collective_alltoall" && !param->alltoall_traffic_matrix_file.empty()) {
    load_alltoall_matrix(param->alltoall_traffic_matrix_file);
  }
  
  // statistics
  time_ = std::chrono::system_clock::now();
  all_message_num_.store(0);
  message_arrived_.store(0);
  message_timeout_.store(0);
  total_cycles_.store(0);
  total_hops_.store(0);
  total_internal_hops_.store(0);
  total_external_hops_.store(0);
  total_specific_hops_.store(0);
  total_other_hops_.store(0);
#ifdef DEBUG
  for (auto& group : network->groups_) {
    for (auto& node : group->nodes_) {
      for (auto& buf : node->in_buffers_) {
        traffic_map_[buf].store(0);
      }
    }
  }
#endif  // DEBUG
}

TrafficManager::~TrafficManager() {
  if (traffic_ == "sd_trace") {
    trace_.close();
  } else if (traffic_ == "netrace") {
    delete CTX;
  }
  output_.close();
  log_.close();
}

void TrafficManager::reset() {
  pkt_for_injection_ = 0;
  cycles = 0;
  is_done = false;
  data_size = 1;
  throughput = 0;
  time_ = std::chrono::system_clock::now();
  all_message_num_.store(0);
  message_arrived_.store(0);
  message_timeout_.store(0);
  total_cycles_.store(0);
  total_hops_.store(0);
  total_internal_hops_.store(0);
  total_external_hops_.store(0);
  total_specific_hops_.store(0);
  total_other_hops_.store(0);
}

void TrafficManager::print_statistics() {
  std::chrono::duration<double> elapsed_seconds = std::chrono::system_clock::now() - time_;
  double average_total_hops = (double)TM->total_hops_ / TM->message_arrived_;
  double average_internal_hops = (double)TM->total_internal_hops_ / TM->message_arrived_;
  double average_external_hops = (double)TM->total_external_hops_ / TM->message_arrived_;
  double average_specific_hops = (double)TM->total_specific_hops_ / TM->message_arrived_;
  double average_other_hops = (double)TM->total_other_hops_ / TM->message_arrived_;
  std::cout << std::endl
            << "Time elapsed: " << elapsed_seconds.count() << "s" << std::endl
            << "Injection rate:" << injection_rate_ << " flits/(node*cycle)"
            << "  Injected: " << all_message_num_ << "  Arrived: " << message_arrived_
            << "  Timeout: " << message_timeout_ << std::endl
            << "Average latency: " << average_latency()
            << "  Average receiving rate: " << receiving_rate() << std::endl
            << "Total Hops: " << average_total_hops
            << "  Internal Hops: " << average_internal_hops << "  External Hops: " << average_external_hops
            << "  Specific Hops: " << average_specific_hops << "  Other Hops: " << average_other_hops
            << std::endl;
  output_ << injection_rate_ << "," << average_latency() << ","
          << receiving_rate() << std::endl;
#ifdef DEBUG
  // int max = 0;
  // Buffer* max_buf = nullptr;
  // for (auto& i : traffic_map_) {
  //   if (i.second > max) {
  //     max = i.second;
  //     max_buf = i.first;
  //   }
  // }
  // std::cout << "Max traffic: " << max << " at " << max_buf->node_->id_ << std::endl;
#endif  // DEBUG
}

/**
 * Load alltoall traffic matrix from CSV file
 * Format: src,dest,data_size (one entry per line, header optional)
 * Example:
 *   src,dest,data_size
 *   0,1,1024
 *   0,2,2048
 *   1,0,512
 *   ...
 */
void TrafficManager::load_alltoall_matrix(const std::string& filename) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Cannot open alltoall traffic matrix file: " << filename << std::endl;
    return;
  }
  
  // Initialize matrix with zeros
  int num_nodes = traffic_scale_;
  traffic_matrix_.resize(num_nodes, std::vector<uint64_t>(num_nodes, 0));
  
  std::string line;
  bool has_header = false;
  
  while (std::getline(file, line)) {
    // Skip empty lines
    if (line.empty()) continue;
    
    // Check if this is a header line
    if (line.find("src") != std::string::npos || line.find("dest") != std::string::npos) {
      has_header = true;
      continue;
    }
    
    // Parse: src,dest,data_size
    std::istringstream iss(line);
    int src, dest;
    uint64_t data_size_val;
    char comma1, comma2;
    
    if (iss >> src >> comma1 >> dest >> comma2 >> data_size_val) {
      if (src >= 0 && src < num_nodes && dest >= 0 && dest < num_nodes) {
        traffic_matrix_[src][dest] = data_size_val;
        alltoall_total_flits_ += data_size_val;
      } else {
        std::cerr << "Warning: Invalid src/dest in line: " << line << std::endl;
      }
    } else {
      std::cerr << "Warning: Failed to parse line: " << line << std::endl;
    }
  }
  
  file.close();
  alltoall_matrix_loaded_ = true;
  
  std::cout << "=== Alltoall Traffic Matrix Loaded ===" << std::endl;
  std::cout << "File: " << filename << std::endl;
  std::cout << "Nodes: " << num_nodes << std::endl;
  std::cout << "Total flits to transfer: " << alltoall_total_flits_ << std::endl;
}

/**
 * collective_alltoall: All-to-All collective communication
 * 
 * Each node sends data to all other nodes according to the traffic matrix.
 * The traffic matrix specifies the amount of data (in flits) to send from src to dest.
 * 
 * Algorithm (stage-based, similar to collective_ring_all_reduce):
 * - Divide into multiple stages
 * - Each stage injects packets for all pairs with remaining data
 * - Wait for network to clear between stages
 * - Complete when all pairs have sent their data
 * 
 * Throughput calculation:
 * - For alltoall, each node sends to (n-1) other nodes
 * - Total data = sum of all traffic matrix entries
 * - Throughput = total_data / (cycles * num_nodes)
 */
void TrafficManager::collective_alltoall(std::vector<Packet*>& packets) {
  static bool initialized = false;
  static std::vector<std::vector<uint64_t>> remaining_flits;  // [src][dest] = remaining flits
  static uint64_t total_transferred = 0;
  static int stage = 0;
  
  // Initialize on first call
  if (!initialized) {
    if (!alltoall_matrix_loaded_) {
      std::cerr << "Error: Alltoall traffic matrix not loaded!" << std::endl;
      is_done = true;
      return;
    }
    remaining_flits = traffic_matrix_;
    initialized = true;
    std::cout << "Starting collective_alltoall with " << alltoall_total_flits_ << " total flits" << std::endl;
  }
  
  // Only inject when network is empty (same as collective_ring_all_reduce)
  if (packets.size() == 0) {
    int num_nodes = traffic_scale_;
    
    // Calculate packet length (use buffer_size/2 like ring_all_reduce)
    int packet_length = std::min(param->buffer_size / 2, message_length_);
    if (packet_length < 1) packet_length = 1;
    
    // Inject one packet per (src, dest) pair per stage (like ring_all_reduce injects per GPU)
    int packets_injected = 0;
    
    for (int src = 0; src < num_nodes; src++) {
      for (int dest = 0; dest < num_nodes; dest++) {
        // Skip self-communication
        if (src == dest) continue;
        
        // Check if this pair has remaining data
        if (remaining_flits[src][dest] > 0) {
          // Calculate packet length for this packet
          int pkt_len = std::min((uint64_t)packet_length, remaining_flits[src][dest]);
          
          // Create and inject packet
          NodeID src_id = network->int_to_nodeid(src);
          NodeID dest_id = network->int_to_nodeid(dest);
          packets.push_back(new Packet(src_id, dest_id, pkt_len));
          
          remaining_flits[src][dest] -= pkt_len;
          total_transferred += pkt_len;
          all_message_num_ += pkt_len;
          packets_injected++;
        }
      }
    }
    
    if (packets_injected > 0) {
      stage++;
    }
  }
  
  // Check if all transfers are complete
  bool all_done = true;
  int num_nodes = traffic_scale_;
  for (int src = 0; src < num_nodes && all_done; src++) {
    for (int dest = 0; dest < num_nodes; dest++) {
      if (src != dest && remaining_flits[src][dest] > 0) {
        all_done = false;
        break;
      }
    }
  }
  
  if (all_done && packets.size() == 0) {
    is_done = true;
    auto end_time = std::chrono::system_clock::now();
    std::chrono::duration<double> elapsed = end_time - time_;
    
    // Calculate throughput: total flits transferred / (cycles * num_nodes)
    throughput = (double)total_transferred / (cycles * traffic_scale_);
    
    std::cout << "=== Alltoall Complete ===" << std::endl;
    std::cout << "Time elapsed: " << elapsed.count() << "s" << std::endl;
    std::cout << "Total flits transferred: " << total_transferred << std::endl;
    std::cout << "Cycles: " << cycles << std::endl;
    std::cout << "Throughput: " << throughput << " flits/(node*cycle)" << std::endl;
    output_ << injection_rate_ << "," << throughput << std::endl;
  }
}

void TrafficManager::print_collective_statistics() {
  std::chrono::duration<double> elapsed_seconds = std::chrono::system_clock::now() - time_;
  std::cout << std::endl
            << "Time elapsed: " << elapsed_seconds.count() << "s" << std::endl
            << "Data size: " << data_size << " flits"
            << "  Cycles: " << cycles << std::endl
            << "Throughput: " << throughput << " flits/(node*cycle)" << std::endl;
  output_ << injection_rate_ << "," << throughput << std::endl;
}

void TrafficManager::genMes(std::vector<Packet*>& packets, uint64_t cyc) {
  cycles++;
  if (traffic_ == "collective_allreduce_torus") {
    allreduce_torus(packets);
    return;
  }
  else if (traffic_ == "collective_ring_all_reduce") {
    collective_ring_all_reduce(packets);
    return;
  }
  else if (traffic_ == "collective_alltoall") {
    collective_alltoall(packets);
    return;
  }
  else if (traffic_ == "torus_all_reduce") {
    torus_all_reduce_mess(packets);
    return;
  } else if (traffic_ == "torus_hirechical_reduce") {
    torus_hirechical_reduce_mess(packets);
    return;
  } else if (traffic_ == "ring_all_reduce") {
    ring_all_reduce_mess(packets);
    return;
  } else if (traffic_ == "ring_all_reduce_bi") {
    ring_all_reduce_bi_mess(packets);
    return;
  } else if (traffic_ == "hierarchical_all_reduce") {
    hierarchical_all_reduce_mess(packets, cyc);
    return;
  } else if (traffic_ == "netrace") {
    netrace(packets, cyc);
    return;
  }
  for (pkt_for_injection_ += message_per_cycle(); pkt_for_injection_ >= 1; pkt_for_injection_--) {
    Packet* mess;
    if (traffic_ == "test")
      mess = new Packet(NodeID(12, 60), NodeID(7, 36), message_length_);
    else if (traffic_ == "uniform")
      mess = uniform_mess();
    else if (traffic_ == "single_flow")
      mess = single_flow_mess();
    else if (traffic_ == "intra_group_uniform")
      mess = intra_group_uniform_mess();
    else if (traffic_ == "inter_group_uniform")
      mess = inter_group_uniform_mess();
    else if (traffic_ == "hotspot")
      mess = hotspot_mess();
    else if (traffic_ == "bitcomplement")
      mess = bitcomplement_mess();
    else if (traffic_ == "bitreverse")
      mess = bitreverse_mess();
    else if (traffic_ == "bitshuffle")
      mess = bitshuffle_mess();
    else if (traffic_ == "bittranspose")
      mess = bittranspose_mess();
    else if (traffic_ == "adversarial")
      mess = adversarial_mess();
    else if (traffic_ == "sd_traces")
      mess = sd_trace_mess();
    else
      std::cerr << "Unknown traffic pattern!" << std::endl;
    packets.push_back(mess);
    all_message_num_++;
  }
}

Packet* TrafficManager::uniform_mess() {
  int src, dest;
  while (true) {
    src = gen() % traffic_scale_;
    dest = gen() % traffic_scale_;
    if (dest != src) break;
  }
  return new Packet(network->int_to_nodeid(src), network->int_to_nodeid(dest), message_length_);
}

Packet* TrafficManager::single_flow_mess() {
  // 单发单收：固定 0 -> single_flow_dest，traffic_scale 应为 1 使注入率表示该单流速率
  return new Packet(network->int_to_nodeid(0), network->int_to_nodeid(param->single_flow_dest),
                    message_length_);
}

Packet* TrafficManager::intra_group_uniform_mess() {
  int src, dest;
  while (true) {
    src = gen() % traffic_scale_;
    dest = gen() % traffic_scale_;
    if (dest != src) break;
  }
  return new Packet(network->int_to_nodeid(src), network->int_to_nodeid(dest), message_length_);
}

Packet* TrafficManager::inter_group_uniform_mess() {
  // 多流跨 node：源和目的必在不同 group，所有流量经 spine，用于验证无阻塞
  int num_groups = network->num_groups_;
  if (num_groups < 2) return uniform_mess();
  int cores_per_group = traffic_scale_ / num_groups;
  int src = gen() % traffic_scale_;
  int group_src = src / cores_per_group;
  int other_group = (group_src + 1 + gen() % (num_groups - 1)) % num_groups;
  int dest = other_group * cores_per_group + gen() % cores_per_group;
  return new Packet(network->int_to_nodeid(src), network->int_to_nodeid(dest), message_length_);
}

Packet* TrafficManager::hotspot_mess() {
  int src, dest;
  int node_per_WG = traffic_scale_ / 4;
  int WG1, WG2;
  while (true) {
    WG1 = gen() % 4 * 10;
    WG2 = gen() % 4 * 10;
    src = (WG1 * node_per_WG + gen() % node_per_WG) % traffic_scale_;
    dest = (WG2 * node_per_WG + gen() % node_per_WG) % traffic_scale_;
    if (src != dest) break;
  }
  return new Packet(network->int_to_nodeid(src), network->int_to_nodeid(dest), message_length_);
}

Packet* TrafficManager::bitcomplement_mess() {
  int src, dest;
  int bits = (int)floor(log2(traffic_scale_));
  while (true) {
    src = gen() % traffic_scale_;
    boost::dynamic_bitset<> src_binary(bits, src);
    boost::dynamic_bitset<> dest_binary = ~src_binary;
    dest = dest_binary.to_ulong() % traffic_scale_;
    if (dest != src) break;
  }
  return new Packet(network->int_to_nodeid(src), network->int_to_nodeid(dest), message_length_);
}

Packet* TrafficManager::bitreverse_mess() {
  int src, dest;
  int bits = (int)floor(log2(traffic_scale_));
  while (true) {
    src = gen() % traffic_scale_;
    boost::dynamic_bitset<> src_binary(bits, src);
    boost::dynamic_bitset<> dest_binary(bits);
    for (int i = 0; i < bits; ++i) {
      dest_binary[i] = src_binary[bits - 1 - i];
    }
    dest = dest_binary.to_ulong() % traffic_scale_;
    if (dest != src) break;
  }
  return new Packet(network->int_to_nodeid(src), network->int_to_nodeid(dest), message_length_);
}

Packet* TrafficManager::bitshuffle_mess() {
  int src, dest;
  int bits = (int)floor(log2(traffic_scale_));
  while (true) {
    src = gen() % traffic_scale_;
    boost::dynamic_bitset<> src_binary(bits, src);
    bool last_bit = src_binary[bits - 1];
    boost::dynamic_bitset<> dest_binary = src_binary << 1;
    dest_binary[0] = last_bit;
    dest = dest_binary.to_ulong() % traffic_scale_;
    if (dest != src) break;
  }
  return new Packet(network->int_to_nodeid(src), network->int_to_nodeid(dest), message_length_);
}

Packet* TrafficManager::bittranspose_mess() {
  int src, dest;
  int bits = (int)floor(log2(traffic_scale_));
  while (true) {
    src = gen() % traffic_scale_;
    boost::dynamic_bitset<> src_binary(bits, src);
    boost::dynamic_bitset<> dest_binary(bits);
    for (int i = 0; i < bits; ++i) {
      dest_binary[i] = src_binary[(i + bits / 2) % bits];
    }
    dest = dest_binary.to_ulong() % traffic_scale_;
    if (dest != src) break;
  }
  return new Packet(network->int_to_nodeid(src), network->int_to_nodeid(dest), message_length_);
}

// special traffic pattern for Dragonfly
Packet* TrafficManager::adversarial_mess() {
  int src, dest;
  int node_per_WG = traffic_scale_ / 41;
  src = gen() % traffic_scale_;
  int src_group = src / node_per_WG;
  dest = ((src_group + 1) * node_per_WG + gen() % node_per_WG) % traffic_scale_;
  assert(src != dest);
  return new Packet(network->int_to_nodeid(src), network->int_to_nodeid(dest), message_length_);
}

Packet* TrafficManager::sd_trace_mess() {
  int src, dest;
  while (true) {
    std::string word;
    std::getline(trace_, word, ',');
    std::getline(trace_, word, ',');
    src = std::stoi(word) * 4 + gen() % 4;
    std::getline(trace_, word);
    dest = std::stoi(word) * 4 + gen() % 4;
    if (dest != src) break;
  }
  return new Packet(network->int_to_nodeid(src), network->int_to_nodeid(dest), message_length_);
}

// special traffic pattern generated by netrace
void TrafficManager::netrace(std::vector<Packet*>& vecmess, uint64_t cyc) {
  int src, dest;
  static int core_per_chip = network->groups_[0]->num_cores_;
  static nt_packet_t* trace_packet = nullptr;
  if (cyc > CTX->input_trheader->num_cycles)
    return;
  else if ((cyc + 1) % 100000000 == 0) {
    print_statistics();
  }
  while (CTX->latest_active_packet_cycle == cyc) {
    trace_packet = nt_read_packet(CTX);
    if (trace_packet == nullptr)
      return;
    else if (nt_get_packet_size(trace_packet) == -1) {
      nt_packet_free(trace_packet);
      continue;
    } else if (all_message_num_ % 10000000 == 0)
      nt_print_packet(trace_packet);
    src = trace_packet->src;
    dest = trace_packet->dst;
    if (src != dest) {
      int packet_length = ceil((double)nt_get_packet_size(trace_packet) / 16);  // 16B Bus width
      Packet* packet = new Packet(network->int_to_nodeid(src), network->int_to_nodeid(dest), packet_length);
      vecmess.push_back(packet);
      all_message_num_++;
    }
    // Get another packet from trace
    nt_packet_free(trace_packet);
  }
}
