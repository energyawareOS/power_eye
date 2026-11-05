# Energy per tool-induced wakeup

Sources: results/realistic

Paired differences vs `none` of the same trial. dW = extra system-wide idle entries/s (includes IPI-woken CPUs); dP = extra package power.

## Per condition (mean ± 95% CI)

| source/scenario | rate Hz | n | dP mW | dW /s | dW per tick | mJ per entry (mean dP / mean dW) |
|---|---|---|---|---|---|---|
| realistic/ebpf_4 | 0.25 | 5 | -70 ± 183 | -67 ± 114 | -267.0 | - |
| realistic/fixed_4 | 0.25 | 5 | 121 ± 360 | 45 ± 179 | 178.7 | 2.71 |
| realistic/fixed_2 | 0.5 | 5 | -89 ± 214 | -67 ± 123 | -133.8 | - |
| realistic/ebpf_2 | 0.5 | 5 | 76 ± 257 | 9 ± 159 | 17.7 | - |
| realistic/fixed_1 | 1 | 5 | 84 ± 306 | 41 ± 189 | 40.7 | 2.05 |
| realistic/ebpf_1 | 1 | 5 | 14 ± 192 | -28 ± 102 | -28.0 | - |
| realistic/adaptive | 1 | 5 | -124 ± 157 | -80 ± 113 | -80.1 | - |

## Regression dP = a + b dW (runs above 1 Hz)

not enough points
