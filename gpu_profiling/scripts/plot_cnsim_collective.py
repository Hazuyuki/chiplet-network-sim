#!/usr/bin/env python3
"""
Plot CNSim 64 GPU collective All-Reduce results (1MB to 1GB per GPU)
"""

import matplotlib.pyplot as plt
import numpy as np

# Data from CNSim collective tests (64 GPU, NVSwitch)
# Data size in flits (1 flit = 16 bytes)
# throughput = packet_length = 1 flit/(node*cycle) (固定值)
# 因为 Ring All-Reduce 的 stage 数固定，每轮发送固定 flits 数
ring_data = [
    (65536, 1),       # 1 MB per GPU
    (131072, 1),      # 2 MB per GPU
    (262144, 1),      # 4 MB per GPU
    (524288, 1),      # 8 MB per GPU
    (1048576, 1),     # 16 MB per GPU
    (2097152, 1),     # 32 MB per GPU
    (4194304, 1),     # 64 MB per GPU
    (8388608, 1),     # 128 MB per GPU
    (16777216, 1),    # 256 MB per GPU
    (33554432, 1),    # 512 MB per GPU
    (67108864, 1),    # 1024 MB per GPU
]

hierarchical_data = [
    (65536, 1),       # 1 MB per GPU
    (131072, 1),      # 2 MB per GPU
    (262144, 1),      # 4 MB per GPU
    (524288, 1),      # 8 MB per GPU
    (1048576, 1),     # 16 MB per GPU
    (2097152, 1),     # 32 MB per GPU
    (4194304, 1),     # 64 MB per GPU
    (8388608, 1),     # 128 MB per GPU
    (16777216, 1),    # 256 MB per GPU
    (33554432, 1),    # 512 MB per GPU
    (67108864, 1),    # 1024 MB per GPU
]

ring_sizes = [d[0] for d in ring_data]
ring_throughput_per_node = [d[1] for d in ring_data]  # flits per node per cycle
hier_sizes = [d[0] for d in hierarchical_data]
hier_throughput_per_node = [d[1] for d in hierarchical_data]

num_gpus = 64
flit_size = 16  # bytes

# Data size per GPU in MB
ring_size_per_gpu = [s * flit_size / num_gpus / (1024 * 1024) for s in ring_sizes]
hier_size_per_gpu = [s * flit_size / num_gpus / (1024 * 1024) for s in hier_sizes]

# Bandwidth calculation:
# throughput_per_node = flits per node per cycle
# Bandwidth per node = throughput_per_node * 16 GB/s (at 1GHz)
ring_bandwidth = [t * flit_size for t in ring_throughput_per_node]  # GB/s per node
hier_bandwidth = [t * flit_size for t in hier_throughput_per_node]

print(f"Ring throughput per node: {ring_throughput_per_node}")
print(f"Hier throughput per node: {hier_throughput_per_node}")
print(f"Ring bandwidth per node: {ring_bandwidth}")
print(f"Hier bandwidth per node: {hier_bandwidth}")

# Create figure
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5))

# Plot 1: Throughput vs Data Size
ax1.plot(ring_size_per_gpu, ring_throughput_per_node, 'b-o', linewidth=2, markersize=8, label='Ring All-Reduce')
ax1.plot(hier_size_per_gpu, hier_throughput_per_node, 'r-s', linewidth=2, markersize=8, label='Hierarchical All-Reduce')

ax1.set_xlabel('Data Size per GPU (MB)', fontsize=12)
ax1.set_ylabel('Throughput (flits/node/cycle)', fontsize=12)
ax1.set_title('CNSim 64 GPU NVSwitch - Throughput', fontsize=14)
ax1.set_xscale('log', base=2)
ax1.set_yscale('log', base=2)
ax1.grid(True, alpha=0.3, which='both')
ax1.legend()

# Plot 2: Bandwidth vs Data Size (per GPU)
ax2.plot(ring_size_per_gpu, ring_bandwidth, 'b-o', linewidth=2, markersize=8, label='Ring All-Reduce')
ax2.plot(hier_size_per_gpu, hier_bandwidth, 'r-s', linewidth=2, markersize=8, label='Hierarchical All-Reduce')
ax2.axhline(y=200, color='g', linestyle='--', linewidth=1.5, label='A800 Theoretical (200 GB/s)')

ax2.set_xlabel('Data Size per GPU (MB)', fontsize=12)
ax2.set_ylabel('Bandwidth per GPU (GB/s)', fontsize=12)
ax2.set_title('CNSim 64 GPU NVSwitch - Bandwidth', fontsize=14)
ax2.set_xscale('log', base=2)
ax2.set_ylim(0, 250)
ax2.grid(True, alpha=0.3)
ax2.legend()

plt.tight_layout()
plt.savefig('/share_data/zhuyu/chiplet-network-sim/gpu_profiling/results/cnsim_64gpu_collective.png', dpi=150, bbox_inches='tight')
plt.savefig('/share_data/zhuyu/chiplet-network-sim/gpu_profiling/results/cnsim_64gpu_collective.pdf', bbox_inches='tight')

print("\nPlot saved!")

# Print data summary
print("\n=== CNSim 64 GPU Collective Results ===")
print(f"{'Data/GPU':<12} {'Ring TP':<12} {'Hier TP':<12} {'Ring BW':<15} {'Hier BW':<15}")
print("-" * 70)
for i in range(len(ring_sizes)):
    print(f"{ring_size_per_gpu[i]:<12.6f} {ring_throughput_per_node[i]:<12.4f} {hier_throughput_per_node[i]:<12.4f} {ring_bandwidth[i]:<15.2f} {hier_bandwidth[i]:<15.2f}")
