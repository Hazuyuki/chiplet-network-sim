#!/bin/bash
# Run CNSim 64 GPU collective Hierarchical All-Reduce test
# Data size: 2^10 to 2^14 flits
# packet_length: 4

cd /share_data/zhuyu/chiplet-network-sim

echo "Starting Hierarchical All-Reduce 64 GPU test..."
./build/ChipletNetworkSim input/nvswitch_collective_hierarchical_64gpu.ini > /tmp/hierarchical_64gpu_result.log 2>&1

echo "Hierarchical test completed. Results saved to /tmp/hierarchical_64gpu_result.log"
