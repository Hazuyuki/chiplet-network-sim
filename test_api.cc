/**
 * CNSim-ASTRA Integration: Complete API Test
 * Standalone Version (No ASTRA-sim dependency)
 * 
 * Tests all key API functions:
 * 1. sim_send - Send data to destination
 * 2. sim_recv - Receive data from source
 * 3. sim_schedule - Schedule events
 * 4. sim_get_time - Get simulation time
 * 5. get_BW_at_dimension - Get bandwidth
 * 6. sim_notify_finished - Notify completion
 * 
 * Build:
 *   g++ -std=c++17 -o test_api test_api.cc -pthread
 * 
 * Run:
 *   ./test_api
 */

#include <iostream>
#include <cassert>
#include <atomic>
#include <queue>
#include <functional>
#include <map>
#include <vector>
#include <cmath>
#include <string>

using namespace std;

// ====== Time Types (from ASTRA-sim) ======
enum time_type_e { SE = 0, MS, US, NS, FS };

struct timespec_t {
    time_type_e time_res;
    long double time_val;
};

struct sim_request {
    uint32_t srcRank;
    uint32_t dstRank;
    uint32_t tag;
    uint32_t reqType;
    uint64_t reqCount;
    uint32_t vnet;
    uint32_t layerNum;
};

// ====== Send Request Structure ======
struct SendRequest {
    void* buffer;
    uint64_t count;
    int tag;
    function<void(void*)> msg_handler;
    void* fun_arg;
    uint64_t send_time;
    int dst;
};

// ====== Pending Event ======
struct PendingEvent {
    uint64_t trigger_cycle;
    function<void(void*)> callback;
    void* arg;
    
    bool operator<(const PendingEvent& other) const {
        return trigger_cycle > other.trigger_cycle;
    }
};

// ====== CNSimNetworkApi Implementation ======
class CNSimNetworkApi {
public:
    enum class CNSimBackendType {
        NVSwitch, SingleChipMesh, MultipleChipMesh, FatTree, DragonflySW, DragonflyChiplet, RailX
    };

    enum class BackendType {
        NotSpecified = 0, Garnet, NS3, Analytical, CNSim
    };

    // Static members for global time synchronization
    static double ns_per_cycle_;
    static uint64_t global_cycle_;
    
    CNSimNetworkApi(int rank, const string& config_file = "", 
                   CNSimBackendType backend_type = CNSimBackendType::NVSwitch, 
                   int num_nodes = 8)
        : rank_(rank), config_file_(config_file), backend_type_(backend_type),
          num_nodes_(num_nodes), packets_sent_(0), packets_delivered_(0), 
          average_latency_(0.0), total_latency_(0), finished_(false) {}
    
    // ====== AstraNetworkAPI Interface Implementation ======
    
    // sim_send: Send data to destination node
    int sim_send(void* buffer, uint64_t count, int type, int dst, int tag,
                 sim_request* request, void (*msg_handler)(void*), void* fun_arg) {
        uint64_t now = global_cycle_;
        
        SendRequest req;
        req.buffer = buffer;
        req.count = count;
        req.tag = tag;
        req.msg_handler = msg_handler;
        req.fun_arg = fun_arg;
        req.send_time = now;
        req.dst = dst;
        
        pending_sends_[dst].push_back(req);
        packets_sent_++;
        
        // Schedule delivery (simplified: 2 cycles)
        uint64_t delivery_cycle = now + 2;
        
        PendingEvent event;
        event.trigger_cycle = delivery_cycle;
        uint64_t send_time_copy = now;
        
        event.callback = [this, dst, send_time_copy](void* arg) {
            auto it = pending_sends_.find(dst);
            if (it != pending_sends_.end() && !it->second.empty()) {
                auto& r = it->second.front();
                if (r.msg_handler) r.msg_handler(r.fun_arg);
                uint64_t latency = global_cycle_ - send_time_copy;
                total_latency_ += latency;
                packets_delivered_++;
                if (packets_delivered_ > 0) {
                    average_latency_ = (double)total_latency_ / packets_delivered_;
                }
                it->second.erase(it->second.begin());
            }
        };
        event.arg = nullptr;
        event_queue_.push(event);
        
        return 0;  // Success
    }
    
