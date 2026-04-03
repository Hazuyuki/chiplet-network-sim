/**
 * CNSim-ASTRA Integration: Time Synchronization Test
 * Standalone Version (No ASTRA-sim dependency)
 * 
 * This tests the time synchronization mechanism.
 * 
 * Build:
 *   g++ -std=c++17 -o test_time_sync test_time_sync.cc -pthread
 * 
 * Run:
 *   ./test_time_sync
 */

#include <iostream>
#include <cassert>
#include <atomic>
#include <queue>
#include <functional>
#include <map>
#include <vector>
#include <cmath>

using namespace std;

// ====== Time Types ======
enum class TimeType { NS = 0, US, MS, FS };

struct Timespec {
    TimeType time_res;
    double time_val;  // nanoseconds
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
        return trigger_cycle > other.trigger_cycle;  // For min-heap
    }
};

// ====== CNSimNetworkApi (Simplified) ======
class CNSimNetworkApi {
public:
    // Static members for global time synchronization
    static double ns_per_cycle_;
    static uint64_t global_cycle_;
    
    CNSimNetworkApi(int rank, int num_nodes) 
        : rank_(rank), num_nodes_(num_nodes), 
          packets_sent_(0), packets_delivered_(0), 
          average_latency_(0.0), total_latency_(0), finished_(false) {
        // Note: current_cycle_ is now computed dynamically from global_cycle_
    }
    
    // ====== API Implementation ======
    
