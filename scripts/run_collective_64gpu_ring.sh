#!/bin/bash
# Run CNSim 64 GPU collective Ring All-Reduce test
# Data size: 2^10 to 2^14 flits
# packet_length: 4

cd /share_data/zhuyu/chiplet-network-sim

echo "Starting Ring All-Reduce 64 GPU test..."
./build/ChipletNetworkSim input/nvswitch_collective_ring_64gpu.ini > /tmp/ring_64gpu_result.log 2>&1

echo "Ring test completed. Results saved to /tmp/ring_64gpu_result.log"
