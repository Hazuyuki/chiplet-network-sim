#!/usr/bin/env python3
"""
绘制4 GPU AllReduce带宽扫描结果
数据大小: 1MB 到 8GB (扩展测试)
"""

import matplotlib.pyplot as plt
import numpy as np

# 测试结果数据
data_sizes_mb = [1, 4, 16, 64, 256, 1024, 2048, 4096, 8192]  # MB
bandwidth_gbps = [31.24, 52.23, 129.63, 168.54, 185.38, 193.30, 201.36, 203.13, 204.72]  # GB/s

# 理论最大带宽 (4 GPU, 每GPU 18 links × 50 bytes/link × 1 GHz = 900 GB/s总带宽)
# AllReduce有效带宽 = 总带宽 / 4 = 225 GB/s (假设完美线性扩展)
# 但实际测试显示稳定在~205 GB/s
theoretical_max = 225  # GB/s

# 创建图形
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 6))

# 左图: 带宽 vs 数据大小
ax1.plot(data_sizes_mb, bandwidth_gbps, 'o-', linewidth=2, markersize=8, 
         color='#2E86AB', label='实测带宽', zorder=3)
ax1.axhline(y=theoretical_max, color='#A23B72', linestyle='--', linewidth=2, 
            label=f'理论峰值 ({theoretical_max} GB/s)', zorder=2)
ax1.axhline(y=bandwidth_gbps[-1], color='#F18F01', linestyle=':', linewidth=2,
            label=f'实测峰值 ({bandwidth_gbps[-1]:.1f} GB/s)', zorder=2)

# 设置对数坐标轴
ax1.set_xscale('log', base=2)
ax1.set_xlabel('数据大小 (MB)', fontsize=14, fontweight='bold')
ax1.set_ylabel('有效带宽 (GB/s)', fontsize=14, fontweight='bold')
ax1.set_title('4 GPU AllReduce 带宽扫描 (1MB - 8GB)', fontsize=16, fontweight='bold')
ax1.grid(True, alpha=0.3, linestyle='-', linewidth=0.5, zorder=1)
ax1.legend(fontsize=12, loc='lower right')
ax1.tick_params(labelsize=12)

# 在每个数据点上标注带宽值
for i, (x, y) in enumerate(zip(data_sizes_mb, bandwidth_gbps)):
    ax1.annotate(f'{y:.1f}', (x, y), textcoords="offset points", 
                 xytext=(0, 10), ha='center', fontsize=10, fontweight='bold')

# 右图: 效率 vs 数据大小
efficiency = [bw / theoretical_max * 100 for bw in bandwidth_gbps]
ax2.plot(data_sizes_mb, efficiency, 's-', linewidth=2, markersize=8,
         color='#C73E1D', label='效率', zorder=3)
ax2.axhline(y=100, color='#A23B72', linestyle='--', linewidth=2,
            label='100% 效率', zorder=2)

ax2.set_xscale('log', base=2)
ax2.set_xlabel('数据大小 (MB)', fontsize=14, fontweight='bold')
ax2.set_ylabel('效率 (%)', fontsize=14, fontweight='bold')
ax2.set_title('带宽效率 vs 数据大小', fontsize=16, fontweight='bold')
ax2.grid(True, alpha=0.3, linestyle='-', linewidth=0.5, zorder=1)
ax2.legend(fontsize=12, loc='lower right')
ax2.tick_params(labelsize=12)
ax2.set_ylim([0, 110])

# 在每个数据点上标注效率值
for i, (x, y) in enumerate(zip(data_sizes_mb, efficiency)):
    ax2.annotate(f'{y:.1f}%', (x, y), textcoords="offset points",
                 xytext=(0, 10), ha='center', fontsize=10, fontweight='bold')

plt.tight_layout()
plt.savefig('/share_data/zhuyu/chiplet-network-sim/gpu_profiling/results/bandwidth_4gpu_extended.png', 
            dpi=300, bbox_inches='tight')
print("图表已保存到: bandwidth_4gpu_extended.png")

# 打印结果摘要
print("\n=== 4 GPU AllReduce 带宽扫描结果 ===")
print(f"{'数据大小':<12} {'带宽 (GB/s)':<15} {'效率 (%)':<10}")
print("-" * 40)
for size, bw, eff in zip(data_sizes_mb, bandwidth_gbps, efficiency):
    print(f"{size:<12} {bw:<15.2f} {eff:<10.1f}")
print("-" * 40)
print(f"峰值带宽: {max(bandwidth_gbps):.2f} GB/s (效率: {max(efficiency):.1f}%)")
print(f"稳定带宽: ~{bandwidth_gbps[-1]:.0f} GB/s (>256MB)")
