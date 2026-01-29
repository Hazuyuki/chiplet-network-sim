import matplotlib.pyplot as plt

# 数据
rtt_vals = [0, 2, 10, 20]
max_recv_credit = [1.51132, 1.49917, 1.54875, 3.19146]

# buffer 模式作为参考线
buffer_max = 1.47056

plt.figure(figsize=(6,4))

# credit 曲线
plt.plot(rtt_vals, max_recv_credit, marker='o', label='credit 流控')

# buffer 基线（横线）
plt.axhline(buffer_max, color='gray', linestyle='--', label=f'buffer 模式 ({buffer_max:.3f})')

plt.xlabel('credit RTT (cycles)')
plt.ylabel('最大接收率 (flits/(node·cycle))')
plt.title('buffer 模式 vs 不同 RTT 的 credit 模式（72 GPU NVSwitch）')
plt.grid(True, linestyle='--', alpha=0.3)
plt.legend()
plt.tight_layout()
plt.savefig('rtt_comparison.png', dpi=300, bbox_inches='tight')
plt.show()