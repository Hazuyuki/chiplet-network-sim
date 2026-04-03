#!/usr/bin/env python3
import matplotlib.pyplot as plt
import numpy as np

# 64 bytes per flit, NVLink ~1GHz
FLIT_SIZE = 64  # bytes
PAYLOAD_SIZE = 50  # 有效载荷 bytes (50/64 有效带宽)

data_size_flits = [1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144]
throughput_flits = [3.28, 6.37, 11.22, 13.29, 14.30, 15.44, 16.64, 17.34, 17.69]
cycles = [547, 563, 639, 1079, 2005, 3713, 6891, 13229, 25937]
time_s = [0, 0, 0, 1, 0, 5, 106, 713, 3577]

# 转换: flits -> MB (64B/flit), throughput -> 有效带宽 GB/s (50B有效载荷 / 64B flit @ 1GHz)
data_size_mb = [d * FLIT_SIZE / (1024 * 1024) for d in data_size_flits]
throughput_effective = [t * PAYLOAD_SIZE for t in throughput_flits]  # 有效带宽 GB/s (50/64)

fig, axes = plt.subplots(2, 2, figsize=(14, 10))
fig.suptitle("Collective Ring All-Reduce: Data Size Sweep (64B/flit @ 1GHz)", fontsize=14, fontweight="bold")

ax1 = axes[0, 0]
ax1.plot(data_size_mb, throughput_effective, "bo-", linewidth=2, markersize=8)
ax1.axhline(y=17.69 * PAYLOAD_SIZE, color="r", linestyle="--", alpha=0.5, label=f"Saturated (~{17.69 * PAYLOAD_SIZE:.0f} GB/s effective)")
ax1.set_xlabel("Data Size (MB, 64B/flit)")
ax1.set_ylabel("Effective Throughput (GB/s per GPU, 50/64 payload)")
ax1.set_title("Effective Bandwidth vs Data Size")
ax1.grid(True, alpha=0.3)
ax1.legend()
ax1.set_xscale("log")
# 标注所有数据点的横坐标
ax1.set_xticks(data_size_mb)
ax1.set_xticklabels([f"{d:.2f}" for d in data_size_mb], rotation=45, ha='right', fontsize=8)

ax2 = axes[0, 1]
ax2.plot(data_size_mb, cycles, "rs-", linewidth=2, markersize=8)
ax2.set_xlabel("Data Size (MB, 64B/flit)")
ax2.set_ylabel("Cycles")
ax2.set_title("Cycles vs Data Size")
ax2.grid(True, alpha=0.3)
ax2.set_xscale("log")
# 标注所有数据点的横坐标
ax2.set_xticks(data_size_mb)
ax2.set_xticklabels([f"{d:.2f}" for d in data_size_mb], rotation=45, ha='right', fontsize=8)

ax3 = axes[1, 0]
x_labels = [f"{d:.2f}" for d in data_size_mb]
ax3.bar(x_labels, time_s, color="steelblue")
ax3.set_xlabel("Data Size (MB)")
ax3.set_ylabel("Time (s)")
ax3.set_title("Execution Time")
ax3.grid(True, alpha=0.3, axis="y")
ax3.tick_params(axis='x', rotation=45, labelsize=8)

ax4 = axes[1, 1]
# 效率: throughput / 理论最大有效带宽 (18 flits/cycle * 50 = 900 GB/s)
efficiency = [t / 18 * 100 for t in throughput_flits]
ax4.bar(x_labels, efficiency, color="green", alpha=0.7)
ax4.axhline(y=87.5, color="r", linestyle="--", label="Ring theory (87.5%)")
ax4.set_xlabel("Data Size (MB)")
ax4.set_ylabel("Efficiency (%)")
ax4.set_title("Bandwidth Utilization (vs 900 GB/s effective theoretical)")
ax4.set_ylim(0, 110)
ax4.legend()
ax4.grid(True, alpha=0.3, axis="y")
ax4.tick_params(axis='x', rotation=45, labelsize=8)

plt.tight_layout()
plt.savefig("output/datasize_sweep_results.png", dpi=150)
plt.savefig("output/datasize_sweep_results.pdf")
print("Done! Saved to output/datasize_sweep_results.png")
