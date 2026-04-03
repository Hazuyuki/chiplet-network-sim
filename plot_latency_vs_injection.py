#!/usr/bin/env python3
"""Plot average latency vs injection rate for buffer and credit (various RTT)."""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

# Use script dir so it works from any cwd
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "output")

CONFIGS = [
    ("Buffer", "nvswitch_72gpu_buffer.csv"),
    ("Credit RTT=0", "nvswitch_72gpu_credit_rtt0.csv"),
    ("Credit RTT=2", "nvswitch_72gpu_credit_rtt2.csv"),
    ("Credit RTT=4", "nvswitch_72gpu_credit_rtt4.csv"),
    ("Credit RTT=6", "nvswitch_72gpu_credit_rtt6.csv"),
    ("Credit RTT=8", "nvswitch_72gpu_credit_rtt8.csv"),
    ("Credit RTT=10", "nvswitch_72gpu_credit_rtt10.csv"),
    ("Credit RTT=12", "nvswitch_72gpu_credit_rtt12.csv"),
    ("Credit RTT=14", "nvswitch_72gpu_credit_rtt14.csv"),
    ("Credit RTT=16 (long)", "nvswitch_72gpu_credit_rtt16_long.csv"),
    ("Credit RTT=18 (long)", "nvswitch_72gpu_credit_rtt18_long.csv"),
    ("Credit RTT=20 (long)", "nvswitch_72gpu_credit_rtt20_long.csv"),
]


def load_csv(path):
    inj, lat = [], []
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
            except ValueError:
                continue
    return np.array(inj), np.array(lat)


def main():
    fig, ax = plt.subplots(figsize=(9, 5))
    colors = plt.cm.tab20(np.linspace(0, 1, len(CONFIGS)))

    for i, (label, fname) in enumerate(CONFIGS):
        path = os.path.join(OUTPUT_DIR, fname)
        if not os.path.isfile(path):
            continue
        x, y = load_csv(path)
        if len(x) == 0:
            continue
        ax.plot(x, y, "-", color=colors[i], label=label, linewidth=1.5)

    ax.set_xlabel("Injection Rate (flits/(node·cycle))")
    ax.set_ylabel("Average Latency (cycles)")
    ax.set_title("Average Latency vs Injection Rate (72 GPU NVSwitch)")
    ax.legend(loc="upper left", fontsize=8, ncol=2)
    ax.grid(True, linestyle="--", alpha=0.3)
    ax.set_xlim(left=0)
    ax.set_ylim(bottom=0)
    plt.tight_layout()
    out_path = os.path.join(SCRIPT_DIR, "latency_vs_injection.png")
    plt.savefig(out_path, dpi=300, bbox_inches="tight")
    print(f"Saved: {out_path}")


if __name__ == "__main__":
    main()
