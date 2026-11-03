# Observer-effect summary

## Warnings

- 2 observer runs show zero wakeups and zero CPU: the observer may be blocked or an unrelated process is being tracked (see obs_comm in raw/*/meta)
- deep_resid_pct is 0 everywhere: no cpuidle state with exit latency >= 100 us had residency (C-states disabled, or lower --deep-latency-us); see the C-state residency table

Mean ± 95% CI over trials. Baseline scenario: `none`.

## Per scenario

| scenario | n | pkg_W | ram_W | deep_resid_pct | idle_entries_per_s | intr_per_s | ctxt_per_s | obs_vol_ctxt_per_s | obs_cpu_ms_per_s | ts_PkgWatt |
|---|---|---|---|---|---|---|---|---|---|---|
| none | 2 | 56.098 ± 0.017 | 11.001 ± 0.025 | 0.000 ± 0.000 | 343.912 ± 1933.752 | 312.587 ± 1785.589 | 417.605 ± 3705.489 | - | - | 56.200 ± 0.000 |
| node_exporter | 2 | 56.089 ± 1.023 | 10.997 ± 0.113 | 0.000 ± 0.000 | 353.661 ± 1990.763 | 323.602 ± 1862.030 | 424.298 ± 3796.669 | 0.000 ± 0.000 | 0.000 ± 0.000 | 56.200 ± 1.016 |
| pe1s | 2 | 56.100 ± 0.761 | 11.011 ± 0.058 | 0.000 ± 0.000 | 400.637 ± 2409.102 | 365.617 ± 2231.773 | 438.695 ± 3790.701 | 2.995 ± 0.002 | 0.000 ± 0.000 | 56.205 ± 0.826 |

## C-state residency (% of all CPU time)

| scenario | nonidle | C1E | C1 | C3 | C6 | POLL |
|---|---|---|---|---|---|---|
| none | -2.4 | 102.4 | 0.0 | 0.0 | 0.0 | 0.0 |
| node_exporter | -0.9 | 100.9 | 0.0 | 0.0 | 0.0 | 0.0 |
| pe1s | -2.8 | 102.8 | 0.0 | 0.0 | 0.0 | 0.0 |

## Paired difference vs `none` (same trial)

`*` = 95% CI excludes 0. Observer-only metrics (`obs_*`, `idle_exit_obs_*`) count as 0 in the baseline.

| scenario | n | pkg_W | ram_W | deep_resid_pct | idle_entries_per_s | intr_per_s | ctxt_per_s | obs_vol_ctxt_per_s | obs_cpu_ms_per_s | ts_PkgWatt |
|---|---|---|---|---|---|---|---|---|---|---|
| node_exporter | 2 | -0.008 ± 1.006 | -0.004 ± 0.138 | 0.000 ± 0.000 | 9.749 ± 3924.515 | 11.015 ± 3647.619 | 6.692 ± 7502.158 | 0.000 ± 0.000 | 0.000 ± 0.000 | -0.000 ± 1.016 |
| pe1s | 2 | 0.002 ± 0.778 | 0.010 ± 0.033 | 0.000 ± 0.000 | 56.724 ± 475.350 | 53.030 ± 446.184 | 21.090 ± 85.212 | 2.995 ± 0.002* | 0.000 ± 0.000 | 0.005 ± 0.826 |

_Trace-pass metrics (`idle_exit_*`) come from a separate perturbing bpftrace window and are not comparable to power numbers._
