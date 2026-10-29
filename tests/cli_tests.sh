#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
checks=0
reject() {
  if "$@" >"$tmp/stdout" 2>"$tmp/stderr"; then
    echo "FAIL: accepted invalid input: $*" >&2; exit 1
  fi
  checks=$((checks + 1))
}
for content in 'A,0,0' 'A,-1,3' 'A,0,-2' 'A,0,2,extra' 'A,0,99999999999999999999' 'A,0,1\nA,1,2' 'bad id,0,1' 'IDLE,0,1' 'A,0,1\nB,0,1000000000' '' 'A,0,1.5' 'A,0,2junk'; do
  printf '%b\n' "$content" >"$tmp/bad.csv"
  reject ./build/scheduler "$tmp/bad.csv" rr 2 "$tmp/out"
done
for q in 0 -1 1.5 abc 100000000000; do reject ./build/scheduler examples/workload_baseline.csv rr "$q" "$tmp/out"; done
reject ./build/scheduler examples/workload_baseline.csv unknown 2 "$tmp/out"
reject ./build/scheduler "$tmp/missing" rr 2 "$tmp/out"
for scenario in 'create A 0' 'create A 1\ncreate A 2' 'create A 1\nwrong' 'step extra' 'create A 1 extra' 'create bad, 1'; do
  printf '%b\n' "$scenario" >"$tmp/bad.txt"
  reject ./build/pcb_queues --scenario "$tmp/bad.txt"
done
./build/pcb_queues --demo >"$tmp/demo.csv"
./build/pcb_queues --scenario examples/queue_scenario.txt >"$tmp/scenario.csv"
cmp "$tmp/demo.csv" "$tmp/scenario.csv"
checks=$((checks + 1))
for args in '--rounds 0' '--iterations 0' '--seed bad' '--cpu 999999' '--timeout 0' '--bogus 1'; do
  read -r -a options <<< "$args"
  reject ./build/nice_benchmark "${options[@]}" --output "$tmp/nice.csv"
done
./build/nice_benchmark --help >"$tmp/help"
checks=$((checks + 1))
printf 'CLI PASS: %d checks (invalid inputs + deterministic demo + help); no timed benchmark in CI\n' "$checks"
