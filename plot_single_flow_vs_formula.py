#!/usr/bin/env python3
"""
单发单收实验：Throughput vs R，与公式对比
包含 Direct routing (单链路) 和包泼洒 (18 链路并行) 两种情况
"""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "output")

# === Direct routing (单链路) ===
buffer_size = 8
vc_number = 2
B_direct = 1
C_eff_direct = buffer_size * vc_number
T_other_single = 9   # 单流拟合
T_other_multi = 4    # 多流
R_crit_single = C_eff_direct / B_direct - T_other_single  # = 7
R_crit_multi = C_eff_direct / B_direct - T_other_multi    # = 12

# === 包泼洒 (18 链路并行，优先级策略) ===
num_links = 18
B_spray = num_links  # 18 flits/cycle
C_eff_spray = buffer_size * vc_number * num_links  # 8 * 2 * 18 = 288
# 由实测转折点反推：R=6~8 时吞吐开始下降，取 R_crit≈7 => T_other = C_eff/B - R_crit = 16-7 = 9
T_other_spray = 9
R_crit_spray = C_eff_spray / B_spray - T_other_spray  # = 7，与实测一致


def load_csv_throughput(path):
    if not os.path.isfile(path):
        return None, None
    inj, thr = [], []
    with open(path) as f:
        for line in f:
            parts = line.strip().split(",")
            if len(parts) < 3:
                continue
            try:
                inj.append(float(parts[0]))
                thr.append(float(parts[2]))
            except ValueError:
                continue
    return np.array(inj), np.array(thr) if inj else (None, None)


def throughput_formula(R, C_eff, T_other, B):
    return np.minimum(B, C_eff / (T_other + np.asarray(R)))


def main():
    R = np.linspace(0, 28, 200)
    
    # Direct routing 公式
    thr_formula_direct = throughput_formula(R, C_eff_direct, T_other_single, B_direct)
    
    # 包泼洒公式
    thr_formula_spray = throughput_formula(R, C_eff_spray, T_other_spray, B_spray)

    fig, ax = plt.subplots(figsize=(12, 7))
    
    # Direct routing: 公式和实验
    ax.plot(R, thr_formula_direct, "b-", linewidth=2, label=f"Direct Formula (B=1, C_eff=16, T_other={T_other_single})")
    ax.axvline(R_crit_single, color="blue", linestyle=":", linewidth=1, alpha=0.6)
    
    # 包泼洒: 公式和实验
    ax.plot(R, thr_formula_spray, "r-", linewidth=2, label=f"Spray Formula (B=18, C_eff=288, T_other={T_other_spray})")
    ax.axvline(R_crit_spray, color="red", linestyle=":", linewidth=1, alpha=0.6)
    
    DELAYS = [0, 2, 4, 6, 8, 12, 16, 20, 24]
    
    # 读取包泼洒实验数据
    thr_sim_spray = []
    for d in DELAYS:
        path = os.path.join(OUTPUT_DIR, f"single_flow_delay_{d}.csv")
        inj, thr = load_csv_throughput(path)
        if inj is None or len(inj) == 0:
            thr_sim_spray.append(np.nan)
            continue
        thr_hi = np.max(thr)
        thr_sim_spray.append(thr_hi)
        ax.scatter([d], [thr_hi], color="red", s=80, zorder=5, marker='o', edgecolors="darkred", linewidths=1.5)
    
    # Direct routing 对比数据（之前的结果）
    direct_data = {0: 1.0, 2: 1.0, 4: 1.0, 6: 1.0, 8: 0.94, 12: 0.75, 16: 0.66, 20: 0.58, 24: 0.53}
    for d in DELAYS:
        if d in direct_data:
            ax.scatter([d], [direct_data[d]], color="blue", s=80, zorder=5, marker='s', edgecolors="darkblue", linewidths=1.5)

    # 图例和标注
    ax.scatter([], [], color="red", s=80, marker='o', edgecolors="darkred", linewidths=1.5, label="Packet Spraying (18 links, min+RR)")
    ax.scatter([], [], color="blue", s=80, marker='s', edgecolors="darkblue", linewidths=1.5, label="Direct Routing (1 link)")
    
    formula_text = (
        r"Throughput$(R) = \min\left(B,\; \frac{C_{eff}}{T_{other}+R}\right)$"
        + f"\n\nDirect: $B=1$, $C_{{eff}}=16$, $T_{{other}}={T_other_single}$ → $R_{{crit}}≈{R_crit_single:.0f}$"
        + f"\nSpray:  $B=18$, $C_{{eff}}=288$, $T_{{other}}={T_other_spray}$ → $R_{{crit}}≈{R_crit_spray:.0f}$"
        + f"\n\nSpray improvement: ~18x at low RTT (1 -> 18)"
        + "\n(Sim < formula when R large: per-link credits + staggered return)"
    )
    ax.text(0.03, 0.97, formula_text, transform=ax.transAxes, fontsize=10,
            verticalalignment="top", horizontalalignment="left",
            bbox=dict(boxstyle="round,pad=0.5", facecolor="wheat", alpha=0.9))

    ax.legend(loc="upper right", fontsize=10)
    ax.set_xlabel("Credit return delay R (cycles)", fontsize=12)
    ax.set_ylabel("Throughput (flits/cycle)", fontsize=12)
    ax.set_title("Single Flow: Packet Spraying vs Direct Routing — RTT Impact", fontsize=13, fontweight='bold')
    ax.set_xlim(0, 28)
    ax.set_ylim(0, 19)
    ax.grid(True, linestyle="--", alpha=0.3)
    plt.tight_layout()
    out = os.path.join(SCRIPT_DIR, "single_flow_vs_formula.png")
    plt.savefig(out, dpi=200, bbox_inches="tight")
    print(f"Saved: {out}")

    print("\n=== Throughput Comparison: Packet Spraying vs Direct ===")
    print("R\tSpray(Sim)\tSpray(Formula)\tDirect(Sim)\tDirect(Formula)")
    for i, d in enumerate(DELAYS):
        spray_sim = thr_sim_spray[i] if not np.isnan(thr_sim_spray[i]) else float("nan")
        spray_form = float(throughput_formula(d, C_eff_spray, T_other_spray, B_spray))
        direct_sim = direct_data.get(d, float("nan"))
        direct_form = float(throughput_formula(d, C_eff_direct, T_other_single, B_direct))
        
        if not np.isnan(spray_sim):
            print(f"{d}\t{spray_sim:.2f}\t\t{spray_form:.2f}\t\t{direct_sim:.2f}\t\t{direct_form:.2f}")
        else:
            print(f"{d}\t--\t\t{spray_form:.2f}\t\t{direct_sim:.2f}\t\t{direct_form:.2f}")


if __name__ == "__main__":
    main()
