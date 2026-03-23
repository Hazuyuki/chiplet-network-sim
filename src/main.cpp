#include <condition_variable>
#include <mutex>
#include <thread>

#include "traffic_manager.h"

// global variables
Parameters* param;
TrafficManager* TM;
System* network;
boost::mt19937 gen;  // random number generator

// multi-threading variables
static std::vector<std::thread> threads;
static volatile bool finished = false; //
static std::condition_variable cv;
static std::atomic_uint64_t pkt_i;  // atomic index towards the next packet to be dispatched
// per thread vavriable
static std::mutex* mtxs;
static volatile bool* thread_ready;
static volatile bool* worker_launch;  // worker thread launch signal

// Each worker (thread) repeatedly excutes:
// 1. fetches issue_width packets from the all_packets vector in order
// 2. updates packets one by one
// Until all packets are processed
static void update_packets(std::vector<Packet*>& packets, System* system) {
  uint64_t i = pkt_i.load();
  uint64_t vec_size = packets.size();
  // Each thread fetches issue_width packets at a time
  static int issue_width = param->issue_width;
  while (i < vec_size) {
    // try to fetch issue_width packets
    if (pkt_i.compare_exchange_strong(i, i + issue_width)) {
      uint64_t max_i = std::min(i + issue_width, vec_size);
      // update packets one by one
      do {
        system->update(*packets[i]);
      } while (++i < max_i);
      i = pkt_i.load();
    }
  }
}

// Worker thread function
static void worker(std::vector<Packet*>& packets, System* s, int id) {
  std::unique_lock<std::mutex> lk(mtxs[id]);
  thread_ready[id] = true;
  cv.wait(lk);  // unlock mtxs[id] and wait for cv.notify_all()
  while (!finished) {
    update_packets(packets, s);
    worker_launch[id] = false;
    while (!worker_launch[id] && !finished) cv.wait(lk);
  }
}

// Run one cycle of the simulation by iterating through all packets twice:
// 1. Process pending credit returns (credit RTT), release link status, delete arrived packets
// 2. Update the packets
static void run_one_cycle(std::vector<Packet*>& vec_pkts, System* system, uint64_t cycle) {
  param->current_simulation_cycle = cycle;
  network->process_pending_credits(cycle);
  // single thread, fisrt come first serve
  uint64_t j = 0;
  uint64_t vecsize = vec_pkts.size();
  for (auto i = 0; i < vecsize; ++i) {
    Packet*& pkt = vec_pkts[i];
    // update link status
    if (pkt->releaselink_ == true) {
      pkt->tail_trace().buffer->release_in_link(*pkt);
      if (pkt->leaving_vc_.buffer != nullptr) // not leaving the source node
        pkt->leaving_vc_.buffer->release_sw_link();
      else {  // leaving the source node
        assert(pkt->leaving_vc_.id == pkt->source_);
      }
      pkt->releaselink_ = false;
    }
    // delete arrived packets
    if (pkt->finished_) {
      delete pkt;
    } else {
      vec_pkts[j] = pkt;
      j++;
    }
  }
  vec_pkts.resize(j);

  // update packets, multi-threading
  pkt_i.store(0);
  if (vec_pkts.size() < param->threads * 1 || param->threads < 2) {  // single thread
    update_packets(vec_pkts, system);
  } else {
    for (int i = 0; i < param->threads; ++i) {
      worker_launch[i] = true;
      mtxs[i].unlock();
    }
    cv.notify_all();
    // wait all threads finish
    for (int i = 0; i < param->threads; ++i) {
      while (worker_launch[i])
        ;
      mtxs[i].lock(); // acquire the lock that released by the cv.wait()
    }
    // All workers are waiting for cv.notify_all() at the second cv.wait()
  }
}

