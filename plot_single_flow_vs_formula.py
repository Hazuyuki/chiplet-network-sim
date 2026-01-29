#!/usr/bin/env python3
"""
单发单收实验：Throughput vs R，与单链路公式 min(B, C_eff/(T_other+R)) 对比。
单流 (node0 -> node1) 应接近公式曲线。
"""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "output")

# 与实验配置一致
buffer_size = 8
vc_number = 2
B = 1
C_eff = buffer_size * vc_number
# 单发单收下有效 T_other 更大（排队等），拟合得 ~9；T_other=4 仅适用于多流
T_other_single = 9   # 单流拟合，使公式与单流仿真一致
T_other_multi = 4    # 多流时用的值
R_crit_single = C_eff / B - T_other_single  # = 7
R_crit_multi = C_eff / B - T_other_multi    # = 12


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
    thr_formula_single = throughput_formula(R, C_eff, T_other_single, B)
    thr_formula_multi = throughput_formula(R, C_eff, T_other_multi, B)

    fig, ax = plt.subplots(figsize=(10, 6))
    ax.plot(R, thr_formula_single, "b-", linewidth=2.5, label=f"Formula T_other={T_other_single} (single-flow fit)")
    ax.plot(R, thr_formula_multi, "b--", linewidth=1.2, alpha=0.7, label=f"Formula T_other={T_other_multi} (multi-flow)")
    ax.axvline(R_crit_single, color="blue", linestyle=":", linewidth=1, alpha=0.8, label=f"R_crit={R_crit_single:.0f} (T_other=9)")
    ax.axvline(R_crit_multi, color="gray", linestyle=":", linewidth=1, alpha=0.6, label=f"R_crit={R_crit_multi:.0f} (T_other=4)")
    ax.axhline(B, color="gray", linestyle=":", alpha=0.6)

    DELAYS = [0, 2, 4, 6, 8, 12, 16, 20, 24]
    thr_sim = []
    for d in DELAYS:
        path = os.path.join(OUTPUT_DIR, f"single_flow_delay_{d}.csv")
        inj, thr = load_csv_throughput(path)
        if inj is None or len(inj) == 0:
            thr_sim.append(np.nan)
            continue
        thr_hi = np.max(thr)
        thr_sim.append(thr_hi)
        ax.scatter([d], [thr_hi], color="green", s=60, zorder=5, edgecolors="darkgreen", linewidths=1.2)
        ax.annotate(f"{d}", (d, thr_hi), textcoords="offset points", xytext=(0, 8), fontsize=8, ha="center")

    formula_text = (
        r"Throughput$(R) = \min\left(B,\; \frac{C_{eff}}{T_{other}+R}\right)$"
        + f"\n$C_{{eff}}={C_eff}$, $B={B}$"
        + f"\n单流拟合 $T_{{other}}={T_other_single}$ → $R_{{crit}}={R_crit_single:.0f}$ (R=12 已掉)"
        + f"\n多流用 $T_{{other}}={T_other_multi}$ → $R_{{crit}}={R_crit_multi:.0f}$"
    )
    ax.text(0.97, 0.97, formula_text, transform=ax.transAxes, fontsize=10,
            verticalalignment="top", horizontalalignment="right",
            bbox=dict(boxstyle="round,pad=0.4", facecolor="wheat", alpha=0.9))

    ax.scatter([], [], color="green", s=60, edgecolors="darkgreen", linewidths=1.2, label="Single flow (max throughput)")
    ax.legend(loc="upper right", fontsize=8)

    ax.set_xlabel("Credit return delay R (cycles)", fontsize=11)
    ax.set_ylabel("Throughput (flits/(node·cycle))", fontsize=11)
    ax.set_title("Single Flow (node0→node1): Throughput vs R — Formula vs Simulation")
    ax.set_xlim(0, 28)
    ax.set_ylim(0, 1.1)
    ax.grid(True, linestyle="--", alpha=0.3)
    plt.tight_layout()
    out = os.path.join(SCRIPT_DIR, "single_flow_vs_formula.png")
    plt.savefig(out, dpi=200, bbox_inches="tight")
    print(f"Saved: {out}")

    print("Single flow: Throughput(R) = min(B, C_eff/(T_other+R)); T_other=9 (single-flow fit)")
    print("R_crit (T_other=9) =", R_crit_single, "; R_crit (T_other=4) =", R_crit_multi)
    print("R\tFormula(T=9)\tSingleFlow")
    for i, d in enumerate(DELAYS):
        f_val = float(throughput_formula(d, C_eff, T_other_single, B))
        s_val = thr_sim[i] if not np.isnan(thr_sim[i]) else float("nan")
        print(f"{d}\t{f_val:.4f}\t{s_val:.4f}" if not np.isnan(s_val) else f"{d}\t{f_val:.4f}\t--")


if __name__ == "__main__":
    main()
