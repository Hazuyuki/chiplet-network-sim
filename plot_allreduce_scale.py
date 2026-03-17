#!/usr/bin/env python3
"""Plot latency and throughput vs injection rate for all-reduce experiments with different GPU counts."""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "build/output")

# 配置: GPU 数量 -> 文件名
CONFIGS = [
    (8, "nvswitch_allreduce_8gpu_18rate.csv"),
    (16, "nvswitch_allreduce_16gpu_18rate.csv"),
    (32, "nvswitch_allreduce_32gpu_18rate.csv"),
    (64, "nvswitch_allreduce_64gpu_18rate.csv"),
]

# 配色方案
COLORS = {
    8: '#1f77b4',    # 蓝色
    16: '#ff7f0e',   # 橙色
    32: '#2ca02c',   # 绿色
    64: '#d62728',   # 红色
}


def load_csv(path):
    """加载 CSV 文件: injection_rate, latency, throughput"""
    inj, lat, thr = [], [], []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split(",")
            if len(parts) < 3:
                continue
            try:
                inj.append(float(parts[0]))
                lat.append(float(parts[1]))
                thr.append(float(parts[2]))
            except ValueError:
                continue
    return np.array(inj), np.array(lat), np.array(thr)


def main():
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5))

    for gpu_count, fname in CONFIGS:
        path = os.path.join(OUTPUT_DIR, fname)
        if not os.path.isfile(path):
            print(f"Warning: {path} not found, skipping...")
            continue
        
        x, y_lat, y_thr = load_csv(path)
        if len(x) == 0:
            continue
        
        color = COLORS[gpu_count]
        label = f"{gpu_count} GPUs"
        
        # 延迟图
        ax1.plot(x, y_lat, "-", color=color, label=label, linewidth=1.5, marker='o', markersize=3)
        
        # 吞吐图
        ax2.plot(x, y_thr, "-", color=color, label=label, linewidth=1.5, marker='o', markersize=3)

    # 延迟图设置
    ax1.set_xlabel("Injection Rate (flits/(node·cycle))", fontsize=11)
    ax1.set_ylabel("Average Latency (cycles)", fontsize=11)
    ax1.set_title("Average Latency vs Injection Rate", fontsize=12)
    ax1.legend(loc="upper left", fontsize=9)
    ax1.grid(True, linestyle="--", alpha=0.3)
    ax1.set_xlim(left=0)
    ax1.set_ylim(bottom=0)

    # 吞吐图设置
    ax2.set_xlabel("Injection Rate (flits/(node·cycle))", fontsize=11)
    ax2.set_ylabel("Throughput (flits/cycle)", fontsize=11)
    ax2.set_title("Throughput vs Injection Rate", fontsize=12)
    ax2.legend(loc="upper left", fontsize=9)
    ax2.grid(True, linestyle="--", alpha=0.3)
    ax2.set_xlim(left=0)
    ax2.set_ylim(bottom=0)

    plt.tight_layout()
    out_path = os.path.join(SCRIPT_DIR, "build/output/allreduce_scale_comparison.png")
    plt.savefig(out_path, dpi=300, bbox_inches="tight")
    print(f"Saved: {out_path}")


if __name__ == "__main__":
    main()
