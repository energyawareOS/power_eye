# Observer-effect summary

## Warnings

- trial 2 none: pkg_W 27.43 W is an outlier (median of none 25.67 W, idle entries 1384/s); other activity during the window? see raw/002_none

Mean ± 95% CI over trials. Baseline scenario: `none`.

## Per scenario

| scenario | n | pkg_W | ram_W | deep_resid_pct | core_c6_pct | pkg_c2_pct | pkg_c6_pct | idle_entries_per_s | intr_per_s | ctxt_per_s | obs_vol_ctxt_per_s | obs_cpu_ms_per_s | ts_Pkg%pc6 | ts_PkgWatt |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| none | 6 | 25.941 ± 0.778 | 3.792 ± 0.168 | 99.841 ± 0.224 | 99.450 ± 0.638 | 10.914 ± 0.204 | 85.190 ± 2.026 | 552.257 ± 428.175 | 538.305 ± 414.121 | 495.437 ± 158.495 | - | - | 85.125 ± 2.025 | 25.952 ± 0.780 |
| bpf100 | 6 | 25.578 ± 0.192 | 3.729 ± 0.046 | 99.925 ± 0.005 | 99.706 ± 0.028 | 10.854 ± 0.315 | 86.123 ± 0.572 | 373.458 ± 29.936 | 370.108 ± 25.829 | 419.842 ± 42.335 | 0.099 ± 0.000 | 0.013 ± 0.000 | 86.053 ± 0.570 | 25.590 ± 0.193 |
| bpf1000 | 6 | 25.578 ± 0.099 | 3.694 ± 0.036 | 99.926 ± 0.003 | 99.704 ± 0.014 | 10.764 ± 0.205 | 86.175 ± 0.349 | 372.514 ± 16.188 | 366.151 ± 16.577 | 435.177 ± 3.706 | 0.099 ± 0.000 | 0.013 ± 0.001 | 86.105 ± 0.352 | 25.590 ± 0.098 |
| hz100 | 6 | 26.816 ± 0.291 | 3.921 ± 0.139 | 99.771 ± 0.041 | 99.368 ± 0.108 | 13.263 ± 0.647 | 80.126 ± 1.426 | 755.461 ± 131.012 | 448.217 ± 72.121 | 848.859 ± 190.697 | 252.759 ± 41.670 | 20.966 ± 0.183 | 80.065 ± 1.427 | 26.828 ± 0.292 |

## C-state residency (% of all CPU time)

| scenario | nonidle | C1E | C1 | C3 | C6 | POLL |
|---|---|---|---|---|---|---|
| none | 0.2 | 0.0 | 0.0 | 0.1 | 99.7 | 0.0 |
| bpf100 | 0.1 | 0.0 | 0.0 | 0.1 | 99.8 | 0.0 |
| bpf1000 | 0.1 | 0.0 | 0.0 | 0.1 | 99.8 | 0.0 |
| hz100 | 0.2 | 0.0 | 0.0 | 0.1 | 99.7 | 0.0 |

## Paired difference vs `none` (same trial)

`*` = 95% CI excludes 0. Observer-only metrics (`obs_*`, `idle_exit_obs_*`) count as 0 in the baseline.

| scenario | n | pkg_W | ram_W | deep_resid_pct | core_c6_pct | pkg_c2_pct | pkg_c6_pct | idle_entries_per_s | intr_per_s | ctxt_per_s | obs_vol_ctxt_per_s | obs_cpu_ms_per_s | ts_Pkg%pc6 | ts_PkgWatt |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| bpf100 | 6 | -0.363 ± 0.912 | -0.063 ± 0.167 | 0.084 ± 0.225 | 0.256 ± 0.646 | -0.060 ± 0.487 | 0.933 ± 2.376 | -178.798 ± 439.855 | -168.197 ± 426.799 | -75.595 ± 158.965 | 0.099 ± 0.000* | 0.013 ± 0.000* | 0.928 ± 2.377 | -0.362 ± 0.915 |
| bpf1000 | 6 | -0.363 ± 0.828 | -0.097 ± 0.155 | 0.085 ± 0.224 | 0.255 ± 0.642 | -0.150 ± 0.386 | 0.985 ± 2.183 | -179.743 ± 434.492 | -172.154 ± 421.021 | -60.260 ± 158.996 | 0.099 ± 0.000* | 0.013 ± 0.001* | 0.980 ± 2.183 | -0.362 ± 0.829 |
| hz100 | 6 | 0.876 ± 0.999 | 0.129 ± 0.266 | -0.070 ± 0.255 | -0.082 ± 0.726 | 2.350 ± 0.643* | -5.064 ± 3.075* | 203.204 ± 537.851 | -90.089 ± 462.566 | 353.422 ± 331.824* | 252.759 ± 41.670* | 20.966 ± 0.183* | -5.060 ± 3.076* | 0.877 ± 1.000 |

## Dose-response (observer sample rate -> package power)

Rates in the fit: 0, 0.1, 100 Hz. Slope is the energy cost of one observer sample (timer wake plus the remote wakeups it causes).

| response | intercept | slope per Hz | 95% CI | R² | runs |
|---|---|---|---|---|---|
| pkg_W | 25.698 | 11.180 mW/Hz = mJ/sample | ± 4.107 | 0.59 | 24 |
| core_c6_pct | 99.620 | -0.003 %-points/Hz | ± 0.003 (n.s.) | 0.12 | 24 |
| idle_entries_per_s | 432.568 | 3.228 entries/s per Hz | ± 2.131 | 0.31 | 24 |

Check linearity before trusting the slope: compare fits with `--max-rate` set to the lower and upper half of the rates.

_Trace-pass metrics (`idle_exit_*`) come from a separate perturbing bpftrace window and are not comparable to power numbers._
