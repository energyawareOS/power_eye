# Observer-effect summary

## Warnings

- trial 1 hz100: pkg_W 49.71 W is an outlier (median 45.04 W, idle entries 3462/s); other activity during the window? see raw/001_hz100
- trial 1 hz20: pkg_W 45.75 W is an outlier (median 45.04 W, idle entries 804/s); other activity during the window? see raw/001_hz20
- trial 2 none: pkg_W 45.86 W is an outlier (median 45.04 W, idle entries 699/s); other activity during the window? see raw/002_none
- trial 3 hz20: pkg_W 45.66 W is an outlier (median 45.04 W, idle entries 871/s); other activity during the window? see raw/003_hz20
- trial 6 hz1: pkg_W 45.92 W is an outlier (median 45.04 W, idle entries 801/s); other activity during the window? see raw/006_hz1
- trial 7 none: pkg_W 45.95 W is an outlier (median 45.04 W, idle entries 772/s); other activity during the window? see raw/007_none
- trial 8 hz5: pkg_W 45.91 W is an outlier (median 45.04 W, idle entries 742/s); other activity during the window? see raw/008_hz5

Mean ± 95% CI over trials. Baseline scenario: `none`.

## Per scenario

| scenario | n | pkg_W | ram_W | deep_resid_pct | core_c6_pct | pkg_c2_pct | pkg_c6_pct | idle_entries_per_s | intr_per_s | ctxt_per_s | obs_vol_ctxt_per_s | obs_cpu_ms_per_s | ts_PkgWatt |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| none | 8 | 45.193 ± 0.381 | 10.972 ± 0.061 | 99.888 ± 0.087 | 99.460 ± 0.251 | 0.000 ± 0.000 | 0.000 ± 0.000 | 451.930 ± 148.351 | 431.121 ± 141.097 | 439.939 ± 74.241 | - | - | 45.194 ± 0.381 |
| hz1 | 8 | 45.133 ± 0.285 | 10.942 ± 0.038 | 99.909 ± 0.066 | 99.519 ± 0.207 | 0.000 ± 0.000 | 0.000 ± 0.000 | 425.715 ± 126.847 | 402.820 ± 121.045 | 459.569 ± 54.519 | 2.752 ± 0.383 | 0.190 ± 0.004 | 45.131 ± 0.286 |
| hz100 | 8 | 45.662 ± 1.370 | 10.992 ± 0.050 | 99.596 ± 0.398 | 98.749 ± 0.840 | 0.000 ± 0.000 | 0.000 ± 0.000 | 1164.158 ± 777.941 | 749.944 ± 701.343 | 1259.038 ± 737.931 | 286.779 ± 22.689 | 17.863 ± 0.210 | 45.664 ± 1.370 |
| hz20 | 8 | 45.191 ± 0.281 | 10.959 ± 0.056 | 99.857 ± 0.088 | 99.362 ± 0.258 | 0.000 ± 0.000 | 0.000 ± 0.000 | 543.102 ± 155.882 | 455.470 ± 146.971 | 548.945 ± 81.079 | 54.284 ± 6.082 | 3.626 ± 0.033 | 45.191 ± 0.282 |
| hz5 | 8 | 45.143 ± 0.330 | 10.961 ± 0.059 | 99.880 ± 0.087 | 99.438 ± 0.264 | 0.000 ± 0.000 | 0.000 ± 0.000 | 469.623 ± 161.859 | 430.392 ± 154.239 | 453.452 ± 88.620 | 14.642 ± 0.603 | 0.933 ± 0.013 | 45.142 ± 0.329 |

## C-state residency (% of all CPU time)

| scenario | nonidle | C1E | C1 | C3 | C6 | POLL |
|---|---|---|---|---|---|---|
| none | 0.1 | 0.0 | 0.0 | 0.1 | 99.8 | 0.0 |
| hz1 | 0.1 | 0.0 | 0.0 | 0.1 | 99.8 | 0.0 |
| hz100 | 0.3 | 0.0 | 0.1 | 0.1 | 99.5 | 0.0 |
| hz20 | 0.1 | 0.0 | 0.0 | 0.1 | 99.8 | 0.0 |
| hz5 | 0.1 | 0.0 | 0.0 | 0.1 | 99.8 | 0.0 |

## Paired difference vs `none` (same trial)

`*` = 95% CI excludes 0. Observer-only metrics (`obs_*`, `idle_exit_obs_*`) count as 0 in the baseline.

| scenario | n | pkg_W | ram_W | deep_resid_pct | core_c6_pct | pkg_c2_pct | pkg_c6_pct | idle_entries_per_s | intr_per_s | ctxt_per_s | obs_vol_ctxt_per_s | obs_cpu_ms_per_s | ts_PkgWatt |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| hz1 | 8 | -0.060 ± 0.510 | -0.029 ± 0.067 | 0.022 ± 0.117 | 0.058 ± 0.349 | 0.000 ± 0.000 | 0.000 ± 0.000 | -26.215 ± 209.416 | -28.301 ± 198.217 | 19.630 ± 91.820 | 2.752 ± 0.383* | 0.190 ± 0.004* | -0.063 ± 0.510 |
| hz100 | 8 | 0.468 ± 1.492 | 0.020 ± 0.080 | -0.292 ± 0.427 | -0.712 ± 0.939 | 0.000 ± 0.000 | 0.000 ± 0.000 | 712.228 ± 839.300 | 318.823 ± 762.674 | 819.099 ± 791.149* | 286.779 ± 22.689* | 17.863 ± 0.210* | 0.470 ± 1.492 |
| hz20 | 8 | -0.002 ± 0.564 | -0.013 ± 0.072 | -0.030 ± 0.142 | -0.098 ± 0.413 | 0.000 ± 0.000 | 0.000 ± 0.000 | 91.172 ± 244.014 | 24.349 ± 234.316 | 109.006 ± 114.324 | 54.284 ± 6.082* | 3.626 ± 0.033* | -0.003 ± 0.563 |
| hz5 | 8 | -0.050 ± 0.553 | -0.011 ± 0.082 | -0.008 ± 0.140 | -0.023 ± 0.412 | 0.000 ± 0.000 | 0.000 ± 0.000 | 17.693 ± 241.874 | -0.729 ± 230.858 | 13.513 ± 115.077 | 14.642 ± 0.603* | 0.933 ± 0.013* | -0.051 ± 0.553 |

## Dose-response (observer sample rate -> package power)

Rates in the fit: 0, 1, 5, 20 Hz (limited by --max-rate 20). Slope is the energy cost of one observer sample (timer wake plus the remote wakeups it causes).

| response | intercept | slope per Hz | 95% CI | R² | runs |
|---|---|---|---|---|---|
| pkg_W | 45.156 | 1.478 mW/Hz = mJ/sample | ± 16.784 (n.s.) | 0.00 | 32 |
| core_c6_pct | 99.486 | -0.006 %-points/Hz | ± 0.013 (n.s.) | 0.03 | 32 |
| idle_entries_per_s | 438.488 | 5.247 entries/s per Hz | ± 7.762 (n.s.) | 0.06 | 32 |

Check linearity before trusting the slope: compare fits with `--max-rate` set to the lower and upper half of the rates.

_Trace-pass metrics (`idle_exit_*`) come from a separate perturbing bpftrace window and are not comparable to power numbers._
