#include "system.h"

#include <algorithm>
#include <map>
#include <vector>

#include "config.h"
#include "dragonfly_chiplet.h"
#include "dragonfly_sw.h"
#include "multiple_chip_mesh.h"
#include "multiple_chip_torus.h"
#include "single_chip_mesh.h"
#include "railx_2d_hyperx.h"
#include "railx_2d_torus.h"
#include "railx_2d_twisted_torus.h"
#include "fat_tree.h"
#include "hammingmesh.h"
#include "nvswitch.h"
#include "traffic_manager.h"

System::System() {
  num_groups_ = 0;
  num_nodes_ = 0;
  num_cores_ = 0;

  // router parameters
  router_stages_ = param->router_stages;

  // simulation parameters
  timeout_time_ = param->timeout_threshold;
}

System* System::New(const std::string& topology) {
  System* sys_ptr;
  if (topology == "SingleChipMesh")
    sys_ptr = new SingleChipMesh;
  else if (topology == "MultiChipMesh")
    sys_ptr = new MultiChipMesh;
  else if (topology == "MultiChipTorus")
    sys_ptr = new MultiChipTorus;
  else if (topology == "DragonflySW")
    sys_ptr = new DragonflySW;
  else if (topology == "DragonflyChiplet")
    sys_ptr = new DragonflyChiplet;
  else if (topology == "RailX2DHyperX")
    sys_ptr = new RailX2DHyperX;
  else if (topology == "RailX2DTorus")
    sys_ptr = new RailX2DTorus;
  else if (topology == "RailX2DTwistedTorus")
    sys_ptr = new RailX2DTwistedTorus;
  else if (topology == "FatTree")
    sys_ptr = new FatTree;
  else if (topology == "HammingMesh")
    sys_ptr = new HammingMesh;
  else if (topology == "NVSwitch")
    sys_ptr = new NVSwitchSystem;
  else {
    std::cerr << "No such a topology!" << std::endl;
    return nullptr;
  }
  return sys_ptr;
}

void System::reset() {
  {
    std::lock_guard<std::mutex> lock(pending_credit_mutex_);
    pending_credit_returns_.clear();
  }
  for (auto chip : groups_) {
    chip->reset();
  }
}

void System::push_pending_credit_return(uint64_t delivery_cycle, Node* node, int port, int vcb,
                                        int n) {
  std::lock_guard<std::mutex> lock(pending_credit_mutex_);
  pending_credit_returns_.push_back({delivery_cycle, node, port, vcb, n});
}

void System::reset_diagnostics() {
  diag_no_credit_blocked_ = 0;
  diag_credit_returns_last_cycle_ = 0;
  diag_injected_last_cycle_ = 0;
  diag_injected_ports_.clear();
}

void System::process_pending_credits(uint64_t current_cycle) {
  std::vector<PendingCreditReturn> to_deliver;
  {
    std::lock_guard<std::mutex> lock(pending_credit_mutex_);
    auto it = pending_credit_returns_.begin();
    while (it != pending_credit_returns_.end()) {
      if (it->delivery_cycle <= current_cycle) {
        to_deliver.push_back(*it);
        it = pending_credit_returns_.erase(it);
      } else {
        ++it;
      }
    }
  }
  diag_credit_returns_last_cycle_ = 0;
  diag_injected_last_cycle_ = 0;
  for (auto& it : diag_injected_ports_) {
    std::fill(it.second.begin(), it.second.end(), 0);
  }
  for (auto& e : to_deliver) {
    if (diagnostics_enabled_) diag_credit_returns_last_cycle_ += e.n;
    e.node->return_credit(e.port, e.vcb, e.n);
  }
}

void System::record_diag_injected_port(Node* node, int port) {
  if (!diagnostics_enabled_ || node == nullptr || port < 0) return;
  auto& vec = diag_injected_ports_[node];
  if (vec.size() < static_cast<size_t>(node->radix_)) {
    vec.assign(node->radix_, 0);
  }
  if (port < static_cast<int>(vec.size())) vec[port] = 1;
}

uint64_t System::get_diag_injected_port_count(Node* node) const {
  if (!diagnostics_enabled_ || node == nullptr) return 0;
  auto it = diag_injected_ports_.find(node);
  if (it == diag_injected_ports_.end()) return 0;
  uint64_t count = 0;
  for (uint8_t v : it->second) count += v ? 1 : 0;
  return count;
}

