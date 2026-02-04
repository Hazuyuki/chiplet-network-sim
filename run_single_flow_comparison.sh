#!/bin/bash
# 单流对比实验：buffer_size 加大 / 路径(dest) / 端口数(routing direct vs min)
# 结果汇总到 output/single_flow_comparison_summary.txt
set -e
BIN="build/ChipletNetworkSim"
INPUT="input"
OUT="output"
SUMMARY="$OUT/single_flow_comparison_summary.txt"

mkdir -p "$OUT"
echo "Single-flow comparison experiments (max throughput flits/(node*cycle))" > "$SUMMARY"
echo "========================================" >> "$SUMMARY"

run_one() {
  local name="$1"
  local ini="$2"
  echo -n "[$name] " >> "$SUMMARY"
  local line
  line=$("$BIN" "$INPUT/$ini" 2>&1 | grep "Maximum average receiving traffic" || true)
  if [ -n "$line" ]; then
    echo "$line" | sed 's/.*: / /' >> "$SUMMARY"
  else
    echo " (no saturation line)" >> "$SUMMARY"
  fi
}

cd "$(dirname "$0")"
if [ ! -x "$BIN" ]; then
  echo "Building..."
  (cd build && make -j8 ChipletNetworkSim)
fi

echo ""
echo "=== 1. Buffer size (0->1, min_routing) ==="
echo "1. Buffer size (0->1, min_routing)" >> "$SUMMARY"
run_one "buffer_64"  "nvswitch_single_flow_same_topology.ini"
run_one "buffer_128" "nvswitch_single_flow_buffer128.ini"
run_one "buffer_256" "nvswitch_single_flow_buffer256.ini"
run_one "buffer_512" "nvswitch_single_flow_buffer512.ini"

echo ""
echo "=== 2. Path: same-group (0->1) vs cross-group (0->8) ==="
echo "2. Path (0->1 vs 0->8, min_routing)" >> "$SUMMARY"
run_one "path_0->1" "nvswitch_single_flow_same_topology.ini"
run_one "path_0->8" "nvswitch_single_flow_dest8.ini"

echo ""
echo "=== 3. Routing: direct (single port) vs min (spray) ==="
echo "3. Routing (direct vs min, 0->1)" >> "$SUMMARY"
run_one "routing_direct" "nvswitch_single_flow_direct.ini"
run_one "routing_min"    "nvswitch_single_flow_same_topology.ini"

echo ""
echo "Summary written to $SUMMARY"
cat "$SUMMARY"
