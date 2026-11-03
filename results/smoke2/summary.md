# Observer-effect summary

Mean ± 95% CI over trials. Baseline scenario: `none`.

## Per scenario

| scenario | n | pkg_W | ram_W | deep_resid_pct | core_c6_pct | pkg_c2_pct | pkg_c6_pct | idle_entries_per_s | intr_per_s | ctxt_per_s | obs_vol_ctxt_per_s | obs_cpu_ms_per_s | ts_PkgWatt |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| none | 2 | 44.916 ± 1.069 | 11.023 ± 0.100 | 99.918 ± 0.231 | 99.602 ± 2.152 | 0.000 ± 0.000 | 0.000 ± 0.000 | 432.259 ± 2285.334 | 432.552 ± 2210.696 | 456.600 ± 3560.924 | - | - | 44.915 ± 1.080 |
| node_exporter | 2 | 44.800 ± 1.470 | 11.005 ± 0.121 | 99.895 ± 0.042 | 99.476 ± 0.080 | 0.000 ± 0.000 | 0.000 ± 0.000 | 583.183 ± 98.271 | 571.014 ± 101.891 | 750.800 ± 29.937 | 2.931 ± 0.833 | 0.205 ± 0.037 | 44.805 ± 1.461 |
| pe1s | 2 | 44.899 ± 1.065 | 11.007 ± 0.208 | 99.922 ± 0.268 | 99.640 ± 1.746 | 0.000 ± 0.000 | 0.000 ± 0.000 | 404.690 ± 2030.150 | 399.129 ± 1890.536 | 456.638 ± 3674.948 | 1.993 ± 0.208 | 0.181 ± 0.055 | 44.905 ± 1.080 |

## C-state residency (% of all CPU time)

| scenario | nonidle | C1E | C1 | C3 | C6 | POLL |
|---|---|---|---|---|---|---|
| none | 0.1 | 0.0 | 0.0 | 0.1 | 99.8 | 0.0 |
| node_exporter | 0.1 | 0.0 | 0.0 | 0.2 | 99.7 | 0.0 |
| pe1s | 0.1 | 0.0 | 0.0 | 0.1 | 99.8 | 0.0 |

## Paired difference vs `none` (same trial)

`*` = 95% CI excludes 0. Observer-only metrics (`obs_*`, `idle_exit_obs_*`) count as 0 in the baseline.

| scenario | n | pkg_W | ram_W | deep_resid_pct | core_c6_pct | pkg_c2_pct | pkg_c6_pct | idle_entries_per_s | intr_per_s | ctxt_per_s | obs_vol_ctxt_per_s | obs_cpu_ms_per_s | ts_PkgWatt |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| node_exporter | 2 | -0.116 ± 2.539 | -0.018 ± 0.021 | -0.024 ± 0.189 | -0.126 ± 2.232 | 0.000 ± 0.000 | 0.000 ± 0.000 | 150.923 ± 2383.605 | 138.462 ± 2312.587 | 294.201 ± 3530.987 | 2.931 ± 0.833* | 0.205 ± 0.037* | -0.110 ± 2.541 |
| pe1s | 2 | -0.017 ± 2.135 | -0.016 ± 0.108 | 0.003 ± 0.499 | 0.038 ± 3.897 | 0.000 ± 0.000 | 0.000 ± 0.000 | -27.569 ± 4315.484 | -33.423 ± 4101.232 | 0.038 ± 7235.872 | 1.993 ± 0.208* | 0.181 ± 0.055* | -0.010 ± 2.160 |

_Trace-pass metrics (`idle_exit_*`) come from a separate perturbing bpftrace window and are not comparable to power numbers._
