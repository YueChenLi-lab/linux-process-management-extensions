#!/usr/bin/env bash
set -euo pipefail
printf 'Captured UTC: '; date -u +%FT%TZ
printf '\nKernel (hostname omitted):\n'; uname -srm
printf '\nDistribution:\n'; cat /etc/os-release
printf '\nCompiler:\n'; gcc --version | head -1
printf '\nPython:\n'; python3 --version
printf '\nCPU and affinity:\n'
awk '/model name/ {print; exit}' /proc/cpuinfo
awk '/Cpus_allowed_list|Mems_allowed_list/ {print}' /proc/self/status
printf '\nCPU quota (cgroup v2 if available):\n'
if [[ -r /sys/fs/cgroup/cpu.max ]]; then cat /sys/fs/cgroup/cpu.max; else printf 'NOT_AVAILABLE\n'; fi
printf '\nAutogroup setting (if exposed):\n'
if [[ -r /proc/sys/kernel/sched_autogroup_enabled ]]; then cat /proc/sys/kernel/sched_autogroup_enabled; else printf 'NOT_AVAILABLE\n'; fi
printf '\nBuild command:\ngcc -Iinclude -std=c11 -O2 -Wall -Wextra -Werror src/{module}.c -o build/{module}\n'
printf '\nSource/executable fingerprints:\n'
sha256sum src/*.c include/*.h build/pcb_queues build/scheduler build/nice_benchmark
