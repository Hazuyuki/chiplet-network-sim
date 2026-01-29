#!/usr/bin/env python3
"""
Credit 流控 RTT 分析图：
1) 理论最大吞吐 r_max = C / (T_other + R) vs R
2) 临界 R_crit = C/B - T_other 标出
3) 可选：叠加仿真得到的 throughput 点
"""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "output")

# 与实验配置一致
buffer_size = 8   # 每 VC 的 buffer 深度 (flits)
vc_number = 2     # 每端口 VC 数
B = 1             # 带宽 (flits/cycle)
# 有效 credit 容量：多 VC 并行，同一端口可“在途”的 flit 数 = 各 VC 之和
C_eff = buffer_size * vc_number  # = 16
# 其他延迟：链路 + 接收端停留；取 ~4 使 R_crit 与仿真“约从 12 开始掉”一致
T_other = 4

R_crit = C_eff / B - T_other  # 临界 RTT：R > R_crit 时 throughput 受 RTT 限制


def load_csv_throughput(path):
    """Load (injection, throughput); return max throughput at high injection."""
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
    """Estimated saturation throughput: min(B, C_eff/(T_other+R))."""
    return np.minimum(B, C_eff / (T_other + np.asarray(R)))


def main():
    # 公式：饱和吞吐估计 Throughput(R) = min(B, C_eff/(T_other + R))
    R = np.linspace(0, 28, 200)
    thr_formula = throughput_formula(R, C_eff, T_other, B)

    fig, ax = plt.subplots(figsize=(10, 6))
    ax.plot(R, thr_formula, "b-", linewidth=2.5, label="Formula (estimated)")
    ax.axvline(R_crit, color="red", linestyle="--", linewidth=1.2, alpha=0.9)
    ax.axhline(B, color="gray", linestyle=":", alpha=0.6)

    # 仿真点：各 delay 下的最大 throughput（饱和区），与公式“饱和吞吐”可比
    DELAYS = [0, 2, 4, 6, 8, 12, 16, 20, 24]
    thr_sim = []
    for d in DELAYS:
        path = os.path.join(OUTPUT_DIR, f"credit_delay_{d}.csv")
        inj, thr = load_csv_throughput(path)
        if inj is None or len(inj) == 0:
            thr_sim.append(np.nan)
            continue
        thr_hi = np.max(thr)  # 饱和吞吐：该 delay 下能达到的最大接收率
        thr_sim.append(thr_hi)
        ax.scatter([d], [thr_hi], color="red", s=55, zorder=5, edgecolors="darkred", linewidths=1.2)
        ax.annotate(f"{d}", (d, thr_hi), textcoords="offset points", xytext=(0, 8), fontsize=8, ha="center")

    # 公式框（注明：公式为单链路估计，仿真为全网平均，故仿真常高于公式）
    formula_text = (
        r"Throughput$(R) = \min\left(B,\; \frac{C_{eff}}{T_{other}+R}\right)$"
        + f"\n$C_{{eff}}={C_eff}$, $T_{{other}}={T_other}$, $B={B}$"
        + f"\n$R_{{crit}} = C_{{eff}}/B - T_{{other}} = {R_crit:.0f}$"
        + "\n(公式: 单链路; 仿真: 全网平均, 故仿真偏高)"
    )
    ax.text(0.97, 0.97, formula_text, transform=ax.transAxes, fontsize=10,
            verticalalignment="top", horizontalalignment="right",
            bbox=dict(boxstyle="round,pad=0.4", facecolor="wheat", alpha=0.9))

    # 图例：公式曲线 + 仿真点
    ax.scatter([], [], color="red", s=55, edgecolors="darkred", linewidths=1.2, label="Simulation (max throughput)")
    ax.legend(loc="upper right", fontsize=9)

    ax.set_xlabel("Credit return delay R (cycles)", fontsize=11)
    ax.set_ylabel("Throughput (flits/(node·cycle))", fontsize=11)
    ax.set_title("Throughput vs R: Formula vs Simulation")
    ax.set_xlim(0, 28)
    ax.set_ylim(0, 1.1)
    ax.grid(True, linestyle="--", alpha=0.3)
    plt.tight_layout()
    out = os.path.join(SCRIPT_DIR, "credit_RTT_analysis.png")
    plt.savefig(out, dpi=200, bbox_inches="tight")
    print(f"Saved: {out}")

    # 打印公式与仿真对比
    print("Formula: Throughput(R) = min(B, C_eff/(T_other+R))")
    print("R_crit =", R_crit)
    print("R\tFormula\tSimulation")
    for i, d in enumerate(DELAYS):
        f_val = float(throughput_formula(d, C_eff, T_other, B))
        s_val = thr_sim[i] if not np.isnan(thr_sim[i]) else float("nan")
        print(f"{d}\t{f_val:.4f}\t{s_val:.4f}" if not np.isnan(s_val) else f"{d}\t{f_val:.4f}\t--")


if __name__ == "__main__":
    main()
