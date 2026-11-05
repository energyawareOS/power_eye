# Energy per tool-induced wakeup

Sources: results/dose-pkgc6, results/sysfs-vs-bpf:sys20,sys100

Paired differences vs `none` of the same trial. dW = extra system-wide idle entries/s (includes IPI-woken CPUs); dP = extra package power.

## Per condition (mean ± 95% CI)

| source/scenario | rate Hz | n | dP mW | dW /s | dW per tick | mJ per entry (mean dP / mean dW) |
|---|---|---|---|---|---|---|
| dose-pkgc6/hz1 | 1 | 8 | 107 ± 348 | 63 ± 232 | 62.6 | 1.72 |
| dose-pkgc6/hz5 | 5 | 8 | 54 ± 340 | 12 ± 195 | 2.3 | - |
| dose-pkgc6/hz20 | 20 | 8 | 303 ± 307 | 143 ± 222 | 7.1 | 2.12 |
| dose-pkgc6/hz100 | 100 | 8 | 1505 ± 310 | 481 ± 189 | 4.8 | 3.13 |
| sysfs-vs-bpf/sys20 | 20 | 6 | 741 ± 232 | 177 ± 36 | 8.9 | 4.18 |
| sysfs-vs-bpf/sys100 | 100 | 6 | 2057 ± 347 | 941 ± 262 | 9.4 | 2.19 |

## Regression dP = a + b dW (runs above 1 Hz)

- runs: 36, slope b = **2.00 mJ per extra idle entry** (95% CI ± 0.24), intercept 224 mW, R² 0.89
- through origin: **2.29 mJ per entry** (± 0.21)

## Prediction for low rates

Extra idle entries per sample (from the fitted runs) = 6.2; predicted dP = b0 x entries/sample x rate.

| -i | rate Hz | predicted dP mW | measured dP mW | n |
|---|---|---|---|---|
| 1 | 1 | 14 ± 1 | 107 ± 348 | 8 |
| 2 | 0.5 | 7 ± 1 | not measured | - |
| 4 | 0.25 | 4 ± 0 | not measured | - |

## Held-out runs (not in the fit)

| source/scenario | rate Hz | n | predicted dP mW | measured dP mW | measured dW /s |
|---|---|---|---|---|---|
| realistic/fixed_1 | 1 | 5 | 14 ± 1 | 84 ± 306 | 41 ± 189 |
| realistic/fixed_2 | 0.5 | 5 | 7 ± 1 | -89 ± 214 | -67 ± 123 |
| realistic/fixed_4 | 0.25 | 5 | 4 ± 0 | 121 ± 360 | 45 ± 179 |

The measured 1 Hz difference is compared with the prediction; if the CI of the measurement contains the prediction the model is consistent, but a CI wider than the predicted value means the test cannot discriminate (package power noise ~0.25 W).

## Reading the held-out result

- The fit is the earlier 5/20/100 Hz data (36 runs, through origin 2.29 mJ per extra idle entry, 6.2 entries per
  sample). The three realistic sysfs periods (`-i 1/2/4`, `results/realistic`, 5 runs each, 300 s windows,
  neighbouring `none` baselines) are not part of the fit.
- Predictions are 14, 7 and 4 mW; the measurements are +84, -89 and +121 mW with CIs of ±210-360 mW. Every CI
  contains its prediction, but also contains zero and values 20x larger: the test cannot confirm or refute the
  model. The prediction CIs only reflect the slope, not the 5-9 entries/sample spread, so they are too small.
- Package power noise: standard deviation of a window is 0.02-0.04 W in quiet conditions and 0.12-0.20 W in others
  (`fixed_1`, `fixed_4`, `ebpf_1`, `ebpf_2` vs `fixed_2`, `ebpf_4`, `adaptive`), which suggests background activity
  during some windows, not a tool effect.
- Detecting 14 mW against a paired-difference standard deviation of about 0.25 W would need roughly
  (1.96 x 0.25 / 0.014)^2 = 1200 pairs per condition.
