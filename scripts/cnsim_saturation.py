#!/usr/bin/env python3
"""
CNSim 饱和点扫描脚本

利用 CNSim 自身的 injection rate 扫描功能，找到饱和点后计算带宽和延迟。

假设：
- packet_length 固定（由配置文件决定）
- packet 数量根据数据量计算

用法：
    python cnsim_saturation.py <config.ini> [options]

示例：
    # 解析已有的仿真输出
    python cnsim_saturation.py input/nvswitch_allreduce.ini -d 256MB -g 1500 -n 8
    
    # 运行仿真并解析
    python cnsim_saturation.py input/nvswitch_allreduce.ini -d 256MB -g 1500 -n 8 --run
"""

import argparse
import subprocess
import re
import os
import sys
from dataclasses import dataclass
from typing import Optional


@dataclass
class SaturationResult:
    """饱和点结果"""
    injection_rate: float       # flits/(node*cycle)
    latency_cycles: float      # cycles
    throughput: float          # flits/(node*cycle)
    bandwidth_gbps: float      # GB/s
    latency_ns: float          # ns
    latency_us: float         # us
    data_size_bytes: int       # 数据量
    packet_length: int         # packet_length from config
    packet_count: int          # 计算的 packet 数量


def parse_data_size(size_str: str) -> int:
    """解析数据大小字符串为 bytes"""
    size_str = size_str.strip().upper()
    multipliers = {'TB': 1024**4, 'GB': 1024**3, 'MB': 1024**2, 'KB': 1024, 'B': 1}
    for suffix, mult in multipliers.items():
        if size_str.endswith(suffix):
            value_str = size_str[:-len(suffix)]
            try:
                return int(float(value_str) * mult)
            except ValueError:
                return int(size_str)
    try:
        return int(size_str)
    except ValueError:
        return 0


def parse_config_packet_length(config_path: str) -> int:
    """从配置文件解析 packet_length"""
    if not os.path.exists(config_path):
        return 1
    
    with open(config_path, 'r') as f:
        content = f.read()
    
    # 查找 packet_length
    for line in content.split('\n'):
        if line.startswith('packet_length'):
            try:
                return int(line.split('=')[1].strip())
            except:
                pass
    return 1


def run_cnsim(binary: str, config: str) -> tuple[str, str, int]:
    """运行 CNSim 仿真"""
    result = subprocess.run(
        [binary, config],
        capture_output=True,
        text=True,
        timeout=600
    )
    return result.stdout, result.stderr, result.returncode


def find_output_file(config_path: str, binary_path: str) -> str:
    """查找 CNSim 输出的 CSV 文件"""
    output_file = "output.csv"
    
    if os.path.exists(config_path):
        with open(config_path, 'r') as f:
            content = f.read()
        for line in content.split('\n'):
            if line.startswith('output_file'):
                output_file = line.split('=')[1].strip()
                break
    
    build_dir = os.path.dirname(binary_path)
    output_path = os.path.join(build_dir, output_file)
    if os.path.exists(output_path):
        return output_path
    
    proj_dir = os.path.dirname(build_dir)
    output_path = os.path.join(proj_dir, output_file)
    if os.path.exists(output_path):
        return output_path
    
    return output_path


