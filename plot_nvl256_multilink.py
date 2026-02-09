#!/usr/bin/env python3
"""Plot NVL256 multilink Ring AllReduce: throughput & latency vs injection for 8/16/32/64/128/256 GPU."""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "output")
OUTPUT_PNG = os.path.join(OUTPUT_DIR, "nvl256_multilink_curves.png")

# (label, csv filename under output/)
CONFIGS = [
    ("8 GPU", "nvswitch_nvl256_multilink_8gpu.csv"),
    ("16 GPU", "nvswitch_nvl256_multilink_16gpu.csv"),
    ("32 GPU", "nvswitch_nvl256_multilink_32gpu.csv"),
    ("64 GPU", "nvswitch_nvl256_multilink.csv"),
    ("128 GPU", "nvswitch_nvl256_multilink_128gpu.csv"),
    ("256 GPU", "nvswitch_nvl256_multilink_256gpu.csv"),
]


def load_csv(path):
    """CSV columns: injection_rate, avg_latency, throughput"""
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
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(9, 9), sharex=True)
    colors = plt.cm.tab10(np.linspace(0, 1, len(CONFIGS)))
    markers = ["o", "s", "^", "D", "v", "P"]

    for i, (label, fname) in enumerate(CONFIGS):
        path = os.path.join(OUTPUT_DIR, fname)
        if not os.path.isfile(path):
            print(f"Skip (not found): {path}")
            continue
        inj, lat, thr = load_csv(path)
        if len(inj) == 0:
            print(f"Skip (no data): {path}")
            continue
        c, m = colors[i], markers[i % len(markers)]
        ax1.plot(inj, thr, "-", color=c, marker=m, markersize=3.5, linewidth=1.2, label=label)
        ax2.plot(inj, lat, "-", color=c, marker=m, markersize=3.5, linewidth=1.2, label=label)

    # Ideal line on throughput plot
    xmax = 20
    ax1.plot([0, xmax], [0, xmax], "k--", linewidth=1, alpha=0.6, label="Ideal (throughput=injection)")

    ax1.set_ylabel("Throughput (flits/(node·cycle))")
    ax1.set_title("NVL256 Multilink Ring AllReduce: Throughput & Latency vs Injection Rate")
    ax1.legend(loc="upper left", ncol=2, fontsize=9)
    ax1.grid(True, linestyle="--", alpha=0.3)
    ax1.set_xlim(0, xmax)
    ax1.set_ylim(bottom=0)

    ax2.set_xlabel("Injection rate (flits/(node·cycle))")
    ax2.set_ylabel("Average latency (cycles)")
    ax2.legend(loc="upper left", ncol=2, fontsize=9)
    ax2.grid(True, linestyle="--", alpha=0.3)
    ax2.set_xlim(0, xmax)
    ax2.set_ylim(bottom=0)

    plt.tight_layout()
    plt.savefig(OUTPUT_PNG, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"Saved: {OUTPUT_PNG}")


if __name__ == "__main__":
    main()
