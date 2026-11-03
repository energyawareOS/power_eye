# Observer-effect summary

## Warnings

- trial 4 hz1: pkg_W 26.18 W is an outlier (median of hz1 25.61 W, idle entries 790/s); other activity during the window? see raw/004_hz1
- trial 8 hz1: pkg_W 26.20 W is an outlier (median of hz1 25.61 W, idle entries 833/s); other activity during the window? see raw/008_hz1
- trial 6 hz20: pkg_W 26.54 W is an outlier (median of hz20 25.82 W, idle entries 910/s); other activity during the window? see raw/006_hz20
- trial 1 hz5: pkg_W 26.42 W is an outlier (median of hz5 25.62 W, idle entries 799/s); other activity during the window? see raw/001_hz5
- trial 3 none: pkg_W 26.01 W is an outlier (median of none 25.58 W, idle entries 743/s); other activity during the window? see raw/003_none

Mean ± 95% CI over trials. Baseline scenario: `none`.

## Per scenario

| scenario | n | pkg_W | ram_W | deep_resid_pct | core_c6_pct | pkg_c2_pct | pkg_c6_pct | idle_entries_per_s | intr_per_s | ctxt_per_s | obs_vol_ctxt_per_s | obs_cpu_ms_per_s | ts_Pkg%pc6 | ts_PkgWatt |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| none | 8 | 25.615 ± 0.142 | 3.715 ± 0.030 | 99.908 ± 0.075 | 99.623 ± 0.189 | 10.723 ± 0.350 | 86.073 ± 0.204 | 408.535 ± 113.283 | 394.262 ± 109.862 | 418.214 ± 16.843 | - | - | 86.039 ± 0.204 | 25.620 ± 0.142 |
| hz1 | 8 | 25.722 ± 0.252 | 3.744 ± 0.060 | 99.872 ± 0.101 | 99.528 ± 0.271 | 10.678 ± 0.270 | 85.909 ± 0.502 | 471.126 ± 177.600 | 450.697 ± 172.563 | 449.784 ± 84.542 | 2.872 ± 0.292 | 0.224 ± 0.007 | 85.874 ± 0.503 | 25.729 ± 0.250 |
| hz100 | 8 | 27.120 ± 0.277 | 4.042 ± 0.073 | 99.723 ± 0.082 | 99.201 ± 0.221 | 13.737 ± 0.415 | 78.869 ± 0.790 | 889.077 ± 155.197 | 527.490 ± 137.046 | 977.816 ± 91.491 | 274.827 ± 31.333 | 21.022 ± 0.174 | 78.839 ± 0.791 | 27.124 ± 0.278 |
| hz20 | 8 | 25.918 ± 0.279 | 3.765 ± 0.064 | 99.836 ± 0.098 | 99.452 ± 0.269 | 11.005 ± 0.324 | 84.870 ± 0.581 | 551.312 ± 175.709 | 465.426 ± 169.446 | 531.422 ± 87.311 | 53.989 ± 7.507 | 4.263 ± 0.050 | 84.839 ± 0.581 | 25.924 ± 0.278 |
| hz5 | 8 | 25.668 ± 0.268 | 3.743 ± 0.050 | 99.897 ± 0.078 | 99.610 ± 0.206 | 10.832 ± 0.233 | 85.805 ± 0.656 | 420.281 ± 131.442 | 391.451 ± 129.153 | 419.266 ± 63.772 | 12.820 ± 2.070 | 1.085 ± 0.011 | 85.773 ± 0.655 | 25.672 ± 0.268 |

## C-state residency (% of all CPU time)

| scenario | nonidle | C1E | C1 | C3 | C6 | POLL |
|---|---|---|---|---|---|---|
| none | 0.1 | 0.0 | 0.0 | 0.1 | 99.8 | 0.0 |
| hz1 | 0.1 | 0.0 | 0.0 | 0.1 | 99.8 | 0.0 |
| hz100 | 0.2 | 0.0 | 0.1 | 0.1 | 99.6 | 0.0 |
| hz20 | 0.1 | 0.0 | 0.0 | 0.1 | 99.7 | 0.0 |
| hz5 | 0.1 | 0.0 | 0.0 | 0.1 | 99.8 | 0.0 |

## Paired difference vs `none` (same trial)

`*` = 95% CI excludes 0. Observer-only metrics (`obs_*`, `idle_exit_obs_*`) count as 0 in the baseline.

| scenario | n | pkg_W | ram_W | deep_resid_pct | core_c6_pct | pkg_c2_pct | pkg_c6_pct | idle_entries_per_s | intr_per_s | ctxt_per_s | obs_vol_ctxt_per_s | obs_cpu_ms_per_s | ts_Pkg%pc6 | ts_PkgWatt |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| hz1 | 8 | 0.107 ± 0.348 | 0.029 ± 0.067 | -0.036 ± 0.139 | -0.095 ± 0.364 | -0.045 ± 0.498 | -0.164 ± 0.652 | 62.591 ± 231.709 | 56.435 ± 224.541 | 31.569 ± 93.409 | 2.872 ± 0.292* | 0.224 ± 0.007* | -0.165 ± 0.651 | 0.109 ± 0.345 |
| hz100 | 8 | 1.505 ± 0.310* | 0.327 ± 0.069* | -0.185 ± 0.113* | -0.421 ± 0.296* | 3.014 ± 0.604* | -7.204 ± 0.838* | 480.542 ± 188.910* | 133.228 ± 182.409 | 559.602 ± 91.490* | 274.827 ± 31.333* | 21.022 ± 0.174* | -7.200 ± 0.839* | 1.504 ± 0.310* |
| hz20 | 8 | 0.303 ± 0.307 | 0.050 ± 0.073 | -0.072 ± 0.135 | -0.170 ± 0.356 | 0.282 ± 0.555 | -1.203 ± 0.628* | 142.777 ± 221.900 | 71.164 ± 216.909 | 113.208 ± 87.781* | 53.989 ± 7.507* | 4.263 ± 0.050* | -1.200 ± 0.628* | 0.304 ± 0.305 |
| hz5 | 8 | 0.054 ± 0.340 | 0.028 ± 0.061 | -0.010 ± 0.116 | -0.013 ± 0.305 | 0.109 ± 0.365 | -0.268 ± 0.648 | 11.746 ± 194.672 | -2.811 ± 190.389 | 1.052 ± 73.053 | 12.820 ± 2.070* | 1.085 ± 0.011* | -0.266 ± 0.649 | 0.053 ± 0.340 |

## Dose-response (observer sample rate -> package power)

Rates in the fit: 0, 1, 5, 20, 100 Hz. Slope is the energy cost of one observer sample (timer wake plus the remote wakeups it causes).

| response | intercept | slope per Hz | 95% CI | R² | runs |
|---|---|---|---|---|---|
| pkg_W | 25.636 | 14.801 mW/Hz = mJ/sample | ± 2.349 | 0.80 | 40 |
| core_c6_pct | 99.580 | -0.004 %-points/Hz | ± 0.002 | 0.24 | 40 |
| idle_entries_per_s | 431.811 | 4.613 entries/s per Hz | ± 1.444 | 0.51 | 40 |

Check linearity before trusting the slope: compare fits with `--max-rate` set to the lower and upper half of the rates.

_Trace-pass metrics (`idle_exit_*`) come from a separate perturbing bpftrace window and are not comparable to power numbers._
