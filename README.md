# PowerEye

`PowerEye` is a lightweight command-line utility for measuring power consumption using Intel's RAPL (Running Average Power Limit) interface, with optional NVIDIA GPU (NVML) and IPMI sources.
It is implemented in C and built with CMake.

## Features

- CPU package power & energy via Intel RAPL (all sockets are summed)
- Optional DRAM domain power & energy (`-d`)
- Optional system-wide power via IPMI (`-I`)
- Optional GPU total and per-GPU power & energy via NVIDIA NVML (`-f g`, `-f G`)
- Per-process attribution (`-p <pid>`): CPU energy is split by each pid's CPU utilization, and GPU energy is split by each pid's `nvmlDeviceGetProcessUtilization` SM utilization, weighted by per-device power on multi-GPU systems

## Requirements

- **OS:** Linux with `/sys/class/powercap` (RAPL) support. Reading `energy_uj` may require root or a udev rule.
- **CPU:** Intel processor with RAPL package—and optionally DRAM—domains
- **GPU (optional):** NVIDIA driver + NVML (`libnvidia-ml.so`) if using `-f g`/`-f G`
- **eBPF sampler (optional):** `libbpf-dev` and `clang` at build time, root and a kernel with RAPL perf events (`/sys/bus/event_source/devices/power`) at run time, if using `-B`
- **IPMI (optional):** a BMC exposed through the kernel IPMI driver (`/dev/ipmi0`, `/dev/ipmi/0` or `/dev/ipmidev/0`) if using `-I`; `ipmitool` is not required

## Build Instructions

Make sure you have CMake (version 3.22 or higher) and a C compiler installed.

```bash
git clone https://github.com/your-username/power_eye.git
cd power_eye
cmake -B build
cmake --build build
```

After building, the executable will be available at `build/power_eye`.

### Build variants

- CPU-only (no NVML dependency)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

- NVML-enabled build (adds GPU monitoring)

```bash
cmake -B build -DUSE_NVML=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

- eBPF sampler build (adds `-B`)

```bash
cmake -B build -DUSE_BPF=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

`USE_BPF=ON` needs `libbpf` (found with pkg-config) and `clang`; the BPF object is compiled during the build and embedded in the executable. Without it, `-B` prints a message and sysfs is used.

`USE_NVML=ON` requires the CUDA toolkit's `nvml` headers/library. Adjust include/lib paths for your distribution if CMake cannot find them (e.g., `/usr/local/cuda/include`, `/usr/local/cuda/lib64`).

## Usage

```bash
./power_eye [-i <interval>] [-t <timeout>] [-d] [-I] [-B] [-p <pid>]... [-A <max>] [-T <watts>] [-C <hours>] [-W <sec>] [-D <watts>] [-R <percent>] [-F <watts>] [-f <specs>] [-h]
```

Options:
- `-i <interval>`: Sampling interval in seconds; fractional values such as `0.5` are accepted, invalid or sub-millisecond values are rejected (default: 1)
- `-t <timeout>`: Total measurement duration in seconds (default: 120)
- `-A <max>`: Adaptive sampling interval. `-i` is the base interval. While the measured power stays within a tolerance band of the running mean, the next interval doubles up to `<max>` seconds; as soon as a sample leaves the band it returns to `-i` and the band restarts at the new level. The cap is lowered to the RAPL counter wrap bound (`max_energy_range_uj / max power / 2`, from powercap; domains that do not report a maximum power are assumed to reach 300 W for packages and 100 W for DRAM, so `-d` can lower the cap) so no energy is lost between reads. An `INT(s)` column at the end of every row gives the length of that row's interval. Rows still report power/energy averaged over their own interval.
- `-T <watts>`: Tolerance band for `-A`: a change is a sample more than `max(<watts>, 3% of the mean)` away from the mean of the current quiet period (default 1.0 W).
- `-C <hours>`: Long-idle ("coma") detection; see below. Reports a coma candidate after `<hours>` (fractions allowed) of consecutive idle windows. Needs `-B` for package C-state residency; use it with `-A` so the node is sampled rarely while idle.
- `-W <sec>`: Window length for `-C` (default 600).
- `-D <watts>`: A window is idle if its mean package power is at most the idle floor plus this (default 1.0).
- `-R <percent>`: ... and its deep package C-state (C6) residency, averaged over packages, is at least this (default 80).
- `-F <watts>`: Idle power floor. By default it is learned as the lowest mean power of windows that meet the residency condition, so a busy start cannot define it; give it explicitly for a reliable verdict.
- `-d`: Enable DRAM power/energy, summed over all sockets (if no DRAM RAPL domain exists, a warning is printed and DRAM is disabled)
- `-B`: Read the RAPL energy counters from an eBPF sampler instead of sysfs (needs a `USE_BPF=ON` build and root; falls back to sysfs with a message if it cannot start). A BPF program on the `cpu_idle` tracepoint reads the package (and with `-d` DRAM) energy perf events on the idle exit of whichever CPU of a package wakes first, at most once per a tenth of the interval (10 ms .. 10 s), so reading does not wake any CPU by IPI. power_eye only reads the latest sample at each tick; if it is older than half an interval it takes one itself, on a CPU of that package (this is what `OBS-RAPLRD` counts with `-B`). Power is energy divided by the time between the samples used, so a row's `(J)` covers about one interval (not exactly), and the first row uses a sample taken at start.
- `-I`: Enable IPMI power measurement
- `-p <pid>`: Monitor a specific process by PID. May be given multiple times.
- `-f <specs>`: Field/output specs. `<specs>` is a string of letters (e.g. `-f cEH`):
  - `d`: Prepend `MM-dd` to the timestamp
  - `c`: Include CPU power/energy columns
  - `g`: Include GPU total power/energy columns (requires `USE_NVML=ON`)
  - `G`: Include per-GPU columns; implies `g`
  - `E`: Include energy `(J)` columns in addition to power `(W)`
  - `H`: Print a one-line header describing the output columns
  - `o`: Report the cost of the observation itself (see below)
