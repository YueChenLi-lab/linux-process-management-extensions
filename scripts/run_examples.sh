#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p results/pcb results/schedule results/schedule/arrivals
./build/pcb_queues --demo > results/pcb/trace.csv
for algorithm in fcfs sjf rr; do
  ./build/scheduler examples/workload_baseline.csv "$algorithm" 2 "results/schedule/$algorithm"
  ./build/scheduler examples/workload_arrivals.csv "$algorithm" 2 "results/schedule/arrivals/$algorithm"
done
"${PYTHON:-python3}" scripts/plot_schedule.py results/schedule
"${PYTHON:-python3}" scripts/plot_schedule.py results/schedule/arrivals
"${PYTHON:-python3}" scripts/plot_queue.py
