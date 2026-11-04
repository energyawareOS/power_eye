# Observer-effect benchmark harness

Measures what a monitoring tool (power_eye, node_exporter, ...) costs the system
it observes, without the measurement itself disturbing it.

## Reference measurement (no mid-window activity)

| Source | How | Why it is quiet |
|---|---|---|
| RAPL energy (`pkg`, `cores`, `ram`) | `perf stat -a -x,` **without** `-I` | counting mode, read once at exit |
| HW C-state residency (`cstate_pkg`, `cstate_core`) | same `perf stat -a`, TSC ticks / (TSC MHz x window x instances) | counting mode, read once at exit |
| C-state residency, idle entries | `/sys/devices/system/cpu/cpu*/cpuidle/state*/{usage,time}` diff | two reads: start and end |
| Pkg C-states, IRQ, watts | `turbostat -q -S <cmd>` (command-wrapping mode) | snapshots at start/end only |
| Interrupts, context switches | `/proc/stat` diff | two reads |
| Observer's own wakeups/CPU | `/proc/<pid>/task/*/status` + `schedstat` (ns) diff | two reads |

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
     -S node_exporter='sh -c "node_exporter --collector.disable-defaults --collector.rapl & sleep 2; while sleep 15; do curl -s localhost:9100/metrics >/dev/null; done"'
tools/bench/aggregate.py results/desktop      # -> runs.csv, summary.md
tools/bench/test_aggregate.py                 # unit test, no root needed
```

The implicit baseline `none` runs nothing; `summary.md` reports paired
differences against it within the same trial (mean ± 95% CI, `*` when the CI
excludes 0). `env.txt` records kernel, cmdline, cpuidle driver/state flags,
intel_idle parameters, governors, RAPL limits and DMI/BIOS info. **BIOS C-state
options are not readable from the OS: always pass them with `--note`** (the
list of cpuidle states in `env.txt` shows their effect).

## Dose-response: energy per observer sample

Run the same observer at several sample rates; `summary.md` then regresses
package power on the rate (taken from the scenario's `-i <seconds>`, 0 for the
baseline). The slope in mW/Hz is the energy of one sample in mJ, including the
remote wakeups it causes (e.g. the IPI to each package's lead CPU that
`energy_uj` reads trigger). Package power is noisy (~0.25 W between paired
runs), so use rates that move it clearly and enough trials:

```bash
sudo tools/bench/bench.sh -o results/dose -d 300 -n 8 -s 30 --note "..." \
     -S hz1='build/src/power_eye -i 1 -t 100000000 -f cE' \
     -S hz5='build/src/power_eye -i 0.2 -t 100000000 -f cE' \
     -S hz20='build/src/power_eye -i 0.05 -t 100000000 -f cE' \
     -S hz100='build/src/power_eye -i 0.01 -t 100000000 -f cE'
