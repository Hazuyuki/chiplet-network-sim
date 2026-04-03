#!/usr/bin/env python3
"""
Plot 4 GPU AllReduce Bandwidth Sweep Results (Fine-grained)
Data sizes: 128KB to 8GB (17 data points)
"""

import matplotlib.pyplot as plt
import numpy as np

# Test results (fine-grained)
data_sizes_mb = [0.125, 0.25, 0.5, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192]  # MB
bandwidth_gbps = [4.94, 10.06, 18.56, 20.17, 45.09, 60.57, 85.66, 129.98, 146.91, 164.21, 176.19, 185.55, 191.37, 198.43, 201.28, 203.14, 204.61]  # GB/s

# Theoretical max bandwidth (4 GPU)
theoretical_max = 225  # GB/s

# Create figure with two subplots
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(18, 7))

# Left plot: Bandwidth vs Data Size (linear scale for small data)
ax1.plot(data_sizes_mb, bandwidth_gbps, 'o-', linewidth=2, markersize=8, 
         color='#2E86AB', label='Measured Bandwidth', zorder=3)
ax1.axhline(y=theoretical_max, color='#A23B72', linestyle='--', linewidth=2, 
            label=f'Theoretical Peak ({theoretical_max} GB/s)', zorder=2)
ax1.axhline(y=bandwidth_gbps[-1], color='#F18F01', linestyle=':', linewidth=2,
            label=f'Measured Peak ({bandwidth_gbps[-1]:.1f} GB/s)', zorder=2)

ax1.set_xscale('log', base=2)
ax1.set_xlabel('Data Size (MB)', fontsize=14, fontweight='bold')
ax1.set_ylabel('Effective Bandwidth (GB/s)', fontsize=14, fontweight='bold')
ax1.set_title('4 GPU AllReduce Bandwidth (Fine-grained, 128KB - 8GB)', fontsize=16, fontweight='bold')
ax1.grid(True, alpha=0.3, linestyle='-', linewidth=0.5, zorder=1)
ax1.legend(fontsize=11, loc='lower right')
ax1.tick_params(labelsize=12)

# Annotate key data points
key_indices = [0, 3, 7, 11, 14, 16]  # 128KB, 1MB, 16MB, 256MB, 2GB, 8GB
for i in key_indices:
    x, y = data_sizes_mb[i], bandwidth_gbps[i]
    ax1.annotate(f'{y:.1f}', (x, y), textcoords="offset points", 
                 xytext=(0, 10), ha='center', fontsize=9, fontweight='bold')

# Right plot: Efficiency vs Data Size
efficiency = [bw / theoretical_max * 100 for bw in bandwidth_gbps]
ax2.plot(data_sizes_mb, efficiency, 's-', linewidth=2, markersize=8,
         color='#C73E1D', label='Efficiency', zorder=3)
ax2.axhline(y=100, color='#A23B72', linestyle='--', linewidth=2,
            label='100% Efficiency', zorder=2)
ax2.axhline(y=90, color='#F18F01', linestyle=':', linewidth=2,
            label='90% Efficiency', zorder=2)

ax2.set_xscale('log', base=2)
ax2.set_xlabel('Data Size (MB)', fontsize=14, fontweight='bold')
ax2.set_ylabel('Efficiency (%)', fontsize=14, fontweight='bold')
ax2.set_title('Bandwidth Efficiency (Fine-grained)', fontsize=16, fontweight='bold')
ax2.grid(True, alpha=0.3, linestyle='-', linewidth=0.5, zorder=1)
ax2.legend(fontsize=11, loc='lower right')
ax2.tick_params(labelsize=12)
ax2.set_ylim([0, 110])

# Annotate key efficiency points
for i in key_indices:
    x, y = data_sizes_mb[i], efficiency[i]
    ax2.annotate(f'{y:.1f}%', (x, y), textcoords="offset points",
                 xytext=(0, 10), ha='center', fontsize=9, fontweight='bold')

plt.tight_layout()
plt.savefig('/share_data/zhuyu/chiplet-network-sim/gpu_profiling/results/bandwidth_4gpu_fine_grained.png', 
            dpi=300, bbox_inches='tight')
print("Plot saved to: bandwidth_4gpu_fine_grained.png")

# Print results summary
print("\n=== 4 GPU AllReduce Bandwidth Sweep (Fine-grained) ===")
print(f"{'Data Size':<12} {'Bandwidth (GB/s)':<18} {'Efficiency (%)':<15} {'Region'}")
print("-" * 70)

regions = ['Startup'] * 4 + ['Rapid Growth'] * 4 + ['Saturation'] * 5 + ['Stable'] * 4
for size, bw, eff, region in zip(data_sizes_mb, bandwidth_gbps, efficiency, regions):
    print(f"{size:<12.3f} {bw:<18.2f} {eff:<15.1f} {region}")

print("-" * 70)
print(f"Peak Bandwidth: {max(bandwidth_gbps):.2f} GB/s (Efficiency: {max(efficiency):.1f}%)")
print(f"Stable Bandwidth: ~{bandwidth_gbps[-1]:.0f} GB/s (for data > 256MB)")
print(f"\nKey Observations:")
print(f"  - Startup phase (<1MB): {bandwidth_gbps[0]:.1f} - {bandwidth_gbps[3]:.1f} GB/s (latency dominated)")
print(f"  - Rapid growth (1-16MB): {bandwidth_gbps[3]:.1f} - {bandwidth_gbps[7]:.1f} GB/s")
print(f"  - Saturation (16-256MB): {bandwidth_gbps[7]:.1f} - {bandwidth_gbps[11]:.1f} GB/s")
print(f"  - Stable region (>256MB): {bandwidth_gbps[11]:.1f} - {bandwidth_gbps[-1]:.1f} GB/s")