void System::init_flow_control() {
  for (auto group : groups_) {
    for (int i = 0; i < group->num_nodes_; i++) {
      Node* node = group->get_node(i);
      for (int port = 0; port < node->radix_; port++) {
        if (node->link_buffers_[port] != nullptr) {
          node->link_buffers_[port]->set_upstream(node, port);
        }
      }
      if (param->flow_control == "credit") {
        node->init_credits();
      }
      // 初始化端口使用计数（用于优先级分配）
      node->init_port_usage();
    }
  }
}

// All stages can be finished in one cycle
void System::onestage(Packet& p) {
  if (p.candidate_channels_.empty()) routing(p);
  if (!p.candidate_channels_.empty() && p.next_vc_.buffer == nullptr)  // VC Allocating Stage
    vc_allocate(p);
  if (p.next_vc_.buffer != nullptr && p.switch_allocated_ == false)  // Switch Allocating Stage
    switch_allocate(p);
}

// First stage: routing + VC allocating
// Second stage: switch allocating
void System::twostage(Packet& p) {
  if (p.candidate_channels_.empty()) routing(p);
  if (!p.candidate_channels_.empty() && p.next_vc_.buffer == nullptr)  // VC Allocating Stage
    vc_allocate(p);
  else if (p.next_vc_.buffer != nullptr && p.switch_allocated_ == false)  // Switch Allocating Stage
    switch_allocate(p);
}

// First stage: routing
// Second stage: VC allocating
// Third stage: switch allocating
void System::Threestage(Packet& p) {
  if (p.candidate_channels_.empty())  // Routing Stage
    routing(p);
  else if (!p.candidate_channels_.empty() && p.next_vc_.buffer == nullptr)  // VC Allocating Stage
    vc_allocate(p);
  else if (p.next_vc_.buffer != nullptr && p.switch_allocated_ == false)  // Switch Allocating Stage
    switch_allocate(p);
}

void System::routing(Packet& p) const {
  assert(p.candidate_channels_.empty());
  routing_algorithm(p);
  assert(!p.candidate_channels_.empty());
}