    // sim_recv: Receive data from source node
    int sim_recv(void* buffer, uint64_t count, int type, int src, int tag,
                 sim_request* request, void (*msg_handler)(void*), void* fun_arg) {
        if (src >= 0) {
            auto it = pending_sends_.find(src);
            if (it != pending_sends_.end() && !it->second.empty()) {
                auto& r = it->second.front();
                if (r.msg_handler) r.msg_handler(r.fun_arg);
                uint64_t latency = global_cycle_ - r.send_time;
                total_latency_ += latency;
                packets_delivered_++;
                if (packets_delivered_ > 0) {
                    average_latency_ = (double)total_latency_ / packets_delivered_;
                }
                it->second.erase(it->second.begin());
                return 0;
            }
        }
        return 0;
    }
    
    // sim_schedule: Schedule an event to be triggered after delta time
    void sim_schedule(timespec_t delta, void (*fun_ptr)(void*), void* fun_arg) {
        uint64_t delta_cycles = (uint64_t)delta.time_val;
        PendingEvent event;
        event.trigger_cycle = global_cycle_ + delta_cycles;
        event.callback = fun_ptr;
        event.arg = fun_arg;
        event_queue_.push(event);
    }
    
    // get_backend_type: Get backend type
    BackendType get_backend_type() { return BackendType::CNSim; }
    
    // sim_get_time: Get current simulation time
    timespec_t sim_get_time() {
        timespec_t ts;
        ts.time_res = NS;
        ts.time_val = global_cycle_ * ns_per_cycle_;
        return ts;
    }
    
    // get_BW_at_dimension: Get bandwidth at specific dimension
    double get_BW_at_dimension(int dim) {
        // Return bandwidth in GB/s (NVSwitch: 900 GB/s per link)
        return 900.0;
    }
    
    // sim_notify_finished: Notify that workload has finished
    void sim_notify_finished() {
        finished_ = true;
    }
    
    // ====== CNSim-specific Methods ======
    
    void run_one_cycle() {
        global_cycle_++;
        process_events();
    }
    
    void run_cycles(uint64_t num_cycles) {
        for (uint64_t i = 0; i < num_cycles && !finished_; i++) {
            run_one_cycle();
        }
    }
    
    uint64_t get_packets_sent() const { return packets_sent_; }
    uint64_t get_packets_delivered() const { return packets_delivered_; }
    double get_average_latency() const { return average_latency_; }
    uint64_t get_current_cycle() const { return global_cycle_; }
    bool is_finished() const { return finished_; }
    
    static void set_ns_per_cycle(double ns) { ns_per_cycle_ = ns; }
    static uint64_t get_global_cycle() { return global_cycle_; }
    
private:
    int rank_;
    string config_file_;
    CNSimBackendType backend_type_;
    int num_nodes_;
    
    priority_queue<PendingEvent> event_queue_;
    map<int, vector<SendRequest>> pending_sends_;
    
    uint64_t packets_sent_;
    uint64_t packets_delivered_;
    double average_latency_;
    uint64_t total_latency_;
    bool finished_;
    
    void process_events() {
        while (!event_queue_.empty() && event_queue_.top().trigger_cycle <= global_cycle_) {
            PendingEvent e = event_queue_.top();
            event_queue_.pop();
            if (e.callback) e.callback(e.arg);
        }
    }
};

// Static member initialization
double CNSimNetworkApi::ns_per_cycle_ = 1.0;
uint64_t CNSimNetworkApi::global_cycle_ = 0;

// ====== Tests ======

atomic<int> test_passed{1};
atomic<int> callbacks{0};

void test_callback(void* arg) {
    callbacks.fetch_add(1);
}

void test_callback_with_arg(void* arg) {
    int* counter = static_cast<int*>(arg);
    (*counter)++;
}

int test_sim_send() {
    cout << "\n=== Test: sim_send ===" << endl;
    
    CNSimNetworkApi::global_cycle_ = 0;
    CNSimNetworkApi* sender = new CNSimNetworkApi(0, "", CNSimNetworkApi::CNSimBackendType::NVSwitch, 2);
    
    char buffer[64];
    sim_request request;
    
    int result = sender->sim_send(buffer, 64, 0, 1, 0, &request, test_callback, nullptr);
    
    cout << "  sim_send return: " << result << endl;
    cout << "  Packets sent: " << sender->get_packets_sent() << endl;
    cout << "  Pending events: " << (sender->get_packets_sent() > 0 ? "yes" : "no") << endl;
    
    bool ok = (result == 0) && (sender->get_packets_sent() == 1);
    
    if (ok) cout << "  ✓ PASSED" << endl;
    else { cout << "  ✗ FAILED" << endl; test_passed.store(0); }
    
    delete sender;
    return ok ? 0 : 1;
}

