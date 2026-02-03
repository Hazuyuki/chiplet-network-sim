#pragma once
#include <mutex>
#include <unordered_map>
#include <vector>

#include "group.h"
#include "packet.h"

class TrafficManager;
class Node;

class System {
 public:
  System();
  static System* New(const std::string&);
  virtual ~System() {}
  virtual void reset();
  void init_flow_control();  // 拓扑建好后调用：设置 upstream、初始化 credit（credit 流控时）
  void push_pending_credit_return(uint64_t delivery_cycle, Node* node, int port, int vcb, int n);
  void process_pending_credits(uint64_t current_cycle);  // 每周期初调用，处理到期的 credit 回报
  virtual void read_config() = 0; // Each system (topology) inport its own parameters.
  virtual void print_config() {
    std::cout << "Number of cores: " << num_cores_ << std::endl;
    std::cout << "Number of nodes: " << num_nodes_ << std::endl;
    std::cout << "Number of groups: " << num_groups_ << std::endl;
  }
  void update(Packet& s);
  void onestage(Packet& s);
  void twostage(Packet& s);
  void Threestage(Packet& s);
  // Three stages of the router
  void routing(Packet& s) const;
  void vc_allocate(Packet& s);
  void switch_allocate(Packet& s);
  virtual void routing_algorithm(Packet& s) const = 0;
  // Used in Traffic Generator, convert core_id (int) to NodeID
  virtual NodeID int_to_nodeid(int id) const {
    int node_id = id % groups_[0]->num_cores_;
    int group_id = id / groups_[0]->num_cores_;
    return NodeID(node_id, group_id);
  }
  virtual inline Group* get_group(int group_id) const { return groups_[group_id]; }
  virtual inline Group* get_group(NodeID id) const { return groups_[id.group_id]; }
  virtual inline Node* get_node(NodeID id) const {
    return groups_[id.group_id]->get_node(id.node_id);
  }

  int num_groups_;
  int num_nodes_;  // some nodes are only used for routing (e.g., switches), no packets injected
  int num_cores_;

  // router parameterss
  std::string router_stages_;
  std::string algorithm_;

  // simulation parameters
  int timeout_time_;

  friend TrafficManager;

  // RTT 偏差诊断：仅当 enable_diagnostics(true) 时收集
  void enable_diagnostics(bool on) { diagnostics_enabled_ = on; }
  void reset_diagnostics();
  void record_diag_no_credit_blocked() {
    if (diagnostics_enabled_) diag_no_credit_blocked_++;
  }
  uint64_t get_diag_no_credit_blocked() const { return diag_no_credit_blocked_; }
  uint64_t get_diag_credit_returns_last_cycle() const { return diag_credit_returns_last_cycle_; }
  void record_diag_injected(uint64_t n) {
    if (diagnostics_enabled_) diag_injected_last_cycle_ += n;
  }
  uint64_t get_diag_injected_last_cycle() const { return diag_injected_last_cycle_; }
  void record_diag_injected_port(Node* node, int port);
  uint64_t get_diag_injected_port_count(Node* node) const;

 protected:
  std::vector<Group*> groups_;

  // Credit 延迟回报：到 delivery_cycle 时对 node 的 (port, vcb) 回报 n
  struct PendingCreditReturn {
    uint64_t delivery_cycle;
    Node* node;
    int port, vcb, n;
  };
  std::vector<PendingCreditReturn> pending_credit_returns_;
  std::mutex pending_credit_mutex_;

  // RTT 偏差诊断计数（仅当 diagnostics_enabled_ 时更新）
  bool diagnostics_enabled_ = false;
  mutable uint64_t diag_no_credit_blocked_ = 0;      // VC 分配因无可用 credit 而失败次数
  mutable uint64_t diag_credit_returns_last_cycle_ = 0;  // 上一周期 credit 回报的 flit 数
  mutable uint64_t diag_injected_last_cycle_ = 0;     // 上一周期源端实际注入的 flit 数
  mutable std::unordered_map<Node*, std::vector<uint8_t>> diag_injected_ports_;
};
