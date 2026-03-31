#!/usr/bin/env python3
"""
Plot bandwidth vs data size for 4 GPU NCCL All-Reduce experiment
"""

import os
import re
import matplotlib.pyplot as plt
import numpy as np

# Data from the 4 GPU experiment
data_sizes = ["1MB", "4MB", "16MB", "64MB", "256MB", "1024MB"]
bandwidths = [22.74, 52.74, 119.68, 167.01, 185.52, 197.92]  # GB/s
times = [0.092, 0.159, 0.280, 0.804, 2.894, 10.850]  # ms

# Convert to numeric values for x-axis
data_size_mb = [1, 4, 16, 64, 256, 1024]

# Create figure with two subplots
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5))

# Plot 1: Bandwidth vs Data Size
ax1.plot(data_size_mb, bandwidths, 'b-o', linewidth=2, markersize=8, label='Measured')
ax1.axhline(y=200, color='r', linestyle='--', linewidth=1.5, label='Theoretical (200 GB/s)')
ax1.fill_between(data_size_mb, 0, bandwidths, alpha=0.3)

ax1.set_xlabel('Data Size (MB)', fontsize=12)
ax1.set_ylabel('Bandwidth (GB/s)', fontsize=12)
ax1.set_title('4 GPU NCCL All-Reduce Bandwidth', fontsize=14)
ax1.set_xscale('log', base=2)
ax1.set_xticks(data_size_mb)
ax1.set_xticklabels(data_sizes)
ax1.grid(True, alpha=0.3)
ax1.legend(loc='lower right')
ax1.set_xlim(0.5, 2048)

# Add efficiency labels
for i, (x, y) in enumerate(zip(data_size_mb, bandwidths)):
    eff = y / 200 * 100
    ax1.annotate(f'{eff:.1f}%', (x, y), textcoords="offset points", 
                xytext=(0, 10), ha='center', fontsize=9)

# Plot 2: Latency vs Data Size
ax2.plot(data_size_mb, times, 'g-s', linewidth=2, markersize=8, label='Measured')
ax2.set_xlabel('Data Size (MB)', fontsize=12)
ax2.set_ylabel('Latency (ms)', fontsize=12)
ax2.set_title('4 GPU NCCL All-Reduce Latency', fontsize=14)
ax2.set_xscale('log', base=2)
ax2.set_yscale('log')
ax2.set_xticks(data_size_mb)
ax2.set_xticklabels(data_sizes)
ax2.grid(True, alpha=0.3, which='both')
ax2.legend(loc='lower right')
ax2.set_xlim(0.5, 2048)

plt.tight_layout()
plt.savefig('/share_data/zhuyu/chiplet-network-sim/gpu_profiling/results/bandwidth_4gpu.png', dpi=150, bbox_inches='tight')
plt.savefig('/share_data/zhuyu/chiplet-network-sim/gpu_profiling/results/bandwidth_4gpu.pdf', bbox_inches='tight')

print("Plot saved to:")
print("  - /share_data/zhuyu/chiplet-network-sim/gpu_profiling/results/bandwidth_4gpu.png")
print("  - /share_data/zhuyu/chiplet-network-sim/gpu_profiling/results/bandwidth_4gpu.pdf")

# Also print the data table
print("\n=== Data Summary ===")
print(f"{'Data Size':<12} {'Bandwidth':<15} {'Latency':<12} {'Efficiency':<12}")
print("-" * 50)
for i in range(len(data_sizes)):
    eff = bandwidths[i] / 200 * 100
    print(f"{data_sizes[i]:<12} {bandwidths[i]:<15.2f} {times[i]:<12.3f} {eff:<12.1f}%")
