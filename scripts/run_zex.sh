#!/usr/bin/env bash
# Run both full instruction exercisers and keep their output for diagnosis.
# Usage: bash scripts/run_zex.sh [--max-steps N]
set -u
cd "$(dirname "$0")/.." || exit 1
mkdir -p reports
result=0
for name in zexdoc zexall; do
  echo "=== Running ${name^^} ==="
  ./build/z80zex "tests/external/$name.com" "$@" 2>&1 | tee "reports/$name.log"
  code=${PIPESTATUS[0]}
  echo "=== ${name^^} exit status: $code; log: reports/$name.log ==="
  if (( code != 0 )); then result=1; fi
done
exit "$result"