tools/bench/aggregate.py results/dose                 # all rates
tools/bench/aggregate.py results/dose --max-rate 20   # low-rate region only
```

At high rates the CPUs stop reaching deep C-states and power saturates, so the
relationship is not linear over the whole range. Compare the slope with
different `--max-rate` values and use the low-rate one for the cost at 1 Hz.

## Caveats

- **A passive exporter costs nothing until it is scraped.** Run node_exporter
  together with a scraper (see the example above, 15 s like a typical Prometheus
  config), otherwise its wakeups and CPU are legitimately 0.
- Each metric is divided by the interval spanned by its own start/end snapshots
  (recorded in `raw/*/times`), not by the outer window; RAPL watts use the
  `sleep` duration `-d`.

- Observer commands are `exec`'d in their own session and must not need `sudo`
  (the harness already runs as root). Paths must not contain spaces.
- The observer and all its descendants are tracked (wrappers such as `sudo` or
  shell scripts are fine); only processes alive at both ends of the window are
  counted. The trace pass matches on the descendants' `comm` names, which
  Go/multi-threaded programs share across threads.
- `summary.md` starts with a `Warnings` section (missing RAPL/turbostat data,
  observer not running, no deep C-state residency) -- read it before the tables.
- cpuidle sysfs counters (`time`, `usage`) only update when a CPU leaves idle, so
  a remote read misses the idle period in progress. `bench.sh` therefore reads
  each CPU's counters from that CPU (`taskset`); before that fix residency read
  up to ~3% too high on 30 s windows. The hardware counters (`core_c6_pct`,
  `pkg_c*_pct`) have no such issue and are the reference for deep residency.
- `summary.md` flags runs whose `pkg_W` is far from the median (other activity
  in the window). With few trials one such run dominates the confidence interval.
- A desktop with background activity has thousands of idle entries/s; effects
  smaller than that noise need long windows and many trials. Close other
  applications and fix the CPU governor/turbo setting before measuring.
- `BENCH_WINDOW_CMD` replaces the measured command (e.g. `sleep 5`); it exists
  only to test the plumbing without root.

## Five-condition comparison and CSV export

`run_conditions.sh` runs none / fixed (0.1, 1, 10 s) / adaptive (`-A 600`) /
ebpf (`-B`) / node_exporter in one session and exports the raw per-run rows
(no averaging) with `export_results.py`:

```bash
sudo tools/bench/run_conditions.sh results/conditions "BIOS: pkg C-state limit C6(retention), C6 on, C1E on, turbo off" 300 8 300
```

Outputs `results.csv` (`condition, interval_s, run, pkg_power_w,
deep_cstate_pct, wakeups_per_s, tool_idle_exits_per_s`) and a short `env.txt`
(CPU model, kernel, BIOS note, window); the full environment is kept as
`env_full.txt`. `deep_cstate_pct` is package C6+C7 residency, `wakeups_per_s`
is system-wide idle entries, `tool_idle_exits_per_s` is the observer's
voluntary context switches (it does not include CPUs woken by IPI for sysfs
RAPL reads). Run time is about 7 x trials x (settle + window + cooldown)
(about 9.5 h with the defaults); the long settle lets `adaptive` reach its
plateau before the window starts.

## Realistic-period comparison (interleaved)

`run_realistic.sh` compares power_eye (sysfs and `-B` at `-i 1, 2, 4`), adaptive (`-A 600`), node_exporter +
Prometheus (15 s scrape, default collectors; versions recorded in `env.txt`) on the same machine and BIOS state:

```bash
sudo tools/bench/run_realistic.sh results/realistic "BIOS: pkg C-state limit C6(retention), C6 on, C1E on, turbo off"
DRY=1 tools/bench/run_realistic.sh results/realistic "..." 600 10   # estimate only
```

Design: 5-minute windows, 5 trials, `--interleave` (`none` before, between and after every scenario; scenario
order shuffled in odd trials and reversed in even ones, so drift cancels), each run is paired with its neighbouring
`none` runs by `aggregate.py`, adaptive gets a 600 s settle (`--settle-for`) so the interval has mostly ramped up
before the window starts. Time: about 9 hours with the defaults (8 scenarios, 17 runs per trial). Window, trials and
settle are the 3rd to 5th arguments; `DRY=1` prints the estimate. The 10-minute / 10-trial design takes about 35 h.

Metrics: package power, deep C-state residency, system-wide idle entries per second, IPIs per second
(`ipi_call_per_s`, `ipi_res_per_s` from `/proc/interrupts`, two reads per window), observer CPU time. Tool-induced
idle exits are measured as the **system-wide idle-entry difference to the neighbouring baseline**, which includes CPUs
woken by IPI (a per-process counter does not). `export_results.py` writes it as `tool_idle_exits_per_s`.

`wake_energy.py DIR[:scenarios]...` regresses the paired power increase on the tool-induced idle entries (mJ per
extra idle entry) and predicts the cost of low rates; see `results/wake-energy/summary.md` for the result on the
existing 5/20/100 Hz data.