def parse_csv_output(csv_path: str) -> Optional[SaturationResult]:
    """解析 CNSim CSV 输出文件，找到饱和点"""
    if not os.path.exists(csv_path):
        return None
    
    with open(csv_path, 'r') as f:
        lines = f.readlines()
    
    data_points = []
    for line in lines:
        line = line.strip()
        if not line:
            continue
        parts = line.split(',')
        if len(parts) >= 3:
            try:
                rate = float(parts[0])
                latency = float(parts[1])
                throughput = float(parts[2])
                data_points.append((rate, latency, throughput))
            except ValueError:
                continue
    
    if not data_points:
        return None
    
    # 找到饱和点：延迟急剧增长或吞吐率开始下降
    saturation_idx = len(data_points) - 1
    
    for i in range(1, len(data_points)):
        prev_rate, prev_lat, prev_throughput = data_points[i-1]
        curr_rate, curr_lat, curr_throughput = data_points[i]
        
        # 延迟增长超过 1.5 倍
        if prev_lat > 0 and curr_lat / prev_lat > 1.5:
            saturation_idx = i - 1
            break
        
        # 吞吐率下降
        if curr_throughput < prev_throughput * 0.95:
            saturation_idx = i - 1
            break
    
    rate, latency, throughput = data_points[saturation_idx]
    
    return SaturationResult(
        injection_rate=rate,
        latency_cycles=latency,
        throughput=throughput,
        bandwidth_gbps=0,
        latency_ns=0,
        latency_us=0,
        data_size_bytes=0,
        packet_length=1,
        packet_count=0
    )


def calculate_packet_params(data_size_bytes: int, packet_length: int) -> dict:
    """
    根据数据量计算 packet 数量
    
    假设：
    - packet_length 固定（flits 数）
    - flit = 16 bytes
    - packet_count = data_size / (packet_length × 16)
    """
    FLIT_SIZE = 16
    
    total_flits = data_size_bytes // FLIT_SIZE
    packet_count = total_flits // packet_length
    
    return {
        'total_flits': total_flits,
        'packet_count': packet_count,
        'packet_length': packet_length,
        'flit_size': FLIT_SIZE
    }


def calculate_metrics(result: SaturationResult, 
                     data_size_bytes: int,
                     gpu_clock_mhz: int,
                     num_gpus: int,
                     packet_length: int) -> SaturationResult:
    """计算带宽和延迟"""
    
    FLIT_SIZE = 16
    
    # 带宽 = throughput * num_gpus * flit_size * clock
    clock_hz = gpu_clock_mhz * 1e6
    bandwidth = (result.throughput * num_gpus * FLIT_SIZE * clock_hz) / 1e9
    
    # 延迟 = latency_cycles / clock
    latency_us = (result.latency_cycles / clock_hz) * 1e6
    latency_ns = latency_us * 1e3
    
    # 计算 packet 数量
    pkt_params = calculate_packet_params(data_size_bytes, packet_length)
    
    result.bandwidth_gbps = bandwidth
    result.latency_us = latency_us
    result.latency_ns = latency_ns
    result.data_size_bytes = data_size_bytes
    result.packet_length = packet_length
    result.packet_count = pkt_params['packet_count']
    
    return result


def print_result(result: SaturationResult, config: str, gpu_params: dict):
    """打印结果"""
    print("\n" + "="*60)
    print("CNSim 饱和点扫描结果")
    print("="*60)
    print(f"配置文件: {config}")
    print(f"GPU 数量: {gpu_params['num_gpus']}")
    print(f"GPU 时钟: {gpu_params['gpu_clock_mhz']} MHz")
    print(f"数据大小: {gpu_params['data_size_mb']:.2f} MB ({result.data_size_bytes} bytes)")
    print("-"*60)
    print(f"Packet Length: {result.packet_length} flits/packet")
    print(f"Packet Count:  {result.packet_count} packets")
    print(f"Flit Size:    16 bytes/flit")
    print("-"*60)
    print(f"Injection Rate: {result.injection_rate:.4f} flits/(node*cycle)")
    print(f"Throughput:     {result.throughput:.4f} flits/(node*cycle)")
    print(f"Latency:        {result.latency_cycles:.2f} cycles")
    print("-"*60)
    print(f">>> 带宽: {result.bandwidth_gbps:.2f} GB/s")
    print(f">>> 延迟: {result.latency_ns:.2f} ns ({result.latency_us:.4f} us)")
    print("="*60)
    
    # 额外信息：完整传输时间估算
    # 正确公式：时间 = 数据量 / 带宽
    data_time_us = (result.data_size_bytes * 1e-6) / result.bandwidth_gbps  # MB / (GB/s) = us
    print(f"\n[参考] 完整传输时间估算: {data_time_us:.2f} us ({data_time_us/1000:.4f} ms)")
    print(f"       (数据量 / 带宽 = {gpu_params['data_size_mb']:.2f} MB / {result.bandwidth_gbps:.2f} GB/s)")


