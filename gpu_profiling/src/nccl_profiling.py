#!/usr/bin/env python3
"""
GPU NCCL All-Reduce Profiling using PyTorch

使用 PyTorch 调用 NCCL 进行多 GPU all-reduce 通信性能测试
支持使用 nsys 和 ncu 进行性能分析

使用方法：
    # 基本运行
    torchrun --nproc_per_node=4 nccl_profiling.py -s 256MB
    
    # 使用 nsys 进行性能分析
    # -o 指定输出文件名
    nsys profile -o my_report -f true -c cudaProfilerApi --capture-range-end stop -x true \
        torchrun --nproc_per_node=4 nccl_profiling.py -s 256MB
    
    # 使用 ncu 进行性能分析
    ncu --set full torchrun --nproc_per_node=4 nccl_profiling.py -s 256MB

重要：
    - 在 Python 代码中使用 torch.cuda.cudart().cudaProfilerStart() 和 cudaProfilerStop()
      来标记需要 profiling 的代码范围
    - 与 nsys 的 -c cudaProfilerApi 选项配合使用
"""

import argparse
import subprocess
import os
import sys
import time
from datetime import datetime


def parse_size(size_str: str) -> int:
    """解析数据大小字符串"""
    size_str = size_str.strip().upper()
    
    # 先处理长后缀
    for suffix in ['TB', 'GB', 'MB', 'KB', 'B']:
        if size_str.endswith(suffix):
            value_str = size_str[:-len(suffix)]
            try:
                return int(float(value_str) * {
                    'TB': 1024 ** 4,
                    'GB': 1024 ** 3,
                    'MB': 1024 ** 2,
                    'KB': 1024,
                    'B': 1
                }[suffix])
            except ValueError:
                pass
    
    # 如果没有后缀，直接转换为整数
    try:
        return int(size_str)
    except ValueError:
        return 0


def run_main():
    import torch
    import torch.distributed as dist
    
    # 解析参数
    parser = argparse.ArgumentParser(description='NCCL All-Reduce Profiling')
    parser.add_argument('-s', '--data-size', default='256MB', help='Data size')
    parser.add_argument('-i', '--iterations', type=int, default=100, help='Iterations')
    parser.add_argument('-w', '--warmup', type=int, default=10, help='Warmup')
    args, unknown = parser.parse_known_args()
    
    # 解析数据大小
    data_size = parse_size(args.data_size)
    num_iterations = args.iterations
    warmup = args.warmup
    
    # 获取 rank 和 world_size (torchrun 设置)
    rank = int(os.environ['RANK'])
    world_size = int(os.environ['WORLD_SIZE'])
    local_rank = int(os.environ['LOCAL_RANK'])
    
    # 初始化 NCCL 进程组
    dist.init_process_group(backend='nccl', init_method='env://')
    
    # 设置当前 GPU
    torch.cuda.set_device(local_rank)
    device = torch.cuda.current_device()
    
    # 计算元素数量
    count = data_size // 4  # float32 = 4 bytes
    
    # 创建 tensor
    send_tensor = torch.ones(count, dtype=torch.float32, device=device)
    recv_tensor = torch.zeros(count, dtype=torch.float32, device=device)
    
    # 同步
    torch.cuda.synchronize()
    
    # Warmup (不计入性能测试)
    for _ in range(warmup):
        dist.all_reduce(recv_tensor, op=dist.ReduceOp.SUM)
    torch.cuda.synchronize()
    
    # ====== nsys/ncu Profiling 范围开始 ======
    # 使用 torch.cuda.cudart().cudaProfilerStart() 开始 profiling
    # 与 nsys -c cudaProfilerApi 配合使用
    torch.cuda.cudart().cudaProfilerStart()
    
    # 正式测试
    times = []
    for _ in range(num_iterations):
        start = time.perf_counter()
        dist.all_reduce(recv_tensor, op=dist.ReduceOp.SUM)
        torch.cuda.synchronize()
        end = time.perf_counter()
        times.append((end - start) * 1000)  # ms
    
    # ====== nsys/ncu Profiling 范围结束 ======
    torch.cuda.cudart().cudaProfilerStop()
    
    # 计算平均时间
    avg_time = sum(times) / len(times)
    
    # 带宽 = 2 * data_size / time (all-reduce 发送和接收)
    bandwidth_gbps = (data_size * 2) / (avg_time / 1000) / 1e9
    
    # 仅 rank 0 打印信息
    if rank == 0:
        print("=== NCCL All-Reduce Profiling ===")
        print(f"Data size: {data_size} bytes ({data_size / (1024**2):.2f} MB)")
        print(f"Number of GPUs: {world_size}")
        print(f"Iterations: {num_iterations}")
        print("=" * 40)
        print(f"\n=== Results ===")
        print(f"Average time: {avg_time:.3f} ms")
        print(f"Bandwidth: {bandwidth_gbps:.2f} GB/s")
        print("=" * 40)
        
        # 输出解析友好的格式
        print("\n###RESULT###")
        print(f"DATA_SIZE={data_size}")
        print(f"NUM_GPUS={world_size}")
        print(f"ITERATIONS={num_iterations}")
        print(f"AVG_TIME_MS={avg_time:.3f}")
        print(f"BANDWIDTH_GBPS={bandwidth_gbps:.2f}")
        print("############")
    
    dist.destroy_process_group()


def main():
    # 检查是否是主进程
    if 'RANK' in os.environ and 'WORLD_SIZE' in os.environ:
        run_main()
    else:
        # 显示使用说明
        print("NCCL All-Reduce Profiling")
        print("=" * 50)
        print("\n使用方法:")
        print("\n1. 基本运行:")
        print("   torchrun --nproc_per_node=4 nccl_profiling.py -s 256MB -i 100")
        
        print("\n2. 使用 nsys 进行性能分析:")
        print("   nsys profile \\")
        print("       -f true \\")
        print("       -c cudaProfilerApi \\")
        print("       --capture-range-end stop \\")
        print("       -x true \\")
        print("       torchrun --nproc_per_node=4 nccl_profiling.py -s 256MB -i 100")
        
        print("\n3. 使用 ncu 进行性能分析:")
        print("   ncu --set full \\")
        print("       torchrun --nproc_per_node=4 nccl_profiling.py -s 256MB -i 100")
        
        print("\n选项:")
        print("  -s, --data-size: 数据大小 (例如: 256MB, 1GB)")
        print("  -i, --iterations: 迭代次数 (默认: 100)")
        print("  -w, --warmup: 预热迭代次数 (默认: 10)")
        
        print("\n示例:")
        print("  torchrun --nproc_per_node=4 nccl_profiling.py -s 256MB -i 50 -w 10")
        print("  torchrun --nproc_per_node=8 nccl_profiling.py -s 1GB -i 20")


if __name__ == '__main__':
    main()
