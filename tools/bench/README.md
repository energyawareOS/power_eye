# Observer-effect benchmark harness

Measures what a monitoring tool (power_eye, node_exporter, ...) costs the system
it observes, without the measurement itself disturbing it.

## Reference measurement (no mid-window activity)

| Source | How | Why it is quiet |
|---|---|---|
| RAPL energy (`pkg`, `cores`, `ram`) | `perf stat -a -x,` **without** `-I` | counting mode, read once at exit |
| C-state residency, idle entries | `/sys/devices/system/cpu/cpu*/cpuidle/state*/{usage,time}` diff | two reads: start and end |
| Pkg C-states, IRQ, watts | `turbostat -q -S <cmd>` (command-wrapping mode) | snapshots at start/end only |
| Interrupts, context switches | `/proc/stat` diff | two reads |
| Observer's own wakeups/CPU | `/proc/<pid>/task/*/status` + `stat` diff | two reads |

Optional `--trace`: a **separate** `bpftrace` window counts idle exits
(`sched_switch` with `prev_pid == 0`) in total and for the observer's `comm`.
It perturbs the system, so it is never mixed into the power numbers.
`--ipmi` records `ipmitool dcmi power reading` before/after (not yet validated
on real hardware).

## Usage

```bash
# 10 trials x 3 scenarios, 10 min windows; scenario order is shuffled per trial
sudo tools/bench/bench.sh -o results/desktop -d 600 -n 10 -s 60 \
     --note "BIOS C-states: C6 on, C1E on, package C-state limit auto" \
     -S pe1s='build/src/power_eye -i 1 -t 100000000' \
     -S node_exporter='node_exporter --collector.disable-defaults --collector.rapl'
tools/bench/aggregate.py results/desktop      # -> runs.csv, summary.md
tools/bench/test_aggregate.py                 # unit test, no root needed
```

The implicit baseline `none` runs nothing; `summary.md` reports paired
differences against it within the same trial (mean ± 95% CI, `*` when the CI
excludes 0). `env.txt` records kernel, cmdline, cpuidle driver/state flags,
intel_idle parameters, governors, RAPL limits and DMI/BIOS info. **BIOS C-state
options are not readable from the OS: always pass them with `--note`** (the
list of cpuidle states in `env.txt` shows their effect).

## Caveats

- Observer commands are `exec`'d in their own session and must not need `sudo`
  (the harness already runs as root). Paths must not contain spaces.
- The observer and all its descendants are tracked (wrappers such as `sudo` or
  shell scripts are fine); only processes alive at both ends of the window are
  counted. The trace pass matches on the descendants' `comm` names, which
  Go/multi-threaded programs share across threads.
- `summary.md` starts with a `Warnings` section (missing RAPL/turbostat data,
  observer not running, no deep C-state residency) -- read it before the tables.
- A desktop with background activity has thousands of idle entries/s; effects
  smaller than that noise need long windows and many trials. Close other
  applications and fix the CPU governor/turbo setting before measuring.
- `BENCH_WINDOW_CMD` replaces the measured command (e.g. `sleep 5`); it exists
  only to test the plumbing without root.
