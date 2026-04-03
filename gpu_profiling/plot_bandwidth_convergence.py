#!/usr/bin/env python3
"""
Plot 4 GPU AllReduce Bandwidth Convergence
Data sizes: 128KB to 16GB (18 data points)
Shows bandwidth convergence to peak value
"""

import matplotlib.pyplot as plt
import numpy as np

# Test results (convergence test)
data_sizes_mb = [0.125, 0.25, 0.5, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384]  # MB
bandwidth_gbps = [5.32, 10.19, 10.65, 21.90, 44.54, 59.32, 81.62, 114.97, 142.48, 166.93, 177.69, 182.80, 189.54, 195.16, 200.60, 201.41, 203.36, 204.29]  # GB/s

# Theoretical max bandwidth (4 GPU)
theoretical_max = 225  # GB/s

# Calculate convergence metrics
peak_bandwidth = max(bandwidth_gbps)
convergence_idx = bandwidth_gbps.index(peak_bandwidth)

# Create figure with two subplots
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(18, 7))

# Left plot: Bandwidth vs Data Size (log scale)
ax1.plot(data_sizes_mb, bandwidth_gbps, 'o-', linewidth=2.5, markersize=8, 
         color='#2E86AB', label='Measured Bandwidth', zorder=3)
ax1.axhline(y=theoretical_max, color='#A23B72', linestyle='--', linewidth=2, 
            label=f'Theoretical Peak ({theoretical_max} GB/s)', zorder=2)
ax1.axhline(y=peak_bandwidth, color='#F18F01', linestyle=':', linewidth=2.5,
            label=f'Converged Peak ({peak_bandwidth:.2f} GB/s)', zorder=2)

# Mark convergence point
ax1.axvline(x=data_sizes_mb[convergence_idx], color='#28A745', linestyle='-.', 
            linewidth=2, alpha=0.7, label=f'Convergence at {data_sizes_mb[convergence_idx]/1024:.1f} GB')

ax1.set_xscale('log', base=2)
ax1.set_xlabel('Data Size (MB)', fontsize=14, fontweight='bold')
ax1.set_ylabel('Effective Bandwidth (GB/s)', fontsize=14, fontweight='bold')
ax1.set_title('4 GPU AllReduce Bandwidth Convergence (128KB - 16GB)', fontsize=16, fontweight='bold')
ax1.grid(True, alpha=0.3, linestyle='-', linewidth=0.5, zorder=1)
ax1.legend(fontsize=11, loc='lower right')
ax1.tick_params(labelsize=12)

# Annotate key convergence points
key_indices = [0, 4, 7, 11, 14, 17]  # 128KB, 2MB, 16MB, 256MB, 2GB, 16GB
for i in key_indices:
    x, y = data_sizes_mb[i], bandwidth_gbps[i]
    ax1.annotate(f'{y:.1f}', (x, y), textcoords="offset points", 
                 xytext=(0, 10), ha='center', fontsize=9, fontweight='bold')

# Right plot: Efficiency vs Data Size
efficiency = [bw / theoretical_max * 100 for bw in bandwidth_gbps]
ax2.plot(data_sizes_mb, efficiency, 's-', linewidth=2.5, markersize=8,
         color='#C73E1D', label='Efficiency', zorder=3)
ax2.axhline(y=100, color='#A23B72', linestyle='--', linewidth=2,
            label='100% Efficiency', zorder=2)
ax2.axhline(y=efficiency[convergence_idx], color='#F18F01', linestyle=':', linewidth=2.5,
            label=f'Converged Efficiency ({efficiency[convergence_idx]:.1f}%)', zorder=2)
ax2.axvline(x=data_sizes_mb[convergence_idx], color='#28A745', linestyle='-.', 
            linewidth=2, alpha=0.7, label=f'Convergence at {data_sizes_mb[convergence_idx]/1024:.1f} GB')

ax2.set_xscale('log', base=2)
ax2.set_xlabel('Data Size (MB)', fontsize=14, fontweight='bold')
ax2.set_ylabel('Efficiency (%)', fontsize=14, fontweight='bold')
ax2.set_title('Bandwidth Efficiency Convergence', fontsize=16, fontweight='bold')
ax2.grid(True, alpha=0.3, linestyle='-', linewidth=0.5, zorder=1)
ax2.legend(fontsize=11, loc='lower right')
ax2.tick_params(labelsize=12)
ax2.set_ylim([0, 115])

# Annotate key efficiency points
for i in key_indices:
    x, y = data_sizes_mb[i], efficiency[i]
    ax2.annotate(f'{y:.1f}%', (x, y), textcoords="offset points",
                 xytext=(0, 10), ha='center', fontsize=9, fontweight='bold')

plt.tight_layout()
plt.savefig('/share_data/zhuyu/chiplet-network-sim/gpu_profiling/results/bandwidth_4gpu_convergence.png', 
            dpi=300, bbox_inches='tight')
print("Convergence plot saved to: bandwidth_4gpu_convergence.png")

# Print convergence analysis
print("\n=== 4 GPU AllReduce Bandwidth Convergence Analysis ===")
print(f"\n{'Data Size':<12} {'Bandwidth (GB/s)':<18} {'Efficiency (%)':<15} {'Δ Bandwidth'}")
print("-" * 70)

for i in range(len(data_sizes_mb)):
    delta = f"+{bandwidth_gbps[i] - bandwidth_gbps[i-1]:.2f}" if i > 0 else "-"
    print(f"{data_sizes_mb[i]:<12.3f} {bandwidth_gbps[i]:<18.2f} {efficiency[i]:<15.1f} {delta}")

print("-" * 70)
print(f"\nConvergence Analysis:")
print(f"  Peak Bandwidth: {peak_bandwidth:.2f} GB/s (at {data_sizes_mb[convergence_idx]/1024:.1f} GB)")
print(f"  Peak Efficiency: {efficiency[convergence_idx]:.1f}% of theoretical max")
print(f"  Convergence achieved at: {data_sizes_mb[convergence_idx]/1024:.1f} GB")
print(f"\n  Bandwidth Growth Rate (last 5 points):")
for i in range(-5, 0):
    growth = (bandwidth_gbps[i] - bandwidth_gbps[i-1]) / bandwidth_gbps[i-1] * 100
    print(f"    {data_sizes_mb[i-1]/1024:.0f}GB → {data_sizes_mb[i]/1024:.0f}GB: {growth:.2f}% increase")

print(f"\n  Stability Analysis:")
print(f"    Std dev (last 5 points): {np.std(bandwidth_gbps[-5:]):.2f} GB/s")
print(f"    Range (last 5 points): {max(bandwidth_gbps[-5:]) - min(bandwidth_gbps[-5:]):.2f} GB/s")
print(f"    Coefficient of variation: {np.std(bandwidth_gbps[-5:]) / np.mean(bandwidth_gbps[-5:]) * 100:.2f}%")

# Calculate when 90%, 95%, 99% of peak is reached
for threshold in [0.9, 0.95, 0.99]:
    threshold_bw = peak_bandwidth * threshold
    for i, bw in enumerate(bandwidth_gbps):
        if bw >= threshold_bw:
            print(f"\n  {threshold*100:.0f}% of peak ({threshold_bw:.2f} GB/s) reached at: {data_sizes_mb[i]/1024:.1f} GB")
            break
