#!/bin/bash
# Run plot script with venv: /share/zhuyu-nfs/llm
cd "$(dirname "$0")"
/share/zhuyu-nfs/llm/bin/python plot_latency_vs_injection.py
