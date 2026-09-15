#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
elf="${1:-build/blessed-llvm-pie.elf}"
readelf --dyn-syms --wide "$elf" \
  | grep ' UND ' \
  | sed -E 's/^.*[[:space:]]UND[[:space:]]+//' \
  | sed -E 's/[[:space:]]+$//' \
  | sort -u
