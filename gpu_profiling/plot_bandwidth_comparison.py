#!/usr/bin/env python3
"""
Compare coarse-grained vs fine-grained 4 GPU AllReduce results
"""

import matplotlib.pyplot as plt
import numpy as np

# Coarse-grained results (9 points)
data_sizes_coarse = [1, 4, 16, 64, 256, 1024, 2048, 4096, 8192]  # MB
bandwidth_coarse = [31.24, 52.23, 129.63, 168.54, 185.38, 193.30, 201.36, 203.13, 204.72]  # GB/s

# Fine-grained results (17 points)
data_sizes_fine = [0.125, 0.25, 0.5, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192]  # MB
bandwidth_fine = [4.94, 10.06, 18.56, 20.17, 45.09, 60.57, 85.66, 129.98, 146.91, 164.21, 176.19, 185.55, 191.37, 198.43, 201.28, 203.14, 204.61]  # GB/s

# Theoretical max
theoretical_max = 225  # GB/s

# Create figure
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(18, 7))

# Left plot: Full range comparison
ax1.plot(data_sizes_fine, bandwidth_fine, 'o-', linewidth=2, markersize=7, 
         color='#2E86AB', label='Fine-grained (17 points)', zorder=3, alpha=0.8)
ax1.plot(data_sizes_coarse, bandwidth_coarse, 's--', linewidth=2, markersize=8, 
         color='#A23B72', label='Coarse-grained (9 points)', zorder=4, markerfacecolor='none', markeredgewidth=2)
ax1.axhline(y=theoretical_max, color='#F18F01', linestyle=':', linewidth=2,
            label=f'Theoretical Peak ({theoretical_max} GB/s)', zorder=2)

ax1.set_xscale('log', base=2)
ax1.set_xlabel('Data Size (MB)', fontsize=14, fontweight='bold')
ax1.set_ylabel('Effective Bandwidth (GB/s)', fontsize=14, fontweight='bold')
ax1.set_title('4 GPU AllReduce: Coarse vs Fine-grained', fontsize=16, fontweight='bold')
ax1.grid(True, alpha=0.3, linestyle='-', linewidth=0.5, zorder=1)
ax1.legend(fontsize=12, loc='lower right')
ax1.tick_params(labelsize=12)

# Right plot: Small data region (linear scale)
small_indices_fine = [i for i, x in enumerate(data_sizes_fine) if x <= 16]
small_indices_coarse = [i for i, x in enumerate(data_sizes_coarse) if x <= 16]

ax2.plot([data_sizes_fine[i] for i in small_indices_fine], 
         [bandwidth_fine[i] for i in small_indices_fine],
         'o-', linewidth=2, markersize=8, color='#2E86AB', 
         label='Fine-grained', zorder=3)
ax2.plot([data_sizes_coarse[i] for i in small_indices_coarse], 
         [bandwidth_coarse[i] for i in small_indices_coarse],
         's--', linewidth=2, markersize=10, color='#A23B72', 
         label='Coarse-grained', zorder=4, markerfacecolor='none', markeredgewidth=2)

ax2.set_xlabel('Data Size (MB)', fontsize=14, fontweight='bold')
ax2.set_ylabel('Effective Bandwidth (GB/s)', fontsize=14, fontweight='bold')
ax2.set_title('Small Data Region (Linear Scale)', fontsize=16, fontweight='bold')
ax2.grid(True, alpha=0.3, linestyle='-', linewidth=0.5, zorder=1)
ax2.legend(fontsize=12, loc='upper left')
ax2.tick_params(labelsize=12)

# Annotate data points
for i in small_indices_fine:
    x, y = data_sizes_fine[i], bandwidth_fine[i]
    ax2.annotate(f'{y:.1f}', (x, y), textcoords="offset points", 
                 xytext=(0, 8), ha='center', fontsize=9, fontweight='bold')

plt.tight_layout()
plt.savefig('/share_data/zhuyu/chiplet-network-sim/gpu_profiling/results/bandwidth_4gpu_comparison.png', 
            dpi=300, bbox_inches='tight')
print("Comparison plot saved to: bandwidth_4gpu_comparison.png")

# Print comparison summary
print("\n=== Comparison: Coarse vs Fine-grained Results ===")
print(f"\n{'Data Size':<12} {'Coarse (GB/s)':<15} {'Fine (GB/s)':<15} {'Difference'}")
print("-" * 60)

# Match common data points
for i, size in enumerate(data_sizes_coarse):
    fine_idx = data_sizes_fine.index(size)
    bw_coarse = bandwidth_coarse[i]
    bw_fine = bandwidth_fine[fine_idx]
    diff = abs(bw_coarse - bw_fine)
    print(f"{size:<12.1f} {bw_coarse:<15.2f} {bw_fine:<15.2f} {diff:.2f} GB/s")

print("-" * 60)
print(f"\nPeak Bandwidth:")
print(f"  Coarse-grained: {max(bandwidth_coarse):.2f} GB/s")
print(f"  Fine-grained:   {max(bandwidth_fine):.2f} GB/s")
print(f"\nFine-grained reveals more detail in small data region:")
print(f"  - 128KB: {bandwidth_fine[0]:.2f} GB/s (2.2% efficiency)")
print(f"  - 256KB: {bandwidth_fine[1]:.2f} GB/s (4.5% efficiency)")
print(f"  - 512KB: {bandwidth_fine[2]:.2f} GB/s (8.2% efficiency)")
