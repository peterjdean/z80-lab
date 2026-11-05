#!/usr/bin/env bash
# Run from repo root on Euler AFTER: source ~/emsdk/emsdk_env.sh
set -euo pipefail
cd "$(dirname "$0")/.."
command -v emcmake >/dev/null || { echo 'Missing Emscripten: source ~/emsdk/emsdk_env.sh' >&2; exit 1; }
command -v npm >/dev/null || { echo 'Missing npm: use the Node.js provided by emsdk' >&2; exit 1; }
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm --parallel
npm install --no-audit --no-fund
npm run build:assembler
npm run test:assembler
python3 scripts/check_web.py --site build-wasm/site
printf '\nZ80/LAB browser build ready at: %s/build-wasm/site\n' "$PWD"
