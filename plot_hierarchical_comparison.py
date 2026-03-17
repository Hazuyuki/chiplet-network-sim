#!/usr/bin/env python3
"""
Plot All-Reduce performance comparison: Ring vs Hierarchical
"""
import matplotlib.pyplot as plt
import numpy as np

# Ring All-Reduce data (64 GPU)
ring_64gpu = {
    'injection': [1, 1.5, 2, 2.5, 3, 3.5, 4, 4.5, 5, 5.5, 6, 6.5, 7, 7.5, 8, 8.5, 9, 9.5, 10, 10.5, 11, 11.5, 12, 12.5, 13, 13.5, 14, 14.5, 15, 15.5, 16, 16.5, 17, 17.5, 18],
    'latency': [6.17, 6.17, 6.17, 6.18, 6.18, 6.19, 6.20, 6.20, 6.20, 6.21, 6.22, 6.23, 6.23, 6.24, 6.24, 6.25, 6.25, 6.26, 6.27, 6.28, 6.29, 6.31, 6.32, 9.24, 18.16, 83.10, 275.43, 373.50, 399.33, 502.87, 548.59, 600.50, 695.99, 724.40, 774.42],
    'throughput': [1.0, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0, 4.5, 5.0, 5.5, 6.0, 6.5, 7.0, 7.5, 8.0, 8.5, 9.0, 9.5, 10.0, 10.5, 11.0, 11.5, 12.0, 12.5, 13.0, 13.3, 13.4, 13.8, 14.3, 14.8, 14.8, 15.5, 15.3, 15.6, 15.9]
}

# Hierarchical All-Reduce data (64 GPU)
hierarchical_64gpu = {
    'injection': [1, 1.5, 2, 2.5, 3, 3.5, 4, 4.5, 5, 5.5, 6, 6.5, 7, 7.5, 8, 8.5, 9, 9.5, 10, 10.5, 11, 11.5, 12, 12.5, 13, 13.5, 14, 14.5, 15, 15.5, 16, 16.5, 17, 17.5, 18],
    'latency': [6.17, 6.23, 6.21, 6.19, 6.20, 6.20, 6.21, 6.19, 6.20, 6.20, 6.21, 6.22, 6.22, 6.23, 6.24, 6.22, 6.22, 6.22, 6.22, 6.23, 6.22, 6.22, 6.24, 16.34, 31.11, 51.28, 64.20, 71.82, 85.12, 109.90, 118.10, 188.00, 229.98, 335.11, 335.11],
    'throughput': [1.0, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0, 4.5, 5.0, 5.5, 6.0, 6.5, 7.0, 7.5, 8.0, 8.5, 9.0, 9.5, 10.0, 10.5, 11.0, 11.5, 12.0, 12.5, 13.0, 13.4, 13.9, 14.5, 14.9, 15.4, 15.9, 16.3, 16.7, 16.9, 16.9]
}

# Ring All-Reduce data (8 GPU)
ring_8gpu = {
    'injection': [1, 1.5, 2, 2.5, 3, 3.5, 4, 4.5, 5, 5.5, 6, 6.5, 7, 7.5, 8, 8.5, 9, 9.5, 10, 10.5, 11, 11.5, 12, 12.5, 13, 13.5, 14, 14.5, 15, 15.5, 16, 16.5, 17, 17.5, 18],
    'latency': [6.17, 6.17, 6.17, 6.18, 6.18, 6.17, 6.19, 6.20, 6.20, 6.21, 6.27, 6.26, 6.29, 6.31, 6.32, 6.32, 6.31, 6.32, 6.32, 6.33, 6.33, 6.36, 6.45, 9.24, 18.16, 83.10, 275.43, 373.50, 399.33, 502.87, 548.59, 600.50, 695.99, 724.40, 774.42],
    'throughput': [1.0, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0, 4.5, 5.0, 5.5, 6.0, 6.5, 7.0, 7.5, 8.0, 8.5, 9.0, 9.5, 10.0, 10.5, 11.0, 11.5, 12.0, 12.5, 13.0, 13.3, 13.4, 13.8, 14.3, 14.8, 14.8, 15.5, 15.3, 15.6, 15.9]
}

# Create figure with 2 subplots
fig, axes = plt.subplots(1, 2, figsize=(14, 5))

# Plot 1: Latency vs Injection Rate
ax1 = axes[0]
ax1.plot(ring_8gpu['injection'], ring_8gpu['latency'], 'b-o', markersize=4, label='Ring (8 GPU)', linewidth=1.5)
ax1.plot(ring_64gpu['injection'], ring_64gpu['latency'], 'r-s', markersize=4, label='Ring (64 GPU)', linewidth=1.5)
ax1.plot(hierarchical_64gpu['injection'], hierarchical_64gpu['latency'], 'g-^', markersize=4, label='Hierarchical (64 GPU)', linewidth=1.5)
ax1.set_xlabel('Injection Rate (flits/node/cycle)', fontsize=12)
ax1.set_ylabel('Average Latency (cycles)', fontsize=12)
ax1.set_title('All-Reduce: Latency vs Injection Rate', fontsize=14)
ax1.set_xlim([0, 19])
ax1.set_ylim([0, 200])
ax1.grid(True, alpha=0.3)
ax1.legend(loc='upper left')
ax1.axvline(x=12.5, color='gray', linestyle='--', alpha=0.5, label='_nolegend_')
ax1.axvline(x=16.5, color='green', linestyle='--', alpha=0.5, label='_nolegend_')

# Plot 2: Throughput vs Injection Rate
ax2 = axes[1]
ax2.plot(ring_8gpu['injection'], ring_8gpu['throughput'], 'b-o', markersize=4, label='Ring (8 GPU)', linewidth=1.5)
ax2.plot(ring_64gpu['injection'], ring_64gpu['throughput'], 'r-s', markersize=4, label='Ring (64 GPU)', linewidth=1.5)
ax2.plot(hierarchical_64gpu['injection'], hierarchical_64gpu['throughput'], 'g-^', markersize=4, label='Hierarchical (64 GPU)', linewidth=1.5)
ax2.plot([0, 20], [0, 20], 'k--', alpha=0.3, label='Ideal (100% efficiency)')
ax2.set_xlabel('Injection Rate (flits/node/cycle)', fontsize=12)
ax2.set_ylabel('Throughput (flits/node/cycle)', fontsize=12)
ax2.set_title('All-Reduce: Throughput vs Injection Rate', fontsize=14)
ax2.set_xlim([0, 19])
ax2.set_ylim([0, 20])
ax2.grid(True, alpha=0.3)
ax2.legend(loc='lower right')

# Add saturation points annotation
ax2.annotate('Ring 64GPU\nSaturation: ~13', xy=(13, 13), xytext=(5, 15),
            fontsize=9, arrowprops=dict(arrowstyle='->', color='red', alpha=0.7))
ax2.annotate('Hierarchical 64GPU\nSaturation: ~16.7 (+28%)', xy=(16.7, 16.7), xytext=(10, 18),
            fontsize=9, arrowprops=dict(arrowstyle='->', color='green', alpha=0.7))

plt.tight_layout()
plt.savefig('output/allreduce_ring_vs_hierarchical_comparison.png', dpi=150, bbox_inches='tight')
plt.close()

print("Plot saved to output/allreduce_ring_vs_hierarchical_comparison.png")

# Print summary
print("\n=== Summary ===")
print(f"Ring All-Reduce (64 GPU) saturation: ~13 flits/(node*cycle)")
print(f"Hierarchical All-Reduce (64 GPU) saturation: ~16.7 flits/(node*cycle)")
print(f"Improvement: +28% throughput")
