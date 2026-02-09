#!/bin/bash
# Run NVL256 multilink Ring AllReduce for 8, 16, 32, 64, 128, 256 GPU (0.5~18 injection scan)
# 64 GPU uses existing config output; others write to output/nvswitch_nvl256_multilink_*gpu.csv
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$SCRIPT_DIR/build"
INPUT="$SCRIPT_DIR/input"
OUTPUT="$SCRIPT_DIR/output"
mkdir -p "$OUTPUT"

for n in 8 16 32 64 128 256; do
  if [ "$n" = "64" ]; then
    ini="nvswitch_nvl256_multilink.ini"
  else
    ini="nvswitch_nvl256_multilink_${n}gpu.ini"
  fi
  echo "===== Running $n GPU: $ini ====="
  "$BUILD/ChipletNetworkSim" "$INPUT/$ini" || { echo "Failed $n GPU"; exit 1; }
done
echo "===== All scale experiments done ====="