- `-h`: Show help and exit

## Output Schema

**Timestamp**
- Default: `HH:MM:SS`
- With `-f d`: `MM-DD HH:MM:SS`

**Columns (by flags)**

- **CPU only (`-f c`):**
    ```text
    [time] CPU(W)
    ```

- **CPU with energy (`-f cE`):**
    ```text
    [time] CPU(W) CPU(J)
    ```

- **CPU + DRAM (`-d -f c`):**
    ```text
    [time] CPU(W) DRAM(W)
    ```

- **CPU + DRAM with energy (`-d -f cE`):**
    ```text
    [time] CPU(W) CPU(J) DRAM(W) DRAM(J)
    ```

- **Add GPU total (`-f cg`):**
    ```text
    [time] CPU(W) GPU(W) [GPU(J) if E]
    ```

- **Add per-GPU (`-f cG`):**
    ```text
    [time] CPU(W) GPU(W) [GPU(J)] GPU00(W) [GPU00(J)] GPU01(W) [GPU01(J)] ...
    ```

- **IPMI (`-I`):**
  The IPMI columns come after the GPU columns and before the per-process columns, matching the header order:
    ```text
    ... IPMI(W) [IPMI(J)] ...
    ```
  If a reading fails, `-` is printed in place of each value so the column count stays constant.

- **Per-process attribution (`-p <pid>`):**
  Per-pid columns are appended at the end. Without GPU, each pid contributes one (or two with `E`) columns:
    ```text
    ... PID<n>(W) [PID<n>(J)] ...
    ```
  With GPU enabled (`-f g` or `-f G`), each pid contributes CPU and GPU columns:
    ```text
    ... PID<n>-CPU(W) [PID<n>-CPU(J)] PID<n>-GPU(W) [PID<n>-GPU(J)] ...
    ```
  Per-pid GPU values are derived from `nvmlDeviceGetProcessUtilization` SM samples and are proportional to the total GPU power/energy across all NVIDIA devices.

- **Self-overhead (`-f o`):**
  Three columns are appended after the per-process columns, and a one-line summary is printed to stderr at exit:
    ```text
    ... OBS-CPU(ms/s) OBS-WAKE(/s) OBS-RAPLRD(/s)
    ```
  - `OBS-CPU(ms/s)`: CPU time used by power_eye itself (all threads) per second.
  - `OBS-WAKE(/s)`: how often power_eye was put on a CPU per second (from `schedstat`).
  - `OBS-RAPLRD(/s)`: RAPL `energy_uj` reads per second. Each read runs an MSR read on a CPU of that domain's package and, unless power_eye happens to run there, wakes that CPU by IPI, so this is an upper bound on the wakeups caused on other CPUs (DRAM domains with `-d` add reads). With `-B` it counts the samples power_eye forced itself, normally 0 on a busy system.

  `OBS-CPU` and `OBS-WAKE` need `CONFIG_SCHED_INFO`; without it they show `-`. This reports what power_eye itself did; it does not measure the system-wide effect (extra idle exits, reduced package C-state residency).

- **Adaptive interval (`-A`):**
  One column, `INT(s)`, is appended after all other columns (after the self-overhead columns, if any). Averaging over a long interval hides short bursts: a burst raises the average only by `burst energy / interval`, so with a long cap only sustained changes are noticed. The interval is changed at the end of a tick; timestamps and `(J)` columns use the interval of that row. Per-GPU process utilization (`-f g`) still assumes the interval it was started with and is not adjusted.

