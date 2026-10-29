#!/usr/bin/env bash
# Reruns replace generated results in this checkout. Copy results first to archive an older run.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p results/nice
make clean all test 2>&1 | tee results/tests.log
bash scripts/capture_environment.sh > results/environment.txt
make demo 2>&1 | tee results/demo.log
printf '%s\n' './build/nice_benchmark --rounds 1 --iterations 5000000 --seed 20261029 --output results/nice/smoke.csv'
./build/nice_benchmark --rounds 1 --iterations 5000000 --seed 20261029 --output results/nice/smoke.csv > results/nice/smoke.stdout.log 2> results/nice/smoke.stderr.log
printf '%s\n' './build/nice_benchmark --rounds 10 --iterations 100000000 --seed 20261029 --output results/nice/raw.csv'
./build/nice_benchmark --rounds 10 --iterations 100000000 --seed 20261029 --output results/nice/raw.csv > results/nice/run.stdout.log 2> results/nice/run.stderr.log
python3 scripts/analyze_benchmark.py results/nice/raw.csv results/nice | tee results/nice/analysis.log
printf 'EXIT 0: full run completed\n'
