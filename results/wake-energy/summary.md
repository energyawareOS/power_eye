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

The measured 1 Hz difference is compared with the prediction; if the CI of the measurement contains the prediction the model is consistent, but a CI wider than the predicted value means the test cannot discriminate (package power noise ~0.25 W).

## Reading the result

- Extra idle entries per sample are 5-9 (own timer wake plus the IPI-woken package lead CPUs, plus the wakeups
  those cause), clearly more than the ~3 a per-process counter shows (`obs_vol_ctxt_per_s`, which only counts
  the tool blocking on the remote read). A system-wide counter difference is the only measure here that
  includes the IPI-woken CPUs.
- 2.0-2.3 mJ per extra idle entry, times 5-6 entries per sample, gives about 12-15 mJ per sample, consistent
  with the ~15 mJ per sample from the earlier dose-response regression.
- The positive intercept (150-220 mW) means the unconstrained fit is partly absorbing drift between the `none`
  run and the observer run (single `none` per trial in these data). The through-origin value is therefore the
  one used for the prediction; the new interleaved design measures that drift directly.
- Validation is weak: at 1 Hz the predicted cost is about 12-14 mW but the measured paired difference is
  107 ± 348 mW (n=8), so it cannot confirm or refute the prediction. It only shows nothing larger than ~0.45 W.
  The new design (10 trials, 10-minute windows, neighbouring baselines) narrows the CI by averaging over
  600 s and over two baselines.
- `dose-pkgc6` has no `-d` (package only); `sysfs-vs-bpf` has `-d` (DRAM reads add remote reads). Pooling them
  mixes a different number of reads per sample; the per-source rows above show the spread (2.2-4.2 mJ per entry).
- -B at 20/100 Hz forces samples (stale limit), so it is not used here.