// 端口优先级分配：选择使用次数最少的端口
// credit 模式下仅考虑当前 credit 足够的端口/VC，不分配给 credit 不足的端口或 VC
static void vc_allocate_priority(Packet& p, Node* sender, bool credit_mode, System* sys) {
  const int n = static_cast<int>(p.candidate_channels_.size());
  if (n <= 0) return;

  // 1. 按物理端口（Buffer 指针）分组候选通道，并记录端口号
  std::vector<std::pair<Buffer*, int>> port_info;  // (Buffer*, port_idx)
  std::map<Buffer*, std::vector<int>> port_to_indices;  // Buffer* -> 候选索引列表

  for (int i = 0; i < n; i++) {
    Buffer* buf = p.candidate_channels_[i].buffer;
    if (port_to_indices.find(buf) == port_to_indices.end()) {
      int port = sender->get_port_to_buffer(buf);
      if (port >= 0) {
        port_info.emplace_back(buf, port);
      }
    }
    port_to_indices[buf].push_back(i);
  }

  const int num_ports = static_cast<int>(port_info.size());
  if (num_ports <= 0) return;

  // 2. [credit 模式] 只保留当前至少有一个 VC credit 足够的端口，不分配给 credit 不足的端口
  std::vector<std::pair<Buffer*, int>> candidates;
  if (credit_mode) {
    for (const auto& [buf, port] : port_info) {
      const auto& indices = port_to_indices[buf];
      bool port_has_credit = false;
      for (int idx : indices) {
        const VCInfo& vc = p.candidate_channels_[idx];
        if (sender->has_credit(port, vc.vcb, p.length_)) {
          port_has_credit = true;
          break;
        }
      }
      if (port_has_credit) {
        candidates.emplace_back(buf, port);
      }
    }
  } else {
    candidates = port_info;
  }

  if (candidates.empty()) {
    if (credit_mode && sys) sys->record_diag_no_credit_blocked();
    return;
  }

  // 3. 优先级策略（更均衡的端口使用）
  // credit 模式：按「当前可用 credit」降序优先（credit 多的端口先选），避免单口耗尽、其余空闲；同 credit 时按 port_usage 升序
  // 非 credit 模式：按 port_usage 升序（最少使用优先）
  std::vector<std::pair<Buffer*, int>> ordered_ports;
  if (credit_mode) {
    struct PortCredit {
      Buffer* buf;
      int port;
      int max_credit;
      uint64_t usage;
    };
    std::vector<PortCredit> with_credit;
    for (const auto& [buf, port] : candidates) {
      const auto& indices = port_to_indices[buf];
      int max_c = 0;
      for (int idx : indices) {
        const VCInfo& vc = p.candidate_channels_[idx];
        if (sender->has_credit(port, vc.vcb, p.length_)) {
          int c = sender->get_credit(port, vc.vcb);
          if (c > max_c) max_c = c;
        }
      }
      with_credit.push_back({buf, port, max_c, sender->get_port_usage(port)});
    }
    std::sort(with_credit.begin(), with_credit.end(), [](const PortCredit& a, const PortCredit& b) {
      if (a.max_credit != b.max_credit) return a.max_credit > b.max_credit;
      return a.usage < b.usage;
    });
    for (const auto& x : with_credit)
      ordered_ports.emplace_back(x.buf, x.port);
  } else {
    uint64_t min_usage = UINT64_MAX;
    for (const auto& [buf, port] : candidates) {
      uint64_t u = sender->get_port_usage(port);
      if (u < min_usage) min_usage = u;
    }
    for (const auto& [buf, port] : candidates) {
      if (sender->get_port_usage(port) == min_usage)
        ordered_ports.emplace_back(buf, port);
    }
    for (const auto& [buf, port] : candidates) {
      if (sender->get_port_usage(port) > min_usage)
        ordered_ports.emplace_back(buf, port);
    }
  }

  // 尝试在指定端口的 VC 列表中分配；credit 模式下只选 has_credit 的 VC
  auto try_port = [&](Buffer* buf, int port, bool empty_only) -> bool {
    const auto& indices = port_to_indices[buf];
    for (int idx : indices) {
      VCInfo& vc = p.candidate_channels_[idx];
      if (credit_mode) {
        if (!sender->has_credit(port, vc.vcb, p.length_)) continue;  // 当前 credit 不足则不分配该 VC
        bool ok = empty_only ? vc.buffer->is_empty(vc.vcb) : true;
        if (ok) {
          p.next_vc_ = vc;
          sender->increment_port_usage(port);
          return true;
        }
      } else {
        bool ok = empty_only
                      ? (vc.buffer->is_empty(vc.vcb) && vc.buffer->allocate_buffer(vc.vcb, p.length_))
                      : vc.buffer->allocate_buffer(vc.vcb, p.length_);
        if (ok) {
          p.next_vc_ = vc;
          sender->increment_port_usage(port);
          return true;
        }
      }
    }
    return false;
  };

  // 5. 按优先级顺序尝试（credit 模式：credit 多→少；buffer 模式：usage 低→高），优先空 VC
  for (const auto& [buf, port] : ordered_ports) {
    if (try_port(buf, port, true)) return;
  }
  // 6. 再试任意可用 VC
  for (const auto& [buf, port] : ordered_ports) {
    if (try_port(buf, port, false)) return;
  }
}

void System::vc_allocate(Packet& p) {
  VCInfo current_vc = p.head_trace();
  if (current_vc.buffer == nullptr ||
      current_vc.head_packet() == &p) {  // the packet is at the source or at the front of the queue
    Node* sender = get_node(current_vc.buffer == nullptr ? p.source_ : current_vc.id);

    if (param->flow_control == "credit") {
      vc_allocate_priority(p, sender, true, this);
      return;
    }

    // Buffer-based: 优先级分配
    vc_allocate_priority(p, sender, false, this);
  }
}

void System::switch_allocate(Packet& p) {
  VCInfo current_vc = p.head_trace();
  if (current_vc.buffer == nullptr) {              // the packet is at the source
    if (p.next_vc_.buffer->allocate_in_link(p)) {  // wait for link to the next buffer
      p.switch_allocated_ = true;
      if (diagnostics_enabled_) record_diag_injected(p.length_);
      if (diagnostics_enabled_) {
        Node* sender = get_node(p.source_);
        int port = sender->get_port_to_buffer(p.next_vc_.buffer);
        record_diag_injected_port(sender, port);
      }
    }
  } else if (current_vc.head_packet() == &p) {
    if (current_vc.buffer->allocate_sw_link()) {     // try to allocate the link to the switch
      if (p.next_vc_.buffer->allocate_in_link(p)) {  // wait for link to the next buffer
        p.switch_allocated_ = true;
      } else
        current_vc.buffer->release_sw_link();
    }
  }
}

