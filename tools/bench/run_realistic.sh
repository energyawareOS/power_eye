#!/usr/bin/env bash
# run_realistic.sh - comparison at realistic sampling periods, same server and BIOS.
#
#   sudo tools/bench/run_realistic.sh OUTDIR "BIOS note" [window_s] [trials] [settle_s]
#
# Scenarios (baseline `none` is run before, between and after every one, see
# bench.sh --interleave; the order of the others alternates between trials):
#   fixed_1/2/4   power_eye sysfs, -i 1/2/4
#   ebpf_1/2/4    power_eye -B, -i 1/2/4
#   adaptive      power_eye -B -i 1 -A 600 (long settle so the interval has ramped up)
#   node_exporter node_exporter (default collectors) + Prometheus scraping every 15 s
# Tool versions are recorded in env.txt (node_exporter/prometheus --version).
# node_exporter exports RAPL through its `rapl` collector; check that it is enabled
# with `node_exporter --help | grep rapl` and `curl -s localhost:9100/metrics | grep rapl`.
#
# Defaults: 300 s window, 5 trials, 30 s settle, 600 s settle for adaptive (about 9 h).
# Time: trials x (2 x scenarios + 1) x (settle + window + cooldown + snapshots); try DRY=1.
set -eu
out=${1:?usage: $0 OUTDIR "BIOS note" [window_s] [trials] [settle_s]}
note=${2:?BIOS C-state settings must be given (they are not readable from the OS)}
dur=${3:-300} n=${4:-5} settle=${5:-30}
here=$(cd "$(dirname "$0")" && pwd)
pe=${POWER_EYE:-build/src/power_eye}
tdb=/tmp/bench-prom-tsdb

args=()
for i in 1 2 4; do args+=(-S "fixed_$i=$pe -i $i -t 100000000 -f cE"); done
for i in 1 2 4; do args+=(-S "ebpf_$i=$pe -B -i $i -t 100000000 -f cE"); done
args+=(-S "adaptive=$pe -B -i 1 -A 600 -t 100000000 -f cE" --settle-for adaptive=600)
args+=(-S "node_exporter=sh -c 'rm -rf $tdb; node_exporter & prometheus --config.file=$here/scenarios/prometheus.yml --storage.tsdb.path=$tdb --web.listen-address=127.0.0.1:9090 & wait'")

dry=()
[ "${DRY:-0}" = 1 ] && dry=(--dry-run)
"$here/bench.sh" -o "$out" -d "$dur" -n "$n" -s "$settle" --interleave --note "$note" "${dry[@]}" "${args[@]}"
[ "${DRY:-0}" = 1 ] && exit 0
"$here/aggregate.py" "$out"
"$here/export_results.py" "$out"
"$here/wake_energy.py" "$out" > "$out/wake_energy.md" || true
