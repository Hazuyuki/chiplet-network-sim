#!/usr/bin/env python3
"""
单发单收 RTT vs Throughput 对比图
包泼洒 (min routing + 轮询) vs 之前的 direct routing
"""
import os

# 包泼洒数据（刚跑的）
data_spraying = {
    0: 12.82, 2: 12.29, 4: 12.30, 6: 12.76, 8: 12.62,
    12: 11.48, 16: 9.77, 20: 7.93, 24: 6.66
}

# 之前 direct routing 的数据（单链路）
data_direct = {
    0: 1.0, 2: 1.0, 4: 1.0, 6: 1.0, 8: 0.94,
    12: 0.75, 16: 0.66, 20: 0.58, 24: 0.53
}

# 理论公式（包泼洒有 18 条路径，C_eff = 18*8*2 = 288）
def formula_spraying(R, T_other=6, C_eff=288, B=18):
    return min(B, C_eff / (T_other + R))

def formula_direct(R, T_other=9, C_eff=16, B=1):
    return min(B, C_eff / (T_other + R))

print("=" * 60)
print("单 GPU 到单 GPU 传输：RTT 对吞吐的影响")
print("=" * 60)
print()
print("配置: 2 GPU, 1 NVSwitch, gpu_nvlink_ports=18, buffer_size=8, vc=2")
print()
print("| RTT | 包泼洒实测 | 包泼洒理论 | Direct实测 | Direct理论 |")
print("|-----|-----------|-----------|-----------|-----------|")

for R in [0, 2, 4, 6, 8, 12, 16, 20, 24]:
    spr_sim = data_spraying.get(R, 0)
    spr_th = formula_spraying(R)
    dir_sim = data_direct.get(R, 0)
    dir_th = formula_direct(R)
    print(f"| {R:3d} | {spr_sim:9.2f} | {spr_th:9.2f} | {dir_sim:9.2f} | {dir_th:9.2f} |")

print()
print("=" * 60)
print("包泼洒提升倍数 = 包泼洒吞吐 / Direct 吞吐")
print("=" * 60)
print()
print("| RTT | 提升倍数 |")
print("|-----|---------|")
for R in [0, 2, 4, 6, 8, 12, 16, 20, 24]:
    spr = data_spraying.get(R, 0)
    dir_ = data_direct.get(R, 0.001)
    ratio = spr / dir_
    print(f"| {R:3d} | {ratio:7.1f}x |")

print()
print("=" * 60)
print("关键结论:")
print("=" * 60)
print("1. 包泼洒将单流吞吐从 ~1 提升到 ~12-13 (约 12x)")
print("2. RTT 敏感度降低: RTT=24 时仍有 ~6.7 的吞吐")
print("3. Direct routing 在 RTT=24 时吞吐仅 0.53")
print("4. 包泼洒使得 R_crit 从 ~8 推迟到 ~12")