- **Coma detection (`-C`):**
  Time is cut into windows (`-W`, default 10 minutes). A window is idle if its mean package power is within `-D` of the idle floor and its package C6 residency is at least `-R`. After `<hours>` of consecutive idle windows the node is a coma candidate; a busy window ends it. Output goes to stdout as `#` lines after the data row that closed the window, and a summary to stderr at exit:
    ```text
    # window 10-04 10:00:00 .. 10-04 10:10:00 25.412 W (floor 25.301 W, +0.111 W) deep 86.3% -> idle (idle for 1.00 h)
    # COMA CANDIDATE: idle since 10-04 09:10:00 (1.00 h)
    # coma candidacy ended: busy window ending 10-04 12:30:00
    ```
  With `-A`, the interval is cut so that ticks end on window boundaries, and besides power the deep residency now also resets the interval to `-i` when it moves by more than 10 percentage points. The verdict is made at a window end, so it comes at most one window after the idle time reaches `<hours>`. Without `-B` (or without a `cstate_pkg` PMU) there is no residency and the verdict uses power only, which is unreliable; a warning says so. A short burst inside a window is averaged away, as with `-A`. Limitation: if the node has small periodic activity (e.g. 1-1.5 W every minute or so), changes larger than the `-T` band keep resetting the interval to `-i`, so it may take a long time to reach the cap. In a 90 minute server run the interval first reached 600 s after about 1 hour; the coma verdict itself was unaffected (see `results/coma-long/summary.md`). A wider `-T` reduces this at the cost of reacting less to real changes.
  Example:
    ```bash
    sudo ./power_eye -B -d -i 1 -A 600 -C 6 -t 100000000 -f cEH
    ```

## Examples

- CPU power, with energy and header, sampled every 1s for 30s:
    ```bash
    ./power_eye -t 30 -f cEH
    ```

- CPU + DRAM + GPU per-device, with energy and header:
    ```bash
    ./power_eye -d -t 60 -f cGEH
    ```

- Per-process CPU/GPU attribution for two pids:
    ```bash
    ./power_eye -p 1234 -p 5678 -t 60 -f cgEH
    ```

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.

## Notes

- **Platform support**
  - Requires Linux with RAPL exposed under `/sys/class/powercap`.
  - Works on **Intel** CPUs that support RAPL. AMD/others are not supported.
  - All CPU packages (`intel-rapl:N`) are read and summed; DRAM subdomains are summed the same way. Per-socket breakdown is not available.
  - RAPL counter wraparound is handled using `max_energy_range_uj`.
  - The DRAM RAPL domain may not be present on all platforms.

- **Permissions**
  - Reading `energy_uj` may require elevated privileges. Use `sudo` or adjust permissions via a udev rule (distro-specific).

- **DRAM flag (`-d`)**
  - If your system lacks a DRAM RAPL domain, a warning is printed and DRAM output is disabled; the run continues with CPU only.

- **GPU measurement (`-f g`, `-f G`)**
  - Requires NVIDIA driver + NVML (`libnvidia-ml.so`) and a build with `-DUSE_NVML=ON`.
  - `-f G` includes per-GPU metrics and implies `-f g`.
  - If NVML isn't available or no NVIDIA GPUs are present, GPU measurement will not work.

- **Per-process measurement (`-p <pid>`)**
  - CPU energy per pid is proportional to the pid's CPU time delta over the interval (from `/proc/<pid>/stat`).
  - GPU energy per pid is proportional to its average `smUtil` from `nvmlDeviceGetProcessUtilization`. On multi-GPU systems, each device's contribution is weighted by that device's current power so a pid using only one GPU is not credited for power drawn by idle GPUs.
  - The pid must be visible to the user running `power_eye`. NVML process utilization may require the same user/permissions as the GPU process.

- **Timing & alignment**
  - Using absolute time scheduling, sampling is aligned to second-of-minute boundaries, keeping timing consistent across runs.

- **Units & fields**
  - Power is in **watts (W)** and energy in **joules (J)**.
  - Use `-f E` to include energy columns; `-f H` prints a header row; `-f d` adds `MM-DD` to the timestamp.

- **Accuracy considerations**
  - CPU energy uses RAPL counters (platform-estimated). GPU energy is trapezoid-integrated from instantaneous NVML power readings.
  - Shorter intervals generally improve integration accuracy but increase overhead.

- **Containers/VMs**
  - Some containers or virtual machines do not expose `/sys/class/powercap` or NVML; measurements may be unavailable there.

- **More info**
  - See the Linux powercap documentation: https://www.kernel.org/doc/Documentation/power/powercap/powercap.txt
