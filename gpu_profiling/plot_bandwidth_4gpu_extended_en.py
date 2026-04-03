#!/usr/bin/env python3
"""
Plot 4 GPU AllReduce Bandwidth Sweep Results
Data sizes: 1MB to 8GB (Extended Test)
"""

import matplotlib.pyplot as plt
import numpy as np

# Test results
data_sizes_mb = [1, 4, 16, 64, 256, 1024, 2048, 4096, 8192]  # MB
bandwidth_gbps = [31.24, 52.23, 129.63, 168.54, 185.38, 193.30, 201.36, 203.13, 204.72]  # GB/s

# Theoretical max bandwidth (4 GPU, each GPU 18 links × 50 bytes/link × 1 GHz = 900 GB/s total)
# AllReduce effective bandwidth = Total bandwidth / 4 = 225 GB/s (assuming perfect linear scaling)
# But actual test shows stable at ~205 GB/s
theoretical_max = 225  # GB/s

# Create figure
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 6))

# Left plot: Bandwidth vs Data Size
ax1.plot(data_sizes_mb, bandwidth_gbps, 'o-', linewidth=2, markersize=8, 
         color='#2E86AB', label='Measured Bandwidth', zorder=3)
ax1.axhline(y=theoretical_max, color='#A23B72', linestyle='--', linewidth=2, 
            label=f'Theoretical Peak ({theoretical_max} GB/s)', zorder=2)
ax1.axhline(y=bandwidth_gbps[-1], color='#F18F01', linestyle=':', linewidth=2,
            label=f'Measured Peak ({bandwidth_gbps[-1]:.1f} GB/s)', zorder=2)

# Set logarithmic x-axis
ax1.set_xscale('log', base=2)
ax1.set_xlabel('Data Size (MB)', fontsize=14, fontweight='bold')
ax1.set_ylabel('Effective Bandwidth (GB/s)', fontsize=14, fontweight='bold')
ax1.set_title('4 GPU AllReduce Bandwidth Sweep (1MB - 8GB)', fontsize=16, fontweight='bold')
ax1.grid(True, alpha=0.3, linestyle='-', linewidth=0.5, zorder=1)
ax1.legend(fontsize=12, loc='lower right')
ax1.tick_params(labelsize=12)

# Annotate bandwidth values on each data point
for i, (x, y) in enumerate(zip(data_sizes_mb, bandwidth_gbps)):
    ax1.annotate(f'{y:.1f}', (x, y), textcoords="offset points", 
                 xytext=(0, 10), ha='center', fontsize=10, fontweight='bold')

# Right plot: Efficiency vs Data Size
efficiency = [bw / theoretical_max * 100 for bw in bandwidth_gbps]
ax2.plot(data_sizes_mb, efficiency, 's-', linewidth=2, markersize=8,
         color='#C73E1D', label='Efficiency', zorder=3)
ax2.axhline(y=100, color='#A23B72', linestyle='--', linewidth=2,
            label='100% Efficiency', zorder=2)

ax2.set_xscale('log', base=2)
ax2.set_xlabel('Data Size (MB)', fontsize=14, fontweight='bold')
ax2.set_ylabel('Efficiency (%)', fontsize=14, fontweight='bold')
ax2.set_title('Bandwidth Efficiency vs Data Size', fontsize=16, fontweight='bold')
ax2.grid(True, alpha=0.3, linestyle='-', linewidth=0.5, zorder=1)
ax2.legend(fontsize=12, loc='lower right')
ax2.tick_params(labelsize=12)
ax2.set_ylim([0, 110])

# Annotate efficiency values on each data point
for i, (x, y) in enumerate(zip(data_sizes_mb, efficiency)):
    ax2.annotate(f'{y:.1f}%', (x, y), textcoords="offset points",
                 xytext=(0, 10), ha='center', fontsize=10, fontweight='bold')

plt.tight_layout()
plt.savefig('/share_data/zhuyu/chiplet-network-sim/gpu_profiling/results/bandwidth_4gpu_extended_en.png', 
            dpi=300, bbox_inches='tight')
print("Plot saved to: bandwidth_4gpu_extended_en.png")

# Print results summary
print("\n=== 4 GPU AllReduce Bandwidth Sweep Results ===")
print(f"{'Data Size':<12} {'Bandwidth (GB/s)':<18} {'Efficiency (%)':<15}")
print("-" * 48)
for size, bw, eff in zip(data_sizes_mb, bandwidth_gbps, efficiency):
    print(f"{size:<12} {bw:<18.2f} {eff:<15.1f}")
print("-" * 48)
print(f"Peak Bandwidth: {max(bandwidth_gbps):.2f} GB/s (Efficiency: {max(efficiency):.1f}%)")
print(f"Stable Bandwidth: ~{bandwidth_gbps[-1]:.0f} GB/s (for data > 256MB)")
