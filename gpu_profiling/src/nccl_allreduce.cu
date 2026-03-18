/**
 * GPU NCCL All-Reduce Profiling Code (Single Process Multi-GPU)
 * 
 * 使用单个进程控制 4 个 GPU 进行 NCCL all-reduce 通信性能测试
 * 支持使用 nsys 和 ncu 进行性能分析
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nccl.h>
#include <cuda.h>
#include <cuda_runtime.h>

#define CHECK_CUDA(call) \
    do { \
        cudaError_t err = call; \
        if (err != cudaSuccess) { \
            fprintf(stderr, "CUDA error at %s:%d: %s\n", __FILE__, __LINE__, \
                    cudaGetErrorString(err)); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

#define CHECK_NCCL(call) \
    do { \
        ncclResult_t err = call; \
        if (err != ncclSuccess) { \
            fprintf(stderr, "NCCL error at %s:%d: %s\n", __FILE__, __LINE__, \
                    ncclGetErrorString(err)); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

static void print_usage(const char* prog) {
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  -s <size>    Data size in bytes (default: 1048576 = 1MB)\n");
    printf("  -i <iter>   Number of iterations (default: 100)\n");
    printf("  -w <warmup>  Warmup iterations (default: 10)\n");
    printf("  -g <gpus>   Number of GPUs (default: 4)\n");
    printf("  -h          Show this help message\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s -s 1048576 -i 100     # 1MB data, 100 iterations\n", prog);
    printf("  %s -s 256MB -i 50       # 256MB data, 50 iterations\n", prog);
    printf("  %s -s 1GB -i 10         # 1GB data, 10 iterations\n", prog);
}

// 解析数据大小字符串 (支持 KB, MB, GB, TB)
static size_t parse_size(const char* str) {
    char* end;
    double size = strtod(str, &end);
    
    if (end == str) {
        fprintf(stderr, "Error: Invalid size '%s'\n", str);
        exit(EXIT_FAILURE);
    }
    
    if (*end == '\0') {
        return (size_t)size;
    }
    
    if (strcmp(end, "KB") == 0 || strcmp(end, "kb") == 0) {
        return (size_t)(size * 1024);
    } else if (strcmp(end, "MB") == 0 || strcmp(end, "mb") == 0) {
        return (size_t)(size * 1024 * 1024);
    } else if (strcmp(end, "GB") == 0 || strcmp(end, "gb") == 0) {
        return (size_t)(size * 1024 * 1024 * 1024);
    } else if (strcmp(end, "TB") == 0 || strcmp(end, "tb") == 0) {
        return (size_t)(size * 1024 * 1024 * 1024 * 1024);
    } else {
        fprintf(stderr, "Error: Unknown size suffix '%s'\n", end);
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char* argv[]) {
    // 默认参数
    size_t data_size = 1048576;  // 1MB
    int num_iterations = 100;
    int warmup_iterations = 10;
    int num_gpus = 4;
    
    // 解析命令行参数
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            data_size = parse_size(argv[++i]);
        } else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            num_iterations = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
            warmup_iterations = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-g") == 0 && i + 1 < argc) {
            num_gpus = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    // 计算元素数量 (假设 float 数据类型)
    size_t count = data_size / sizeof(float);
    
    printf("=== NCCL All-Reduce Profiling ===\n");
    printf("Data size: %zu bytes (%.2f MB)\n", data_size, data_size / (1024.0 * 1024.0));
    printf("Element count: %zu floats\n", count);
    printf("Number of GPUs: %d\n", num_gpus);
    printf("Iterations: %d (+ %d warmup)\n", num_iterations, warmup_iterations);
    printf("==================================\n\n");
    
    // 检查 CUDA
    int deviceCount;
    CHECK_CUDA(cudaGetDeviceCount(&deviceCount));
    printf("Found %d CUDA devices\n", deviceCount);
    
    if (deviceCount < num_gpus) {
        fprintf(stderr, "Error: Need at least %d GPUs, but only %d available\n", 
                num_gpus, deviceCount);
        return 1;
    }
    
    // 获取所有 GPU 的设备 ID
    int* gpu_ids = (int*)malloc(num_gpus * sizeof(int));
    for (int i = 0; i < num_gpus; i++) {
        gpu_ids[i] = i;
    }
    
    // 打印 GPU 信息
    for (int i = 0; i < num_gpus; i++) {
        cudaDeviceProp prop;
        CHECK_CUDA(cudaGetDeviceProperties(&prop, gpu_ids[i]));
        printf("GPU %d: %s (Compute %d.%d)\n", gpu_ids[i], prop.name,
               prop.major, prop.minor);
    }
    printf("\n");
    
    // 初始化每个 GPU
    for (int i = 0; i < num_gpus; i++) {
        CHECK_CUDA(cudaSetDevice(gpu_ids[i]));
    }
    
    // 生成 NCCL unique ID
    ncclUniqueId nccl_id;
    CHECK_NCCL(ncclGetUniqueId(&nccl_id));
    
    // 创建 NCCL communicators
    ncclComm_t* comms = (ncclComm_t*)malloc(num_gpus * sizeof(ncclComm_t));
    CHECK_NCCL(ncclCommInitAll(comms, num_gpus, gpu_ids));
    
    // 分配 GPU 内存
    float** sendbuffs = (float**)malloc(num_gpus * sizeof(float*));
    float** recvbuffs = (float**)malloc(num_gpus * sizeof(float*));
    
    for (int i = 0; i < num_gpus; i++) {
        CHECK_CUDA(cudaSetDevice(gpu_ids[i]));
        CHECK_CUDA(cudaMalloc(&sendbuffs[i], data_size));
        CHECK_CUDA(cudaMalloc(&recvbuffs[i], data_size));
        
        // 初始化数据
        CHECK_CUDA(cudaMemset(sendbuffs[i], 1, data_size));
        CHECK_CUDA(cudaMemset(recvbuffs[i], 0, data_size));
    }
    
    // 同步所有 GPU
    for (int i = 0; i < num_gpus; i++) {
        CHECK_CUDA(cudaSetDevice(gpu_ids[i]));
        CHECK_CUDA(cudaDeviceSynchronize());
    }
    
    // Warmup
    printf("Running warmup (%d iterations)...\n", warmup_iterations);
    for (int iter = 0; iter < warmup_iterations; iter++) {
        for (int rank = 0; rank < num_gpus; rank++) {
            CHECK_CUDA(cudaSetDevice(gpu_ids[rank]));
            CHECK_NCCL(ncclAllReduce(sendbuffs[rank], recvbuffs[rank], count, 
                             ncclFloat, ncclSum, comms[rank], NULL));
        }
    }
    
    // 同步所有 GPU
    for (int i = 0; i < num_gpus; i++) {
        CHECK_CUDA(cudaSetDevice(gpu_ids[i]));
        CHECK_CUDA(cudaDeviceSynchronize());
    }
    
    // 正式测试
    printf("Running profiling (%d iterations)...\n", num_iterations);
    
    // 使用 CUDA 事件进行精确计时
    cudaEvent_t* starts = (cudaEvent_t*)malloc(num_gpus * sizeof(cudaEvent_t));
    cudaEvent_t* stops = (cudaEvent_t*)malloc(num_gpus * sizeof(cudaEvent_t));
    
    for (int i = 0; i < num_gpus; i++) {
        CHECK_CUDA(cudaEventCreate(&starts[i]));
        CHECK_CUDA(cudaEventCreate(&stops[i]));
    }
    
    float total_time = 0.0f;
    
    for (int iter = 0; iter < num_iterations; iter++) {
        // 在每个 GPU 上记录开始时间并执行 all-reduce
        for (int rank = 0; rank < num_gpus; rank++) {
            CHECK_CUDA(cudaSetDevice(gpu_ids[rank]));
            CHECK_CUDA(cudaEventRecord(starts[rank], NULL));
            CHECK_NCCL(ncclAllReduce(sendbuffs[rank], recvbuffs[rank], count,
                             ncclFloat, ncclSum, comms[rank], NULL));
        }
        
        // 等待所有 GPU 完成并记录结束时间
        for (int rank = 0; rank < num_gpus; rank++) {
            CHECK_CUDA(cudaSetDevice(gpu_ids[rank]));
            CHECK_CUDA(cudaEventRecord(stops[rank], NULL));
            CHECK_CUDA(cudaEventSynchronize(stops[rank]));
            
            float elapsed;
            CHECK_CUDA(cudaEventElapsedTime(&elapsed, starts[rank], stops[rank]));
            total_time += elapsed;
        }
    }
    
    // 计算平均时间 (每个迭代在所有 GPU 上执行一次)
    float avg_time_per_iter = total_time / num_iterations;
    float avg_time_per_gpu = total_time / (num_iterations * num_gpus);
    
    // 带宽计算: all-reduce 传输 2 * data_size 字节 (每个 GPU 发送并接收)
    float bandwidth = (data_size * 2.0f) / (avg_time_per_iter / 1000.0f) / 1e9;  // GB/s
    
    printf("\n=== Results ===\n");
    printf("Total time: %.3f ms\n", total_time);
    printf("Average time per iteration: %.3f ms\n", avg_time_per_iter);
    printf("Average time per GPU: %.3f ms\n", avg_time_per_gpu);
    printf("Bandwidth: %.2f GB/s\n", bandwidth);
    printf("===============\n");
    
    // 输出 JSON 格式（便于脚本解析）
    printf("\n###RESULT###\n");
    printf("DATA_SIZE=%zu\n", data_size);
    printf("NUM_GPUS=%d\n", num_gpus);
    printf("ITERATIONS=%d\n", num_iterations);
    printf("AVG_TIME_PER_ITER_MS=%.3f\n", avg_time_per_iter);
    printf("AVG_TIME_PER_GPU_MS=%.3f\n", avg_time_per_gpu);
    printf("BANDWIDTH_GBPS=%.2f\n", bandwidth);
    printf("############\n");
    
    // 清理
    for (int i = 0; i < num_gpus; i++) {
        CHECK_CUDA(cudaEventDestroy(starts[i]));
        CHECK_CUDA(cudaEventDestroy(stops[i]));
    }
    
    for (int i = 0; i < num_gpus; i++) {
        CHECK_CUDA(cudaSetDevice(gpu_ids[i]));
        CHECK_CUDA(cudaFree(sendbuffs[i]));
        CHECK_CUDA(cudaFree(recvbuffs[i]));
        CHECK_NCCL(ncclCommDestroy(comms[i]));
    }
    
    free(gpu_ids);
    free(comms);
    free(sendbuffs);
    free(recvbuffs);
    free(starts);
    free(stops);
    
    return 0;
}