def main():
    parser = argparse.ArgumentParser(description='CNSim 饱和点扫描')
    parser.add_argument('config', help='CNSim 配置文件')
    parser.add_argument('-d', '--data-size', default='256MB', 
                       help='GPU 通信数据大小 (e.g., 256MB, 1GB)')
    parser.add_argument('-g', '--gpu-clock', type=int, default=1500,
                       help='GPU 时钟频率 (MHz)')
    parser.add_argument('-n', '--num-gpus', type=int, default=8,
                       help='GPU 数量')
    parser.add_argument('-b', '--binary', default='./ChipletNetworkSim',
                       help='CNSim 可执行文件路径')
    parser.add_argument('--output', '-o', help='输出 CSV 文件路径')
    parser.add_argument('--run', action='store_true', default=False,
                       help='运行 CNSim 仿真 (默认只解析已有输出)')
    
    args = parser.parse_args()
    
    # 解析参数
    data_size_bytes = parse_data_size(args.data_size)
    data_size_mb = data_size_bytes / (1024**2)
    
    # 找配置文件路径
    script_dir = os.path.dirname(os.path.abspath(__file__))
    proj_dir = os.path.dirname(script_dir)
    config_path = args.config
    if not os.path.isabs(config_path):
        config_path = os.path.join(proj_dir, args.config)
    
    binary_path = args.binary
    if not os.path.isabs(binary_path):
        binary_path = os.path.join(proj_dir, 'build', 'ChipletNetworkSim')
    
    # 从配置文件获取 packet_length
    packet_length = parse_config_packet_length(config_path)
    
    gpu_params = {
        'num_gpus': args.num_gpus,
        'gpu_clock_mhz': args.gpu_clock,
        'data_size_mb': data_size_mb
    }
    
    print(f"配置文件: {config_path}")
    print(f"可执行文件: {binary_path}")
    print(f"数据大小: {data_size_mb:.2f} MB ({data_size_bytes} bytes)")
    print(f"GPU: {args.num_gpus} x {args.gpu_clock} MHz")
    print(f"Packet Length (from config): {packet_length} flits")
    
    # 计算 packet 数量
    pkt_params = calculate_packet_params(data_size_bytes, packet_length)
    print(f"Packet Count: {pkt_params['packet_count']} packets")
    
    # 运行仿真 (如果指定 --run)
    if args.run:
        print("\n运行仿真中...")
        stdout, stderr, returncode = run_cnsim(binary_path, config_path)
        
        if returncode != 0:
            print(f"Warning: 仿真返回非零退出码: {returncode}")
            if stderr:
                print(f"stderr: {stderr[:500]}")
    
    # 查找输出文件
    csv_path = args.output if args.output else find_output_file(config_path, binary_path)
    print(f"输出文件: {csv_path}")
    
    # 解析输出
    result = parse_csv_output(csv_path)
    
    if result is None:
        print(f"Error: 无法解析输出文件: {csv_path}")
        sys.exit(1)
    
    # 计算带宽和延迟
    result = calculate_metrics(result, data_size_bytes, args.gpu_clock, 
                              args.num_gpus, packet_length)
    
    # 打印结果
    print_result(result, args.config, gpu_params)
    
    # 输出便于脚本解析的格式
    print(f"\n###RESULT###")
    print(f"BANDWIDTH_GBPS={result.bandwidth_gbps:.2f}")
    print(f"LATENCY_NS={result.latency_ns:.2f}")
    print(f"INJECTION_RATE={result.injection_rate:.4f}")
    print(f"THROUGHPUT={result.throughput:.4f}")
    print(f"PACKET_LENGTH={result.packet_length}")
    print(f"PACKET_COUNT={result.packet_count}")
    print(f"############")


if __name__ == '__main__':
    main()