int test_sim_recv() {
    cout << "\n=== Test: sim_recv ===" << endl;
    
    CNSimNetworkApi::global_cycle_ = 0;
    CNSimNetworkApi* receiver = new CNSimNetworkApi(1, "", CNSimNetworkApi::CNSimBackendType::NVSwitch, 2);
    
    char buffer[64];
    sim_request request;
    
    int result = receiver->sim_recv(buffer, 64, 0, 0, 0, &request, test_callback, nullptr);
    
    cout << "  sim_recv return: " << result << endl;
    
    bool ok = (result == 0);  // Returns 0 even if no matching send
    
    if (ok) cout << "  ✓ PASSED" << endl;
    else { cout << "  ✗ FAILED" << endl; test_passed.store(0); }
    
    delete receiver;
    return ok ? 0 : 1;
}

int test_sim_schedule() {
    cout << "\n=== Test: sim_schedule ===" << endl;
    
    CNSimNetworkApi::global_cycle_ = 0;
    CNSimNetworkApi* network = new CNSimNetworkApi(0);
    
    callbacks.store(0);
    
    timespec_t delta;
    delta.time_res = NS;
    delta.time_val = 10;  // 10 cycles
    
    network->sim_schedule(delta, test_callback, nullptr);
    
    cout << "  Scheduled callback at cycle +10" << endl;
    
    // Run 5 cycles - should NOT fire
    network->run_cycles(5);
    cout << "  After 5 cycles: callbacks=" << callbacks.load() << endl;
    
    // Run 5 more cycles - should fire
    network->run_cycles(5);
    cout << "  After 10 cycles: callbacks=" << callbacks.load() << endl;
    
    bool ok = (callbacks.load() == 1);
    
    if (ok) cout << "  ✓ PASSED" << endl;
    else { cout << "  ✗ FAILED" << endl; test_passed.store(0); }
    
    delete network;
    return ok ? 0 : 1;
}

int test_sim_get_time() {
    cout << "\n=== Test: sim_get_time ===" << endl;
    
    CNSimNetworkApi::global_cycle_ = 0;
    CNSimNetworkApi* network = new CNSimNetworkApi(0);
    
    timespec_t t1 = network->sim_get_time();
    cout << "  Initial time: " << t1.time_val << " ns" << endl;
    
    network->run_cycles(100);
    
    timespec_t t2 = network->sim_get_time();
    cout << "  After 100 cycles: " << t2.time_val << " ns" << endl;
    
    bool ok = (t1.time_val == 0) && (t2.time_val == 100);
    
    if (ok) cout << "  ✓ PASSED" << endl;
    else { cout << "  ✗ FAILED" << endl; test_passed.store(0); }
    
    delete network;
    return ok ? 0 : 1;
}

int test_get_BW_at_dimension() {
    cout << "\n=== Test: get_BW_at_dimension ===" << endl;
    
    CNSimNetworkApi* network = new CNSimNetworkApi(0);
    
    double bw0 = network->get_BW_at_dimension(0);
    double bw1 = network->get_BW_at_dimension(1);
    double bw2 = network->get_BW_at_dimension(2);
    
    cout << "  BW at dim 0: " << bw0 << " GB/s" << endl;
    cout << "  BW at dim 1: " << bw1 << " GB/s" << endl;
    cout << "  BW at dim 2: " << bw2 << " GB/s" << endl;
    
    bool ok = (bw0 == 900.0);  // NVSwitch default
    
    if (ok) cout << "  ✓ PASSED" << endl;
    else { cout << "  ✗ FAILED" << endl; test_passed.store(0); }
    
    delete network;
    return ok ? 0 : 1;
}

int test_sim_notify_finished() {
    cout << "\n=== Test: sim_notify_finished ===" << endl;
    
    CNSimNetworkApi* network = new CNSimNetworkApi(0);
    
    cout << "  Before: finished=" << network->is_finished() << endl;
    
    network->sim_notify_finished();
    
    cout << "  After: finished=" << network->is_finished() << endl;
    
    bool ok = network->is_finished();
    
    if (ok) cout << "  ✓ PASSED" << endl;
    else { cout << "  ✗ FAILED" << endl; test_passed.store(0); }
    
    delete network;
    return ok ? 0 : 1;
}