int main(int argc, char* argv[]) {
  std::string config_file;
  if (argc > 1) {
    config_file = argv[1];
    std::cout << "Using configuration file: " << config_file << std::endl;
  } else {
    std::cerr << "Usage: " << argv[0] << " <config_file>" << std::endl;
    return 1;
  }
  param = new Parameters(config_file);
  network = System::New(param->topology);
  network->init_flow_control();
  TM = new TrafficManager();
  gen.seed(1);

  double maximum_receiving_rate = 0;
  std::vector<Packet*> all_packets;

  // Multi-threads initialization
  if (param->threads > 1) {
    mtxs = new std::mutex[param->threads];
    thread_ready = new bool[param->threads];
    worker_launch = new bool[param->threads];
    pkt_i.store(0);
    for (auto i = 0; i < param->threads; ++i) {
      thread_ready[i] = false;
      threads.push_back(std::thread(worker, std::ref(all_packets), network, i));
    }
    for (auto i = 0; i < param->threads; ++i) {  // wait for all threads ready
      while (!thread_ready[i])
        ;
      mtxs[i].lock();  // acquire the lock that released by the cv.wait()
    }
    // All workers are waiting for cv.notify_all() at the first cv.wait()
  }

  if (param->traffic == "netrace") {  // inject according to the time_stamp
    TM->injection_rate_ = (double)TM->CTX->input_trheader->num_packets /
                          TM->CTX->input_trheader->num_cycles / network->num_cores_;
    for (uint64_t i = 0; i < TM->CTX->input_trheader->num_cycles + 1000; i++) {
      TM->genMes(all_packets, i);
      run_one_cycle(all_packets, network, i);
    }
    TM->print_statistics();
    nt_close_trfile(TM->CTX);
  } 
  else if (param->traffic.find("collective") != std::string::npos) {
    // 数据大小扫描: 使用更大的数据量来测试
    // data_size 以 flits 为单位
    // 每个 GPU 的数据量 = data_size / num_gpus
    // Ring All-Reduce 需要 (num_gpus - 1) * 2 轮
    // Hierarchical 需要 (gpus_per_server - 1) * 2 + (num_servers - 1) * 2 轮
    
    // 测试更大的数据量: 2^10 到 2^17 (1MB to 64MB per GPU, 假设 1 flit = 16 bytes)
    for (int i = 10; i < 18; i++) {
      TM->reset();
      TM->data_size = 1 << i;  // 使用固定的 flits 数量
      uint64_t sim_cycle = 0;
      while (true){
        TM->genMes(all_packets);
        run_one_cycle(all_packets, network, sim_cycle);
        sim_cycle++;
        if (TM->is_done) break;
      }
      // TM->print_statistics();
      TM->print_collective_statistics();
    }
  }
  else {  // gradually increase the injection rate to find the saturation point
    bool saturated = false;
    while (!saturated) {
      TM->injection_rate_ += param->injection_increment;

      //  warm up for 50% of the simulation time
      for (uint64_t i = 0; i < param->simulation_time / 2; i++) {
        TM->genMes(all_packets);
        run_one_cycle(all_packets, network, i);
      }
      TM->reset();
      // 固定测量窗口：始终跑满 simulation_time 周期，不因超时提前结束，使不同 (R, 注入率) 可比
      for (uint64_t i = 0; i < param->simulation_time; i++) {
        TM->genMes(all_packets);
        run_one_cycle(all_packets, network, i);
      }
      TM->print_statistics();
      if (TM->receiving_rate() > maximum_receiving_rate)
        maximum_receiving_rate = TM->receiving_rate();
      // Saturated: 仅当“到达过少”且（未设 max_injection 或 已扫描到 max_injection）时才停止
      bool saturation_detected =
          (TM->message_arrived_ < (TM->message_timeout_ + all_packets.size()) * 5);
      bool reached_max_injection =
          (param->max_injection <= 0 || TM->injection_rate_ >= param->max_injection);
      if (saturation_detected && reached_max_injection) {
        std::cout << std::endl
                  << "Saturation point!" << std::endl
                  << "Maximum average receiving traffic: " << maximum_receiving_rate
                  << " flits/(node*cycle)" << std::endl;
        saturated = true;
#ifdef DEBUG // check deadlock
        for (uint64_t drain_i = 0; drain_i < param->simulation_time * 2; drain_i++) {  // try to drain
          run_one_cycle(all_packets, network, drain_i);
          if (all_packets.size() == 0) {
            std::cerr << "No deadlock!" << std::endl;
            break;
          }
        }
        if (all_packets.size() != 0) std::cerr << "Possible Deadlock!" << std::endl;
#endif  // DEBUG
      }
      for (auto pkt : all_packets) delete pkt;
      all_packets.clear();
      network->reset();
      gen.seed(1);
    }
  }
  if (param->threads > 1) {
    finished = true;
    for (int i = 0; i < param->threads; ++i) {
      mtxs[i].unlock();
    }
    cv.notify_all();
    for (int i = 0; i < param->threads; ++i) {
      threads[i].join();
    }
    delete[] mtxs;
    delete[] thread_ready;
    delete[] worker_launch;
  }
  delete TM;
  delete network;
  delete param;
  return 0;
}