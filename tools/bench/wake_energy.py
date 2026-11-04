#!/usr/bin/env python3
"""Energy per tool-induced wakeup from existing bench runs.

    wake_energy.py [--exclude-rate HZ] [--validate-rate HZ] DIR[:SCEN,SCEN,...] ...

For every observer run the paired differences against the `none` run of the
same trial are formed:  dP (mW) = pkg_W - pkg_W(none),  dW (entries/s) =
idle_entries_per_s - idle_entries_per_s(none).  dW is the system-wide extra
idle entries, so it includes CPUs woken by IPI (remote RAPL reads), which a
per-process counter cannot see.  Regressing dP on dW gives mJ per extra idle
entry.  The fit uses the runs with sample rate > --validate-rate (default 1 Hz
is held out); the held-out rate is then predicted and compared with its
measurement.  Output: markdown on stdout.
"""
import collections, csv, math, os, statistics, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from aggregate import ols, mean_ci, T95


def load(spec):
    d, _, sc = spec.partition(":")
    want = set(sc.split(",")) if sc else None
    rows = list(csv.DictReader(open(os.path.join(d, "runs.csv"))))
    by = collections.defaultdict(dict)
    for r in rows:
        by[r["scenario"]][int(r["trial"])] = r
    out = []
    for s, runs in by.items():
        if s == "none" or (want and s not in want):
            continue
        for t, r in runs.items():
            b = by["none"].get(t)
            if not b or not r["rate_hz"]:
                continue
            f = lambda k: float(r[k]) - float(b[k])
            out.append(dict(src=d, scen=s, trial=t, rate=float(r["rate_hz"]),
                            dP=1000 * f("pkg_W"), dW=f("idle_entries_per_s"),
                            dR=1000 * f("ram_W")))
    return out


def main():
    args = sys.argv[1:]
    hold = 1.0
    if args[:1] == ["--validate-rate"]:
        hold = float(args[1]); args = args[2:]
    if not args:
        sys.exit(__doc__)
    pts = [p for a in args for p in load(a)]
    fit = [p for p in pts if p["rate"] > hold]
    test = [p for p in pts if p["rate"] == hold]
    L = ["# Energy per tool-induced wakeup", "",
         "Sources: " + ", ".join(args), "",
         "Paired differences vs `none` of the same trial. dW = extra system-wide idle "
         "entries/s (includes IPI-woken CPUs); dP = extra package power.", "",
         "## Per condition (mean ± 95% CI)", "",
         "| source/scenario | rate Hz | n | dP mW | dW /s | dW per tick | mJ per entry (mean dP / mean dW) |",
         "|---|---|---|---|---|---|---|"]
    g = collections.defaultdict(list)
    for p in pts:
        g[(p["src"], p["scen"], p["rate"])].append(p)
    for (src, sc, rate), v in sorted(g.items(), key=lambda kv: (kv[0][0], kv[0][2])):
        mp, cp, n = mean_ci([p["dP"] for p in v])
        mw, cw, _ = mean_ci([p["dW"] for p in v])
        L.append("| %s/%s | %g | %d | %.0f ± %.0f | %.0f ± %.0f | %.1f | %s |" % (
            os.path.basename(src), sc, rate, n, mp, cp or 0, mw, cw or 0, mw / rate,
            "%.2f" % (mp / mw) if mw > 20 else "-"))
    L += ["", "## Regression dP = a + b dW (runs above %g Hz)" % hold, ""]
    res = ols([p["dW"] for p in fit], [p["dP"] for p in fit])
    if not res:
        L.append("not enough points"); print("\n".join(L)); return
    a, b, ci, r2, n = res
    L += ["- runs: %d, slope b = **%.2f mJ per extra idle entry** (95%% CI ± %.2f), "
          "intercept %.0f mW, R² %.2f" % (n, b, ci, a, r2)]
    # through-origin fit (no wakeups -> no cost)
    sxy = sum(p["dW"] * p["dP"] for p in fit); sxx = sum(p["dW"] ** 2 for p in fit)
    b0 = sxy / sxx
    sse = sum((p["dP"] - b0 * p["dW"]) ** 2 for p in fit)
    se0 = math.sqrt(sse / (n - 1) / sxx)
    t = T95[n - 2] if n - 1 <= len(T95) else 1.96
    L += ["- through origin: **%.2f mJ per entry** (± %.2f)" % (b0, t * se0), ""]
    # wakes per tick from the fit rates (dW/rate), then prediction at low rates
    wpt = statistics.fmean([p["dW"] / p["rate"] for p in fit])
    L += ["## Prediction for low rates", "",
          "Extra idle entries per sample (from the fitted runs) = %.1f; "
          "predicted dP = b0 x entries/sample x rate." % wpt, "",
          "| -i | rate Hz | predicted dP mW | measured dP mW | n |", "|---|---|---|---|---|"]
    for iv in (1, 2, 4):
        rate = 1.0 / iv
        pred = b0 * wpt * rate
        if rate == hold and test:
            mp, cp, nn = mean_ci([p["dP"] for p in test])
            meas = "%.0f ± %.0f" % (mp, cp or 0); nn = str(nn)
        else:
            meas, nn = "not measured", "-"
        L.append("| %d | %g | %.0f ± %.0f | %s | %s |" % (
            iv, rate, pred, t * se0 * wpt * rate, meas, nn))
    L += ["", "The measured 1 Hz difference is compared with the prediction; if the CI of the "
          "measurement contains the prediction the model is consistent, but a CI wider than the "
          "predicted value means the test cannot discriminate (package power noise ~0.25 W)."]
    print("\n".join(L))


if __name__ == "__main__":
    main()