void System::update(Packet& p) {
  // A packet cannot be sent to itself
  assert(p.link_timer_ > 0 || p.destination_ != p.tail_trace().id);

  p.trans_timer_++;
  if (p.wait_timer_ == timeout_time_) {  // timeout
    TM->message_timeout_++;
#ifdef DEBUG
    std::cout << p.source_ << " -> " << p.head_trace().id << " -> " << p.destination_ << " timeout!"
              << std::endl;
#endif  // DEBUG
  }

  // Processing at source node before transmission (Packetization, injection, etc.)
  if (p.head_trace().id == p.source_ && p.process_timer_ > 0) {
    p.process_timer_--;
    return;
  }

  // Routing -> VC allocating -> Switch allocating -> Transmission
  // switch_allocated_ is the final credit for message forwarding.
  if (p.link_timer_ == 0) {                     // reach the input buffer
    if (p.head_trace().id != p.destination_) {  // not reach destination
      if (router_stages_ == "OneStage") {
        onestage(p);
      } else if (router_stages_ == "TwoStage") {
        twostage(p);
      } else if (router_stages_ == "ThreeStage") {
        Threestage(p);
      } else {
        std::cerr << "No such a microarchitecture!" << std::endl;
      }
      if (!p.switch_allocated_) p.wait_timer_++;
    }
  } else {  // flying in the link
    p.link_timer_--;
  }

  VCInfo temp1, temp2;
  int i = 0;

  if (p.switch_allocated_) {
    temp1 = p.next_vc_;
    p.wait_timer_ = 0;
    p.link_timer_ = p.next_vc_.buffer->channel_.latency;
#ifdef DEBUG
    TM->traffic_map_[temp1.buffer]++;
#endif  // DEBUG
    p.hops_++;
    if (temp1.buffer->channel_.latency == 1)
      p.internal_hops_++;
    else // channel_.latency > 1
      p.external_hops_++;
    if (temp1.buffer->channel_ == specific_channel)
      p.specific_hops_++;
    else
      p.other_hops_++;
    p.candidate_channels_.clear();
    p.next_vc_ = VCInfo();
    p.switch_allocated_ = false;
  } else {
    temp1 = p.head_trace();
    // find the flit that fall behind the head flit
    while (i < p.length_ && p.flit_trace_[i].id == temp1.id)            
      i++;
  }

  if (i < p.length_) {  // there is flits fall behind
    temp2 = p.flit_trace_[i];
    int linkwidth = temp1.buffer->channel_.width;  // linkwidth
    int j = 0;
    while (i < p.length_) {
      if (p.flit_trace_[i].id == temp2.id && j < linkwidth) {
        assert(p.flit_trace_[i].id != temp1.id);
        p.flit_trace_[i] = temp1;
        j++;
      } else {
        if (p.flit_trace_[i].id != temp2.id) {
          temp1 = temp2;
          temp2 = p.flit_trace_[i];
          linkwidth = temp1.buffer->channel_.width;
          j = 0;
          assert(p.flit_trace_[i].id != temp1.id);
          p.flit_trace_[i] = temp1;
          j++;
        } else {
          assert(j == linkwidth);
        }
      }
      i++;
    }
    // If last flit shift, realease link
    if (temp2.id != p.tail_trace().id) {
      p.releaselink_ = true;
      p.leaving_vc_ = temp2;
      if (temp2.buffer != nullptr) {
        temp2.buffer->release_buffer(temp2.vcb, p.length_);
      }
    }
  }
  // If the last flit reach destination, delete message
  if (p.link_timer_ == 0 && p.tail_trace().id == p.destination_) {
    VCInfo dest_vc = p.tail_trace();
    dest_vc.buffer->release_buffer(dest_vc.vcb, p.length_);
    p.finished_ = true;
    TM->message_arrived_++;
    TM->total_cycles_ += p.trans_timer_;
    TM->total_hops_ += p.hops_;
    TM->total_external_hops_ += p.external_hops_;
    TM->total_specific_hops_ += p.specific_hops_;
    TM->total_internal_hops_ += p.internal_hops_;
    TM->total_other_hops_ += p.other_hops_;
    return;
  }
}
