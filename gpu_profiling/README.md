# GPU NCCL All-Reduce Profiling

本目录用于存放 GPU kernel 调用代码和 profiling 结果，用于与 CNSim 仿真结果进行对比。

## 目录结构

```
gpu_profiling/
├── src/
│   ├── nccl_profiling.py      # Python 版本 (推荐)
│   └── nccl_allreduce.cu      # CUDA C++ 版本
├── scripts/
│   ├── compile.sh              # 编译 C++ 版本
│   └── run_profiling.sh        # 运行脚本
├── results/                    # Profiling 结果目录
└── README.md                   # 本文档
```

## 快速开始（推荐使用 Python 版本）

### 1. 确保 PyTorch 已安装

```bash
pip install torch
```

### 2. 运行基本测试

```bash
cd gpu_profiling/scripts
./run_profiling.sh -s 256MB -i 100
```

### 3. 运行 nsys profiling

```bash
./run_profiling.sh -s 256MB -i 100 --nsys
```

### 4. 运行 ncu profiling

```bash
./run_profiling.sh -s 256MB -i 100 --ncu
```

## 直接使用 Python

```bash
# 基本测试
python3 src/nccl_profiling.py -s 256MB -i 100

# 带 nsys
python3 src/nccl_profiling.py -s 256MB -i 100 --nsys

# 带 ncu
python3 src/nccl_profiling.py -s 256MB -i 100 --ncu
```

## 运行参数

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `-s, --data-size` | 数据大小 | 256MB |
| `-i, --iterations` | 迭代次数 | 100 |
| `-w, --warmup` | 预热次数 | 10 |
| `-g, --num-gpus` | GPU 数量 | 4 |
| `--nsys` | 运行 nsys profiling | false |
| `--ncu` | 运行 ncu profiling | false |
| `-o, --output` | 输出目录 | results/ |

## 数据大小格式

支持以下格式：
- `1KB`, `1MB`, `1GB`, `1TB`
- 或者直接使用字节数

示例：
- `1MB` = 1048576 bytes
- `256MB` = 268435456 bytes
- `1GB` = 1073741824 bytes

## 输出结果

运行后会生成以下文件：

```
results/
├── nccl_profiling.py       # 测试脚本
├── nccl_nsys.qdrep       # nsys 输出 (使用 nsys 查看)
├── nsys_report.txt       # nsys 统计报告
├── ncu_report.txt         # ncu 报告
└── ncu_logs/            # ncu 日志目录
```

## 使用 nsys 查看结果

```bash
# 查看 nsys 输出
nsys view <output_file>.qdrep

# 生成 CUDA API 报告
nsys stats --report cudaapisum <output_file>.qdrep

# 生成 GPU kernel 报告
nsys stats --report gputopsum <output_file>.qdrep
```

## 使用 ncu 查看结果

```bash
# 查看 ncu 输出
ncu --report <output_file>

# 指定指标集
ncu --set full <executable>
```

## 与 CNSim 对比

1. 运行 GPU profiling 获取实际通信时间和带宽
2. 使用 CNSim 仿真获取饱和点
3. 对比两者差异，调整 CNSim 参数

详见 `../../docs/GPU对比脚本使用说明.md`

## CUDA C++ 版本

如果需要使用 CUDA C++ 版本：

```bash
cd gpu_profiling/scripts
./compile.sh
```

然后修改 `run_profiling.sh` 中的 `PYTHON_SCRIPT` 指向编译后的可执行文件。

## 注意事项

1. 需要 4 个空闲 GPU 才能运行
2. NCCL 需要正确配置（GPU 之间的 NVLink 连接）
3. nsys 和 ncu 需要单独安装
4. PyTorch 需要支持 NCCL（使用 `pip install torch` 安装的版本通常支持）
