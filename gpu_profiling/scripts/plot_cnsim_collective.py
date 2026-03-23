#!/usr/bin/env python3
"""
Plot CNSim 64 GPU collective All-Reduce results
"""

import matplotlib.pyplot as plt
import numpy as np

# Data from CNSim collective tests (64 GPU, NVSwitch)
# throughput = data_size / cycles, total flits across all nodes
ring_data = [
    (256, 2.01575),
    (512, 4.0315),
    (1024, 8.06299),
    (2048, 16.126),
    (4096, 32.252),
    (8192, 64.5039),
    (16384, 129.008),
    (32768, 258.016),
]

hierarchical_data = [
    (256, 8.82759),
    (512, 17.6552),
    (1024, 35.3103),
    (2048, 70.6207),
    (4096, 141.241),
    (8192, 282.483),
    (16384, 564.966),
    (32768, 1129.93),
]

ring_sizes = [d[0] for d in ring_data]
ring_throughput_total = [d[1] for d in ring_data]  # total flits per cycle (all nodes)
hier_sizes = [d[0] for d in hierarchical_data]
hier_throughput_total = [d[1] for d in hierarchical_data]

num_gpus = 64

# Data size per GPU in MB
ring_size_per_gpu = [s * 16 / num_gpus / (1024 * 1024) for s in ring_sizes]
hier_size_per_gpu = [s * 16 / num_gpus / (1024 * 1024) for s in hier_sizes]

# Bandwidth calculation:
# throughput_total = total flits per cycle across all nodes
# throughput_per_node = throughput_total / num_gpus
# Bandwidth per node = throughput_per_node * 16 GB/s (at 1GHz)
ring_throughput_per_node = [t / num_gpus for t in ring_throughput_total]
hier_throughput_per_node = [t / num_gpus for t in hier_throughput_total]

flit_size = 16  # bytes
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
