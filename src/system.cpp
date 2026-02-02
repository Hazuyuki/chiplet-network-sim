#include "system.h"

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
  for (auto& e : to_deliver) {
    e.node->return_credit(e.port, e.vcb, e.n);
  }
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

// 轮询辅助：从 start 开始遍历 [0, n)，返回 (start + i) % n 的索引序列
static void vc_allocate_round_robin(Packet& p, Node* sender, bool credit_mode) {
  const int n = static_cast<int>(p.candidate_channels_.size());
  if (n <= 0) return;
  int start = sender->vc_rr_counter_.load(std::memory_order_relaxed) % n;

  auto try_assign = [&](int idx) -> bool {
    VCInfo& vc = p.candidate_channels_[idx];
    int port = sender->get_port_to_buffer(vc.buffer);
    if (port < 0) return false;
    if (credit_mode) {
      if (vc.buffer->is_empty(vc.vcb) && sender->has_credit(port, vc.vcb, p.length_)) {
        p.next_vc_ = vc;
        sender->vc_rr_counter_.fetch_add(1, std::memory_order_relaxed);
        return true;
      }
      return false;
    }
    if (vc.buffer->is_empty(vc.vcb) && vc.buffer->allocate_buffer(vc.vcb, p.length_)) {
      p.next_vc_ = vc;
      sender->vc_rr_counter_.fetch_add(1, std::memory_order_relaxed);
      return true;
    }
    return false;
  };

  auto try_assign_any = [&](int idx) -> bool {
    VCInfo& vc = p.candidate_channels_[idx];
    int port = sender->get_port_to_buffer(vc.buffer);
    if (port < 0) return false;
    if (credit_mode) {
      if (sender->has_credit(port, vc.vcb, p.length_)) {
        p.next_vc_ = vc;
        sender->vc_rr_counter_.fetch_add(1, std::memory_order_relaxed);
        return true;
      }
      return false;
    }
    if (vc.buffer->allocate_buffer(vc.vcb, p.length_)) {
      p.next_vc_ = vc;
      sender->vc_rr_counter_.fetch_add(1, std::memory_order_relaxed);
      return true;
    }
    return false;
  };

  // 优先空 VC，从 start 轮询
  for (int i = 0; i < n; i++) {
    int idx = (start + i) % n;
    if (try_assign(idx)) return;
  }
  // 再试任意可用 VC，从 start 轮询
  for (int i = 0; i < n; i++) {
    int idx = (start + i) % n;
    if (try_assign_any(idx)) return;
  }
}

void System::vc_allocate(Packet& p) {
  VCInfo current_vc = p.head_trace();
  if (current_vc.buffer == nullptr ||
      current_vc.head_packet() == &p) {  // the packet is at the source or at the front of the queue
    Node* sender = get_node(current_vc.buffer == nullptr ? p.source_ : current_vc.id);

    if (param->flow_control == "credit") {
      vc_allocate_round_robin(p, sender, true);
      return;
    }

    // Buffer-based: 轮询
    vc_allocate_round_robin(p, sender, false);
  }
}

void System::switch_allocate(Packet& p) {
  VCInfo current_vc = p.head_trace();
  if (current_vc.buffer == nullptr) {              // the packet is at the source
    if (p.next_vc_.buffer->allocate_in_link(p)) {  // wait for link to the next buffer
      p.switch_allocated_ = true;
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
