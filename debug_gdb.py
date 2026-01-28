#!/usr/bin/env python3
"""
GDB Python脚本 - 用于调试NVSwitch拓扑卡死问题
使用方法: gdb -x debug_gdb.py ./ChipletNetworkSim
"""

import gdb
import time
import threading

class GDBInterruptHandler:
    def __init__(self, wait_time=10):
        self.wait_time = wait_time
        self.interrupted = False
        
    def interrupt_after_delay(self):
        """在指定时间后中断程序"""
        time.sleep(self.wait_time)
        if not self.interrupted:
            gdb.execute("interrupt")
            self.interrupted = True
            print("\n=== 程序已中断，显示堆栈信息 ===\n")
            gdb.execute("bt 30")
            print("\n=== 所有线程信息 ===\n")
            gdb.execute("info threads")
            print("\n=== 所有线程堆栈 ===\n")
            gdb.execute("thread apply all bt 15")
            print("\n=== 当前代码位置 ===\n")
            gdb.execute("list")
            print("\n=== 局部变量 ===\n")
            try:
                gdb.execute("info locals")
            except:
                pass

# 设置GDB
gdb.execute("set confirm off")
gdb.execute("set pagination off")
gdb.execute("handle SIGINT stop print")
gdb.execute("handle SIGTERM stop print")

# 启动中断处理线程
handler = GDBInterruptHandler(wait_time=10)
interrupt_thread = threading.Thread(target=handler.interrupt_after_delay, daemon=True)
interrupt_thread.start()

# 运行程序
print("=== 启动程序，将在10秒后自动中断 ===\n")
try:
    gdb.execute("run ../../input/nvswitch_test.ini")
except gdb.error as e:
    if "exited" in str(e) or "received signal" in str(e):
        # 程序正常退出或被中断
        if handler.interrupted:
            print("\n=== 调试完成 ===\n")
        else:
            print("\n程序已退出\n")
    else:
        print(f"GDB错误: {e}")
