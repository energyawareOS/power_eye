#!/usr/bin/env bash
# run_conditions.sh - the five-condition comparison (none / fixed / adaptive /
# ebpf / node_exporter) with bench.sh, then export results.csv and env.txt.
#
#   sudo tools/bench/run_conditions.sh OUTDIR "BIOS note" [window_s] [trials] [settle_s]
#
# Defaults: 300 s window, 8 trials, 300 s settle. The settle time matters for
# "adaptive": the interval doubles from 1 s, so it needs several minutes to
# reach its plateau before the window starts. Total time is roughly
# 7 scenarios x trials x (settle + window + cooldown).
set -eu
out=${1:?usage: $0 OUTDIR "BIOS note" [window_s] [trials] [settle_s]}
note=${2:?BIOS C-state settings must be given (they are not readable from the OS)}
dur=${3:-300} n=${4:-8} settle=${5:-300}
here=$(cd "$(dirname "$0")" && pwd)
pe=${POWER_EYE:-build/src/power_eye}

"$here/bench.sh" -o "$out" -d "$dur" -n "$n" -s "$settle" --note "$note" \
    -S fixed_0.1="$pe -i 0.1 -t 100000000 -f cE" \
    -S fixed_1="$pe -i 1 -t 100000000 -f cE" \
    -S fixed_10="$pe -i 10 -t 100000000 -f cE" \
    -S adaptive="$pe -B -i 1 -A 600 -t 100000000 -f cE" \
    -S ebpf="$pe -B -i 1 -t 100000000 -f cE" \
    -S node_exporter='sh -c "node_exporter --collector.disable-defaults --collector.rapl & sleep 2; while sleep 15; do curl -s localhost:9100/metrics >/dev/null; done"'
"$here/aggregate.py" "$out"
"$here/export_results.py" "$out"
