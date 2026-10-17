# Long coma run (server `power`, 2026-11-25)

Command:

    sudo build/src/power_eye -B -d -i 1 -A 600 -C 0.5 -F 25.0 -t 5400 -f cEHo

Raw output: `coma-long-power.txt`. Idle floor was given with `-F 25.0`.

## Detection

| window end | mean W | deep % | verdict |
|---|---|---|---|
| 17:45:30 | 25.50 | 86.6 | idle |
| 17:55:36 | 26.98 | 81.9 | busy (other job running) |
| 18:05:40 | 25.63 | 86.3 | idle (10.1 min) |
| 18:15:46 | 25.47 | 86.6 | idle (20.2 min) |
| 18:25:49 | 25.77 | 86.0 | idle (30.2 min) -> COMA CANDIDATE since 17:55:36 |
| 18:55:51 | 25.88 | 86.1 | idle (1.00 h) |

The candidate is reported at the first window end after 30 min of idle time.
Window labels match the real boundaries; no tiny window-end ticks occurred.

## Observer cost

OBS-WAKE falls from 1.00/s (INT 1 s) to 0.00/s once INT is above ~100 s,
OBS-CPU from ~0.3 to ~0.001 ms/s, OBS-RAPLRD is 0 except right after a reset
(one forced sample: WAKE 3.00, RAPLRD 2.00).

## Limitation seen

INT reached 600 s only at 18:55:54. Before that, small (1-1.5 W) activity
every 1-1.5 min exceeded the 3% (~0.75 W) band and reset INT to 1 s, so INT
stayed at 32-64 s in 18:05-18:25. Try `-T 2` to widen the band (not yet run).
