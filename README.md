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

`USE_NVML=ON` requires the CUDA toolkit's `nvml` headers/library. Adjust include/lib paths for your distribution if CMake cannot find them (e.g., `/usr/local/cuda/include`, `/usr/local/cuda/lib64`).

## Usage

```bash
./power_eye [-i <interval>] [-t <timeout>] [-d] [-I] [-p <pid>]... [-f <specs>] [-h]
```

Options:
- `-i <interval>`: Sampling interval in seconds; fractional values such as `0.5` are accepted, invalid or sub-millisecond values are rejected (default: 1)
- `-t <timeout>`: Total measurement duration in seconds (default: 120)
- `-d`: Enable DRAM power/energy, summed over all sockets (if no DRAM RAPL domain exists, a warning is printed and DRAM is disabled)
- `-I`: Enable IPMI power measurement
- `-p <pid>`: Monitor a specific process by PID. May be given multiple times.
- `-f <specs>`: Field/output specs. `<specs>` is a string of letters (e.g. `-f cEH`):
  - `d`: Prepend `MM-dd` to the timestamp
  - `c`: Include CPU power/energy columns
  - `g`: Include GPU total power/energy columns (requires `USE_NVML=ON`)
  - `G`: Include per-GPU columns; implies `g`
  - `E`: Include energy `(J)` columns in addition to power `(W)`
  - `H`: Print a one-line header describing the output columns
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
