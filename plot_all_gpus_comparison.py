import matplotlib.pyplot as plt
import numpy as np

# 数据
data_sizes = [1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144]
size_mb = [d * 8 / 1024 / 1024 for d in data_sizes]

throughput_4gpu = [6.707, 13.185, 13.328, 14.681, 15.664, 16.500, 16.969, 16.512, 15.203]
throughput_8gpu = [3.276, 6.366, 11.218, 13.286, 14.300, 15.444, 16.643, 17.339, 17.687]
throughput_64gpu = [0.587, 0.740, 1.474, 2.883, 3.903, 5.467, 8.112, 10.295, 11.903]

# 创建图表
fig, axes = plt.subplots(1, 2, figsize=(14, 6))

# 图1: 吞吐量对比
ax1 = axes[0]
ax1.plot(size_mb, throughput_4gpu, "o-", label="4 GPU", linewidth=2, markersize=8, color="green")
ax1.plot(size_mb, throughput_8gpu, "s-", label="8 GPU", linewidth=2, markersize=8, color="blue")
ax1.plot(size_mb, throughput_64gpu, "^-", label="64 GPU", linewidth=2, markersize=8, color="red")

ax1.set_xlabel("Data Size (MB)", fontsize=12)
ax1.set_ylabel("Throughput (flits/node/cycle)", fontsize=12)
ax1.set_title("collective_ring_all_reduce: Throughput Comparison", fontsize=14)
ax1.legend(fontsize=11)
ax1.grid(True, alpha=0.3)
ax1.set_xscale("log", base=2)

# 图2: 相对于 8 GPU 的效率
ax2 = axes[1]
efficiency_4gpu = [t4/t8 if t8 > 0 else 0 for t4, t8 in zip(throughput_4gpu, throughput_8gpu)]
efficiency_64gpu = [t64/t8 if t8 > 0 else 0 for t64, t8 in zip(throughput_64gpu, throughput_8gpu)]

ax2.plot(size_mb, efficiency_4gpu, "o-", label="4 GPU / 8 GPU", linewidth=2, markersize=8, color="green")
ax2.plot(size_mb, efficiency_64gpu, "^-", label="64 GPU / 8 GPU", linewidth=2, markersize=8, color="red")

ax2.axhline(y=1.0, color="gray", linestyle="--", alpha=0.5, label="Baseline (8 GPU)")
ax2.set_xlabel("Data Size (MB)", fontsize=12)
ax2.set_ylabel("Relative Efficiency", fontsize=12)
ax2.set_title("Efficiency Relative to 8 GPU", fontsize=14)
ax2.legend(fontsize=11)
ax2.grid(True, alpha=0.3)
ax2.set_xscale("log", base=2)

plt.tight_layout()
plt.savefig("output/all_gpus_comparison.png", dpi=150, bbox_inches="tight")
plt.savefig("output/all_gpus_comparison.pdf", bbox_inches="tight")
print("图表已保存: output/all_gpus_comparison.png")

# 打印峰值吞吐量
print("\n=== 峰值吞吐量 ===")
print(f"4 GPU:  {max(throughput_4gpu):.2f} flits/node/cycle (at 65536)")
print(f"8 GPU:  {max(throughput_8gpu):.2f} flits/node/cycle (at 262144)")
print(f"64 GPU: {max(throughput_64gpu):.2f} flits/node/cycle (at 262144)")
