#!/usr/bin/env python3
"""Plot latency and throughput vs injection rate for different credit_return_delay."""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "output")

# 与 run_experiment_credit_delay.sh 中 DELAYS 一致
DELAYS = [0, 2, 4, 6, 8, 12, 16, 20, 24]


def load_csv(path):
    """Returns (injection, latency, throughput)."""
    inj, lat, thr = [], [], []
    if not os.path.isfile(path):
        return np.array([]), np.array([]), np.array([])
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split(",")
            if len(parts) < 2:
                continue
            try:
                inj.append(float(parts[0]))
                lat.append(float(parts[1]))
                thr.append(float(parts[2]) if len(parts) >= 3 else 0.0)
            except ValueError:
                continue
    return np.array(inj), np.array(lat), np.array(thr)


def main():
    colors = plt.cm.viridis(np.linspace(0.15, 0.95, len(DELAYS)))

    # 图1: 吞吐(接收率) vs 注入率 —— RTT 影响主图
    fig1, ax1 = plt.subplots(figsize=(9, 6))
    for i, d in enumerate(DELAYS):
        path = os.path.join(OUTPUT_DIR, f"credit_delay_{d}.csv")
        x, _, y = load_csv(path)
        if len(x) == 0:
            print(f"Skip (no data): {path}")
            continue
        ax1.plot(x, y, "-o", color=colors[i], label=f"delay={d}", linewidth=2, markersize=4)
    ax1.set_xlabel("Injection Rate (flits/(node·cycle))", fontsize=11)
    ax1.set_ylabel("Throughput (flits/(node·cycle))", fontsize=11)
    ax1.set_title("Throughput vs Injection Rate: Credit Return Delay (RTT) Effect", fontsize=12)
    ax1.legend(loc="lower right", fontsize=9, ncol=2)
    ax1.grid(True, linestyle="--", alpha=0.4)
    ax1.set_xlim(left=0)
    ax1.set_ylim(bottom=0)
    max_xy = max(ax1.get_xlim()[1], ax1.get_ylim()[1], 1.0)
    ax1.plot([0, max_xy], [0, max_xy], "k--", alpha=0.35, linewidth=1)
    ax1.text(0.95, 0.92, "ideal y=x", transform=ax1.transAxes, fontsize=8, alpha=0.7, ha="right")
    plt.tight_layout()
    out1 = os.path.join(SCRIPT_DIR, "credit_delay_throughput.png")
    plt.savefig(out1, dpi=200, bbox_inches="tight")
    print(f"Saved: {out1}")

    # 图2: 延迟 vs 注入率（辅助）
    fig2, ax2 = plt.subplots(figsize=(8, 5))
    for i, d in enumerate(DELAYS):
        path = os.path.join(OUTPUT_DIR, f"credit_delay_{d}.csv")
        x, y, _ = load_csv(path)
        if len(x) == 0:
            continue
        ax2.plot(x, y, "-o", color=colors[i], label=f"delay={d}", linewidth=1.5, markersize=3)
    ax2.set_xlabel("Injection Rate (flits/(node·cycle))")
    ax2.set_ylabel("Average Latency (cycles)")
    ax2.set_title("Latency vs Injection (RTT has little effect on per-packet latency)")
    ax2.legend(loc="upper left", fontsize=8, ncol=2)
    ax2.grid(True, linestyle="--", alpha=0.3)
    ax2.set_xlim(left=0)
    ax2.set_ylim(bottom=0)
    plt.tight_layout()
    out2 = os.path.join(SCRIPT_DIR, "credit_delay_comparison.png")
    plt.savefig(out2, dpi=150, bbox_inches="tight")
    print(f"Saved: {out2}")


if __name__ == "__main__":
    main()