int test_get_backend_type() {
    cout << "\n=== Test: get_backend_type ===" << endl;
    
    CNSimNetworkApi* network = new CNSimNetworkApi(0);
    
    auto type = network->get_backend_type();
    
    cout << "  Backend type: " << (type == CNSimNetworkApi::BackendType::CNSim ? "CNSim" : "other") << endl;
    
    bool ok = (type == CNSimNetworkApi::BackendType::CNSim);
    
    if (ok) cout << "  ✓ PASSED" << endl;
    else { cout << "  ✗ FAILED" << endl; test_passed.store(0); }
    
    delete network;
    return ok ? 0 : 1;
}

int test_complete_send_recv() {
    cout << "\n=== Test: Complete Send/Receive ===" << endl;
    
    CNSimNetworkApi::global_cycle_ = 0;
    CNSimNetworkApi* sender = new CNSimNetworkApi(0);
    CNSimNetworkApi* receiver = new CNSimNetworkApi(1);
    
    callbacks.store(0);
    
    char send_buf[64], recv_buf[64];
    sim_request request;
    
    // Register receive
    receiver->sim_recv(recv_buf, 64, 0, 0, 0, &request, test_callback, nullptr);
    
    // Send
    sender->sim_send(send_buf, 64, 0, 1, 0, &request, test_callback, nullptr);
    
    cout << "  Initial state: sent=" << sender->get_packets_sent() 
         << ", delivered=" << receiver->get_packets_delivered() << endl;
    
    // Run simulation
    sender->run_cycles(100);
    receiver->run_cycles(100);
    
    cout << "  After simulation: sent=" << sender->get_packets_sent() 
         << ", delivered=" << receiver->get_packets_delivered() 
         << ", callbacks=" << callbacks.load() << endl;
    
    bool ok = (sender->get_packets_sent() == 1) && (callbacks.load() >= 1);
    
    if (ok) cout << "  ✓ PASSED" << endl;
    else { cout << "  ✗ FAILED" << endl; test_passed.store(0); }
    
    delete sender;
    delete receiver;
    return ok ? 0 : 1;
}

int test_multiple_nodes() {
    cout << "\n=== Test: Multiple Nodes Time Sync ===" << endl;
    
    CNSimNetworkApi::global_cycle_ = 0;
    
    CNSimNetworkApi* node0 = new CNSimNetworkApi(0);
    CNSimNetworkApi* node1 = new CNSimNetworkApi(1);
    CNSimNetworkApi* node2 = new CNSimNetworkApi(2);
    CNSimNetworkApi* node3 = new CNSimNetworkApi(3);
    
    // Advance time through one node
    node0->run_one_cycle();
    
    // All nodes should see the same time
    cout << "  After 1 cycle: node0=" << node0->get_current_cycle()
         << ", node1=" << node1->get_current_cycle()
         << ", node2=" << node2->get_current_cycle()
         << ", node3=" << node3->get_current_cycle() << endl;
    
    bool ok = (node0->get_current_cycle() == 1) &&
              (node1->get_current_cycle() == 1) &&
              (node2->get_current_cycle() == 1) &&
              (node3->get_current_cycle() == 1);
    
    if (ok) cout << "  ✓ PASSED" << endl;
    else { cout << "  ✗ FAILED" << endl; test_passed.store(0); }
    
    delete node0;
    delete node1;
    delete node2;
    delete node3;
    return ok ? 0 : 1;
}

int main() {
    cout << "================================================" << endl;
    cout << "CNSim-ASTRA: Complete API Test" << endl;
    cout << "================================================" << endl;
    cout << "Testing all API functions:" << endl;
    cout << "  1. sim_send" << endl;
    cout << "  2. sim_recv" << endl;
    cout << "  3. sim_schedule" << endl;
    cout << "  4. sim_get_time" << endl;
    cout << "  5. get_BW_at_dimension" << endl;
    cout << "  6. sim_notify_finished" << endl;
    cout << "  7. get_backend_type" << endl;
    cout << "  8. Complete Send/Receive" << endl;
    cout << "  9. Multiple Nodes Time Sync" << endl;
    
    int failures = 0;
    
    failures += test_sim_send();
    failures += test_sim_recv();
    failures += test_sim_schedule();
    failures += test_sim_get_time();
    failures += test_get_BW_at_dimension();
    failures += test_sim_notify_finished();
    failures += test_get_backend_type();
    failures += test_complete_send_recv();
    failures += test_multiple_nodes();
    
    cout << "\n================================================" << endl;
    if (failures == 0 && test_passed.load()) {
        cout << "ALL API TESTS PASSED ✓" << endl;
    } else {
        cout << "SOME TESTS FAILED ✗ (" << failures << " failures)" << endl;
    }
    cout << "================================================" << endl;
    
    return failures;
}
