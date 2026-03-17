#include "buffer.h"

#include <thread>

#include "config.h"
#include "packet.h"
#include "system.h"

VCInfo::VCInfo(Buffer* buffer_, int vc_, NodeID id_) {
  buffer = buffer_;
  vcb = vc_;
  if (buffer_ != nullptr)
    id = buffer->node_->id_;
  else
    id = id_;
}

Packet* VCInfo::head_packet() const { return buffer->head_packet(vcb); }

Buffer::Buffer() {
  node_ = nullptr;
  buffer_size_ = 0;
  vc_num_ = 0;
  in_link_used_.store(false);
  sw_link_used_.store(false);
  vc_buffer_ = nullptr;
  vc_queue_ = nullptr;
  vc_head_packet = nullptr;
}

Buffer::Buffer(Node* node, int vc_num, int buffer_size, Channel channel) {
  node_ = node;
  buffer_size_ = buffer_size;
  vc_num_ = vc_num;
  channel_ = channel;
  in_link_used_.store(false);
  sw_link_used_.store(false);
  vc_buffer_ = new std::atomic_int[vc_num_];
  vc_queue_ = new std::queue<Packet*>[vc_num_];
  vc_head_packet = new std::atomic<Packet*>[vc_num_];
  for (int i = 0; i < vc_num_; ++i) {
    vc_buffer_[i].store(buffer_size);
    vc_queue_[i] = std::queue<Packet*>();
    vc_head_packet[i].store(nullptr);
  }
}

Buffer::~Buffer() {
  delete[] vc_buffer_;
  delete[] vc_queue_;
  delete[] vc_head_packet;
}

bool Buffer::allocate_buffer(int vcb, int n) {
  int buffer = vc_buffer_[vcb].load();
  while (true) {
    if (buffer < n)
      return false;
    else if (vc_buffer_[vcb].compare_exchange_weak(buffer, buffer - n))
      return true;
    else // buffer is modified by other threads (packets), try again
      ;  
  }
}

void Buffer::release_buffer(int vcb, int n) {
  if (param->flow_control == "credit" && upstream_node_ != nullptr && upstream_port_ >= 0) {
    if (param->credit_return_delay <= 0) {
      upstream_node_->return_credit(upstream_port_, vcb, n);
    } else {
      uint64_t delivery = param->current_simulation_cycle + param->credit_return_delay;
      network->push_pending_credit_return(delivery, upstream_node_, upstream_port_, vcb, n);
    }
  } else {
    int buffer = vc_buffer_[vcb].load();
    while (!vc_buffer_[vcb].compare_exchange_weak(buffer, buffer + n))
      ;
    assert(vc_buffer_[vcb].load() <= buffer_size_);
  }
}

void Buffer::set_upstream(Node* node, int port) {
  upstream_node_ = node;
  upstream_port_ = port;
}

bool Buffer::allocate_in_link(Packet& p) {
  int vcb = p.next_vc_.vcb;
  bool link_used_state = in_link_used_.load();
  if (link_used_state)
    return false;
  else if (in_link_used_.compare_exchange_strong(link_used_state, true)) {
    // link is allocated by this thread (packet)
    if (node_->id_ != p.destination_) {
      if (param->flow_control == "credit" && upstream_node_ != nullptr && upstream_port_ >= 0) {
        // 等待直到有足够的 credit（最多等待一定次数，避免死循环）
        const int max_wait = 10000;
        int wait_count = 0;
        while (!upstream_node_->has_credit(upstream_port_, vcb, p.length_)) {
          if (++wait_count >= max_wait) {
            // 超时，释放链路并返回失败（不再 assert）
            in_link_used_.store(false);
            return false;
          }
          // 让出 CPU，让其他线程有机会返回 credit
          std::this_thread::yield();
        }
        upstream_node_->consume_credit(upstream_port_, vcb, p.length_);
      }
      push_pkt(&p, vcb);
    }
    return true;
  } else  // allocation failed, link is allocated by other threads(packets)
    return false;
}

void Buffer::release_in_link(Packet& p) {
  assert(in_link_used_);
  // release head in former buffer
  if (p.leaving_vc_.buffer != nullptr) {  // not the source node
    assert(p.leaving_vc_.head_packet() == &p);
    p.leaving_vc_.buffer->pop_pkt(p.leaving_vc_.vcb);
  }
  // release link
  in_link_used_.store(false);
}

bool Buffer::allocate_sw_link() {
  bool link_used_state = sw_link_used_.load();
  if (link_used_state)
	return false;
  else if (sw_link_used_.compare_exchange_strong(link_used_state, true)) 
	return true; // link is allocated by this thread (packet)
  else  // allocation failed, link is allocated by other threads(packets)
	return false;
}

void Buffer::release_sw_link() {
  assert(sw_link_used_);
  sw_link_used_.store(false);
}

void Buffer::reset() {
  in_link_used_.store(false);
  sw_link_used_.store(false);
  for (int i = 0; i < vc_num_; ++i) {
    vc_buffer_[i].store(buffer_size_);
    while (!vc_queue_[i].empty()) vc_queue_[i].pop();
    vc_head_packet[i].store(nullptr);
  }
}
