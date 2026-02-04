#include "node.h"

#include <climits>
#include <cstdint>

#include "buffer.h"
#include "config.h"

std::ostream& operator<<(std::ostream& s, const NodeID& id) {
  s << "NodeID:" << id.node_id << " GroupID:" << id.group_id;
  return s;
}

Node::Node(int radix, int vc_num, int buffer_size, Channel channel) {
  group_ = nullptr;
  id_ = NodeID();
  radix_ = radix;
  vc_num_ = vc_num;
  in_buffers_.resize(radix_);
  link_nodes_.resize(radix_);
  link_buffers_.resize(radix_);
  ports_.resize(radix_);
  for (int i = 0; i < radix_; i++) {
    in_buffers_[i] = new Buffer(this, vc_num, buffer_size, channel);
    link_nodes_[i] = NodeID();
    link_buffers_[i] = nullptr;
    ports_[i] = new Port(id_, in_buffers_[i], link_nodes_[i], link_buffers_[i]);
  }
}

Node::~Node() {
  delete[] credits_;
  credits_ = nullptr;
  delete[] port_usage_;
  port_usage_ = nullptr;
  for (auto in_buffer : in_buffers_) {
    delete in_buffer;
  }
  in_buffers_.clear();
  for (auto port : ports_) {
    delete port;
  }
  ports_.clear();
}

void Node::set_node(Group* group, NodeID id) {
  group_ = group;
  id_ = id;
}

void Node::reset() {
  for (auto in_buffer : in_buffers_) {
    in_buffer->reset();
  }
  if (credits_ != nullptr) {
    init_credits();
  }
  if (port_usage_ != nullptr) {
    init_port_usage();
  }
  vc_alloc_round_robin_.store(0, std::memory_order_relaxed);
}

void Node::init_credits() {
  if (credits_ != nullptr) {
    delete[] credits_;
  }
  int n = radix_ * vc_num_;
  credits_ = new std::atomic_int[n];
  int buf_size = in_buffers_[0] != nullptr ? in_buffers_[0]->buffer_size_ : 64;
  for (int i = 0; i < n; ++i) {
    credits_[i].store(buf_size);
  }
}

bool Node::has_credit(int port, int vcb, int n) const {
  if (credits_ == nullptr || port < 0 || port >= radix_) return false;
  int idx = port * vc_num_ + vcb;
  int c = credits_[idx].load();
  return c >= n;
}

void Node::consume_credit(int port, int vcb, int n) {
  assert(credits_ != nullptr && port >= 0 && port < radix_);
  int idx = port * vc_num_ + vcb;
  int c = credits_[idx].load();
  while (!credits_[idx].compare_exchange_weak(c, c - n)) {
    ;
  }
  assert(credits_[idx].load() >= 0);
}

void Node::return_credit(int port, int vcb, int n) {
  if (credits_ == nullptr || port < 0 || port >= radix_) return;
  int idx = port * vc_num_ + vcb;
  int c = credits_[idx].load();
  while (!credits_[idx].compare_exchange_weak(c, c + n)) {
    ;
  }
}

int Node::get_credit(int port, int vcb) const {
  if (credits_ == nullptr || port < 0 || port >= radix_) return -1;
  return credits_[port * vc_num_ + vcb].load();
}

int Node::get_port_to_buffer(Buffer* buf) const {
  if (buf == nullptr) return -1;
  for (int i = 0; i < radix_; ++i) {
    if (link_buffers_[i] == buf) return i;
  }
  return -1;
}

void Node::init_port_usage() {
  if (port_usage_ != nullptr) {
    delete[] port_usage_;
  }
  port_usage_ = new std::atomic<uint64_t>[radix_];
  for (int i = 0; i < radix_; ++i) {
    port_usage_[i].store(0);
  }
}

uint64_t Node::get_port_usage(int port) const {
  if (port_usage_ == nullptr || port < 0 || port >= radix_) return UINT64_MAX;
  return port_usage_[port].load();
}

void Node::increment_port_usage(int port) {
  if (port_usage_ == nullptr || port < 0 || port >= radix_) return;
  port_usage_[port].fetch_add(1, std::memory_order_relaxed);
}