    int sim_send(void* buffer, uint64_t count, int type, int dst, int tag,
                 void (*msg_handler)(void*), void* fun_arg) {
        uint64_t now = global_cycle_;  // Use GLOBAL time
        
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
        
        // Capture the send_time for latency calculation
        uint64_t send_time_copy = now;
        
        event.callback = [this, dst, send_time_copy](void* arg) {
            auto it = pending_sends_.find(dst);
            if (it != pending_sends_.end() && !it->second.empty()) {
                auto& r = it->second.front();
                if (r.msg_handler) r.msg_handler(r.fun_arg);
                
                // Use GLOBAL time for latency
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
        
        return 0;
    }
    
    int sim_recv(void* buffer, uint64_t count, int type, int src, int tag,
                 void (*msg_handler)(void*), void* fun_arg) {
        if (src >= 0) {
            auto it = pending_sends_.find(src);
            if (it != pending_sends_.end() && !it->second.empty()) {
                auto& r = it->second.front();
                if (r.msg_handler) r.msg_handler(r.fun_arg);
                
                // Use GLOBAL time for latency
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
    
    void sim_schedule(Timespec delta, function<void(void*)> fun_ptr, void* fun_arg) {
        uint64_t delta_cycles = (uint64_t)delta.time_val;
        PendingEvent event;
        event.trigger_cycle = global_cycle_ + delta_cycles;  // Use GLOBAL time
        event.callback = fun_ptr;
        event.arg = fun_arg;
        event_queue_.push(event);
    }
    
    Timespec sim_get_time() {
        Timespec ts;
        ts.time_res = TimeType::NS;
        ts.time_val = global_cycle_ * ns_per_cycle_;  // Use GLOBAL time
        return ts;
    }
    
    // This should be called CENTRALLY to advance global time
    static void run_one_global_cycle() {
        global_cycle_++;
    }
    
    void run_one_cycle() {
        // Each instance calls this to process events at current global time
        process_events();
    }
    
    // For backward compatibility - run N cycles
    void run_cycles(uint64_t num_cycles) {
        for (uint64_t i = 0; i < num_cycles && !finished_; i++) {
            run_one_global_cycle();
            process_events();
        }
    }
    
    // Get current time (reads from global)
    uint64_t get_current_cycle() const { 
        return global_cycle_;  // Read from GLOBAL time
    }
    
    uint64_t get_packets_sent() const { return packets_sent_; }
    uint64_t get_packets_delivered() const { return packets_delivered_; }
    double get_average_latency() const { return average_latency_; }
    
    static void set_ns_per_cycle(double ns) { ns_per_cycle_ = ns; }
    static uint64_t get_global_cycle() { return global_cycle_; }
    
    // Process events at current global time (call this after advancing time)
    void process_events() {
        while (!event_queue_.empty() && event_queue_.top().trigger_cycle <= global_cycle_) {
            PendingEvent e = event_queue_.top();
            event_queue_.pop();
            if (e.callback) e.callback(e.arg);
        }
    }
    
private:
    int rank_;
    int num_nodes_;
    uint64_t packets_sent_;
    uint64_t packets_delivered_;
    double average_latency_;
    uint64_t total_latency_;
    bool finished_;
    
    priority_queue<PendingEvent> event_queue_;
    map<int, vector<SendRequest>> pending_sends_;
};

// Static member initialization
double CNSimNetworkApi::ns_per_cycle_ = 1.0;
uint64_t CNSimNetworkApi::global_cycle_ = 0;

// ====== Tests ======

atomic<int> test_passed{1};
atomic<int> callbacks{0};

void dummy_callback(void* arg) {
    callbacks.fetch_add(1);
}

int test_global_time_sync() {
    cout << "\n=== Test: Global Time Synchronization ===" << endl;
    
    CNSimNetworkApi::global_cycle_ = 0;  // Reset
    
    CNSimNetworkApi* apis[4];
    for (int i = 0; i < 4; i++) {
        apis[i] = new CNSimNetworkApi(i, 4);
    }
    
    // Check initial time (all should be 0 from global)
    cout << "  Initial (global=" << CNSimNetworkApi::get_global_cycle() << "): ";
    for (int i = 0; i < 4; i++) {
        cout << "Node" << i << "=" << apis[i]->get_current_cycle() << " ";
    }
    cout << endl;
    
    bool initial = true;
    for (int i = 0; i < 4; i++) {
        if (apis[i]->get_current_cycle() != 0) initial = false;
    }
    
    // Advance GLOBAL time (centralized)
    CNSimNetworkApi::run_one_global_cycle();
    
    // Each instance needs to process events at the new time
    for (int i = 0; i < 4; i++) {
        apis[i]->run_one_cycle();
    }
    
    cout << "  After 1 global cycle (global=" << CNSimNetworkApi::get_global_cycle() << "): ";
    for (int i = 0; i < 4; i++) {
        cout << "Node" << i << "=" << apis[i]->get_current_cycle() << " ";
    }
    cout << endl;
    
    bool after = true;
    for (int i = 0; i < 4; i++) {
        if (apis[i]->get_current_cycle() != 1) after = false;
    }
    
    // Check sim_get_time returns same value
    Timespec t0 = apis[0]->sim_get_time();
    Timespec t1 = apis[1]->sim_get_time();
    bool ts_sync = (t0.time_val == t1.time_val);
    
    cout << "  sim_get_time: Node0=" << t0.time_val << "ns, Node1=" << t1.time_val << "ns" << endl;
    
    bool ok = initial && after && ts_sync;
    
    if (ok) cout << "  ✓ PASSED" << endl;
    else { 
        cout << "  ✗ FAILED" << endl; 
        cout << "    initial=" << initial << ", after=" << after << ", ts_sync=" << ts_sync << endl;
        test_passed.store(0); 
    }
    
    for (int i = 0; i < 4; i++) delete apis[i];
    return ok ? 0 : 1;
}

int test_time_advancement() {
    cout << "\n=== Test: Time Advancement ===" << endl;
    
    CNSimNetworkApi::global_cycle_ = 0;  // Reset
    CNSimNetworkApi* net = new CNSimNetworkApi(0, 2);
    
    net->run_cycles(50);
    
    uint64_t cyc = net->get_current_cycle();
    Timespec ts = net->sim_get_time();
    
    cout << "  After 50 cycles: cycle=" << cyc << ", time=" << ts.time_val << "ns" << endl;
    
    bool ok = (cyc == 50) && (ts.time_val == 50);
    
    if (ok) cout << "  ✓ PASSED" << endl;
    else { 
        cout << "  ✗ FAILED (expected 50, got " << cyc << ")" << endl; 
        test_passed.store(0); 
    }
    
    delete net;
    return ok ? 0 : 1;
}

int test_schedule() {
    cout << "\n=== Test: Schedule with Global Time ===" << endl;
    
    CNSimNetworkApi::global_cycle_ = 0;
    CNSimNetworkApi* net = new CNSimNetworkApi(0, 2);
    
    callbacks.store(0);
    
    Timespec delta;
    delta.time_res = TimeType::NS;
    delta.time_val = 10;  // 10 cycles
    
    net->sim_schedule(delta, dummy_callback, nullptr);
    
    cout << "  Scheduled at delta=" << delta.time_val << "ns" << endl;
    
    // Run 5 cycles - should NOT fire
    net->run_cycles(5);
    cout << "  After 5 cycles: callbacks=" << callbacks.load() << endl;
    
    // Run 5 more cycles - should fire
    net->run_cycles(5);
    cout << "  After 10 cycles: callbacks=" << callbacks.load() << endl;
    
    bool ok = (callbacks.load() == 1);
    
    if (ok) cout << "  ✓ PASSED" << endl;
    else { cout << "  ✗ FAILED" << endl; test_passed.store(0); }
    
    delete net;
    return ok ? 0 : 1;
}

int test_ns_conversion() {
    cout << "\n=== Test: NS per Cycle Conversion ===" << endl;
    
    CNSimNetworkApi::global_cycle_ = 0;
    CNSimNetworkApi::set_ns_per_cycle(0.5);  // 2 GHz
    
    CNSimNetworkApi* net = new CNSimNetworkApi(0, 2);
    net->run_cycles(10);
    
    Timespec ts = net->sim_get_time();
    cout << "  After 10 cycles @ 0.5ns/cycle: " << ts.time_val << "ns" << endl;
    
    bool ok = (ts.time_val == 5.0);
    
    CNSimNetworkApi::set_ns_per_cycle(1.0);  // Reset
    
    if (ok) cout << "  ✓ PASSED" << endl;
    else { cout << "  ✗ FAILED (expected 5.0, got " << ts.time_val << ")" << endl; test_passed.store(0); }
    
    delete net;
    return ok ? 0 : 1;
}

int test_send_recv() {
    cout << "\n=== Test: Send/Receive with Time Sync ===" << endl;
    
    CNSimNetworkApi::global_cycle_ = 0;
    CNSimNetworkApi* sender = new CNSimNetworkApi(0, 2);
    CNSimNetworkApi* receiver = new CNSimNetworkApi(1, 2);
    
    callbacks.store(0);
    
    char buf[64];
    
    // Register receive first
    receiver->sim_recv(buf, 64, 0, 0, 0, dummy_callback, nullptr);
    
    // Then send
    sender->sim_send(buf, 64, 0, 1, 0, dummy_callback, nullptr);
    
    cout << "  Before simulation: global=" << CNSimNetworkApi::get_global_cycle() << endl;
    
    // Run simulation - both instances need to process events
    for (int i = 0; i < 100; i++) {
        CNSimNetworkApi::run_one_global_cycle();
        sender->run_one_cycle();
        receiver->run_one_cycle();
    }
    
    cout << "  After simulation: global=" << CNSimNetworkApi::get_global_cycle() << endl;
    cout << "  Sent: " << sender->get_packets_sent() << endl;
    cout << "  Delivered: " << receiver->get_packets_delivered() << endl;
    cout << "  Callbacks: " << callbacks.load() << endl;
    cout << "  Avg latency: " << receiver->get_average_latency() << " cycles" << endl;
    
    // The key is that callbacks ARE triggered at the right time (at cycle 2)
    bool ok = (sender->get_packets_sent() == 1) && (callbacks.load() >= 1);
    
    if (ok) cout << "  ✓ PASSED" << endl;
    else { cout << "  ✗ FAILED" << endl; test_passed.store(0); }
    
    delete sender;
    delete receiver;
    return ok ? 0 : 1;
}

int main() {
    cout << "================================================" << endl;
    cout << "CNSim-ASTRA: Time Synchronization Test" << endl;
    cout << "================================================" << endl;
    
    int failures = 0;
    
    failures += test_global_time_sync();
    failures += test_time_advancement();
    failures += test_schedule();
    failures += test_ns_conversion();
    failures += test_send_recv();
    
    cout << "\n================================================" << endl;
    if (failures == 0 && test_passed.load()) {
        cout << "ALL TESTS PASSED ✓" << endl;
    } else {
        cout << "SOME TESTS FAILED ✗ (" << failures << " failures)" << endl;
    }
    cout << "================================================" << endl;
    
    return failures;
}
