import matplotlib.pyplot as plt
import numpy as np

# 8 GPU 数据
data_8gpu = {
    "data_size": [1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144],
    "throughput": [3.276, 6.366, 11.218, 13.286, 14.300, 15.444, 16.643, 17.339, 17.687]
}

# 64 GPU 数据
data_64gpu = {
    "data_size": [1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144],
    "throughput": [0.587, 0.740, 1.474, 2.883, 3.903, 5.467, 8.112, 10.295, 11.903]
}

# 计算数据大小 (MB)
size_mb_8 = [d * 8 / 1024 / 1024 for d in data_8gpu["data_size"]]
size_mb_64 = [d * 8 / 1024 / 1024 for d in data_64gpu["data_size"]]

# 创建图表
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6))

# 图1: 吞吐量 vs 数据大小
ax1.plot(size_mb_8, data_8gpu["throughput"], "o-", label="8 GPU", linewidth=2, markersize=8, color="blue")
ax1.plot(size_mb_64, data_64gpu["throughput"], "s-", label="64 GPU", linewidth=2, markersize=8, color="red")

ax1.set_xlabel("Data Size (MB)", fontsize=12)
ax1.set_ylabel("Throughput (flits/node/cycle)", fontsize=12)
ax1.set_title("collective_ring_all_reduce: 8 GPU vs 64 GPU", fontsize=14)
ax1.legend(fontsize=11)
ax1.grid(True, alpha=0.3)
ax1.set_xscale("log", base=2)

# 添加理论带宽参考线
# 8 GPU: 18 ports * 0.5 = 9 flits/cycle 单向，双向 = 18 flits/cycle per node
# 但 ring all-reduce 需要 (n-1)/n 的带宽利用率
ax1.axhline(y=17.5, color="blue", linestyle="--", alpha=0.5, label="8 GPU 理论峰值 ~17.5")
ax1.axhline(y=12, color="red", linestyle="--", alpha=0.5, label="64 GPU 实际峰值 ~12")

# 图2: 吞吐量比值 (64 GPU / 8 GPU)
ratio = [d64 / d8 for d64, d8 in zip(data_64gpu["throughput"], data_8gpu["throughput"])]
ax2.plot(size_mb_8, ratio, "o-", linewidth=2, markersize=8, color="green")
ax2.set_xlabel("Data Size (MB)", fontsize=12)
ax2.set_ylabel("Throughput Ratio (64 GPU / 8 GPU)", fontsize=12)
ax2.set_title("Throughput Ratio: 64 GPU relative to 8 GPU", fontsize=14)
ax2.grid(True, alpha=0.3)
ax2.set_xscale("log", base=2)
ax2.axhline(y=1.0, color="gray", linestyle="--", alpha=0.5)

plt.tight_layout()
plt.savefig("output/8gpu_vs_64gpu_comparison.png", dpi=150, bbox_inches="tight")
plt.savefig("output/8gpu_vs_64gpu_comparison.pdf", bbox_inches="tight")
print("图表已保存: output/8gpu_vs_64gpu_comparison.png")
