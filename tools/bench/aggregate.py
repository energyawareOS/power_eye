#!/usr/bin/env python3
"""Aggregate raw output of bench.sh into runs.csv and summary.md.

Per run it derives package power, C-state residency, idle-entry rate,
interrupt/context-switch rates and the observer's own wakeups from
start/end snapshots. Per scenario it reports mean and 95% CI, and paired
differences against the baseline scenario ("none") within the same trial.
"""
import argparse
import csv
import glob
import math
import os
import re
import statistics
import sys

# two-sided 95% Student t, df = 1..30
T95 = [12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228,
       2.201, 2.179, 2.160, 2.145, 2.131, 2.120, 2.110, 2.101, 2.093, 2.086,
       2.080, 2.074, 2.069, 2.064, 2.060, 2.056, 2.052, 2.048, 2.045, 2.042]

KEY_METRICS = [
    "pkg_W", "ram_W", "deep_resid_pct", "core_c6_pct", "pkg_c2_pct", "pkg_c6_pct", "idle_entries_per_s", "intr_per_s",
    "ctxt_per_s", "obs_vol_ctxt_per_s", "obs_cpu_ms_per_s",
    "idle_exit_total_per_s", "idle_exit_obs_per_s", "ts_Pkg%pc6", "ts_PkgWatt",
    "ipmi_avg_W",
]


TSC_MHZ_FALLBACK = [None]  # set from --tsc-mhz when turbostat output is unavailable


def read_kv(path):
    d = {}
    with open(path) as f:
        for line in f:
            if "=" in line:
                k, v = line.rstrip("\n").split("=", 1)
                d[k] = v
    return d


def read_idle(path):
    rows = {}
    with open(path) as f:
        for line in f:
            p = line.rstrip("\n").split("\t")
            if len(p) < 6:
                continue
            rows[(int(p[0]), int(p[1]))] = (p[2], int(p[3]), int(p[4]), int(p[5]))
    return rows


def idle_metrics(pre, post, window, deep_lat_us):
    ncpu = len({c for c, _ in pre})
    out, entries, deep = {}, 0, 0.0
    total_idle = 0.0
    per_name = {}
    for key, (name, u0, t0, lat) in pre.items():
        if key not in post:
            continue
        _, u1, t1, _ = post[key]
        du, dt = u1 - u0, (t1 - t0) / 1e6
        entries += du
        per_name[name] = per_name.get(name, 0.0) + dt
        total_idle += dt
        if lat >= deep_lat_us:
            deep += dt
    denom = ncpu * window / 100.0
    for name, t in per_name.items():
        out["resid_%s_pct" % name] = t / denom
    out["deep_resid_pct"] = deep / denom
    out["nonidle_pct"] = 100.0 - total_idle / denom
    out["idle_entries_per_s"] = entries / window
    out["ncpu"] = ncpu
    return out


def read_pairs(path):
    d = {}
    with open(path) as f:
        for line in f:
            p = line.split()
            if len(p) == 2:
                try:
                    d[p[0]] = float(p[1])
                except ValueError:
                    pass
    return d


def read_times(path):
    """times: 'name start end' per snapshot -> {name: midpoint}."""
    d = {}
    if os.path.exists(path):
        with open(path) as f:
            for line in f:
                p = line.split()
                if len(p) == 3:
                    d[p[0]] = (float(p[1]) + float(p[2])) / 2.0
    return d


def read_obs(path):
    """pre/post.obs: 'pid vol nonvol utime stime [runtime_ns]' per process."""
    d = {}
    if os.path.exists(path):
        with open(path) as f:
            for line in f:
                p = line.split()
                if len(p) in (5, 6):
                    try:
                        d[int(p[0])] = tuple(int(x) for x in p[1:]) + (0,) * (6 - len(p))
                    except ValueError:
                        pass
    return d


def parse_perf(path):
    """perf stat -x, output: value,unit,event,runtime,pct,..."""
    out = {}
    if not os.path.exists(path):
        return out
    with open(path) as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            p = line.rstrip("\n").split(",")
            if len(p) < 3:
                continue
            try:
                val = float(p[0])
            except ValueError:
                continue  # <not supported> / <not counted>
            ev = p[2]
            if ev.startswith("power/energy-") and ev.endswith("/"):
                out[ev[len("power/energy-"):-1] + "_J"] = val
            elif ev.startswith(("cstate_pkg/", "cstate_core/")) and ev.endswith("-residency/"):
                dev, name = ev.rstrip("/").split("/")
                out[("pkg_" if dev == "cstate_pkg" else "core_") + name.split("-")[0] + "_ticks"] = val
    return out


def parse_turbostat(path):
    """Summary row of turbostat (tab/space separated header + '-' row)."""
    out = {}
    if not os.path.exists(path):
        return out
    with open(path) as f:
        lines = [l.rstrip("\n") for l in f if l.strip()]
    for i, line in enumerate(lines):
        cols = line.split()
        if "Avg_MHz" in cols or "Busy%" in cols:
            for row in lines[i + 1:]:
                vals = row.split()
                if len(vals) == len(cols) and (vals[0] == "-" or len(lines) == i + 2):
                    for c, v in zip(cols, vals):
                        try:
                            out["ts_" + c] = float(v)
                        except ValueError:
                            pass
                    return out
            break
    return out


def parse_trace(path):
    out = {}
    if not os.path.exists(path):
        return out
    with open(path) as f:
        for line in f:
            if line.startswith("@") and ":" in line:
                k, v = line.split(":", 1)
                try:
                    out[k[1:].strip()] = float(v.strip())
                except ValueError:
                    pass
    return out


def parse_ipmi(path):
    if not os.path.exists(path):
        return None
    with open(path) as f:
        for line in f:
            if "Average power reading" in line and ":" in line:
                try:
                    return float(line.split(":", 1)[1].split()[0])
                except (ValueError, IndexError):
                    return None
    return None


def sample_rate_hz(meta):
    """Observer sample rate from its '-i <seconds>' option; 0 for the idle baseline."""
    cmd = meta.get("cmd", "")
    if not cmd.strip():
        return 0.0
    m = re.search(r"(?:^|\s)-i\s*=?\s*([0-9]*\.?[0-9]+)", cmd)
    if m and float(m.group(1)) > 0:
        return 1.0 / float(m.group(1))
    return None


def ols(xs, ys):
    """Least squares y = a + b x. Returns (a, b, ci95 of b, r2, n) or None."""
    n = len(xs)
    if n < 3:
        return None
    mx, my = statistics.fmean(xs), statistics.fmean(ys)
    sxx = sum((x - mx) ** 2 for x in xs)
    if sxx == 0:
        return None
    b = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx
    a = my - b * mx
    sse = sum((y - a - b * x) ** 2 for x, y in zip(xs, ys))
    sst = sum((y - my) ** 2 for y in ys)
    df = n - 2
    se = math.sqrt(sse / df / sxx) if df > 0 else float("nan")
    t = T95[df - 1] if 1 <= df <= len(T95) else 1.96
    return a, b, t * se, (1 - sse / sst) if sst else float("nan"), n


def run_metrics(d, deep_lat_us):
    meta = read_kv(os.path.join(d, "meta"))
    outer = float(meta["t1"]) - float(meta["t0"])
    dur = float(meta.get("dur", outer))
    tm = read_times(os.path.join(d, "times"))

    def span(kind):  # interval between this kind's own pre/post snapshots
        if "pre." + kind in tm and "post." + kind in tm:
            return tm["post." + kind] - tm["pre." + kind]
        return outer

    window = span("idle")
    row = {"scenario": meta["scenario"], "trial": int(meta["trial"]),
           "pos": int(meta.get("pos", 0)), "window_s": window,
           "obs_comm": meta.get("obs_comm", ""), "rate_hz": sample_rate_hz(meta)}
    row.update(idle_metrics(read_idle(os.path.join(d, "pre.idle")),
                            read_idle(os.path.join(d, "post.idle")),
                            window, deep_lat_us))
    s0, s1 = read_pairs(os.path.join(d, "pre.stat")), read_pairs(os.path.join(d, "post.stat"))
    if "intr" in s0 and "intr" in s1:
        ws = span("stat")
        row["intr_per_s"] = (s1["intr"] - s0["intr"]) / ws
        row["ctxt_per_s"] = (s1["ctxt"] - s0["ctxt"]) / ws
    o0, o1 = read_obs(os.path.join(d, "pre.obs")), read_obs(os.path.join(d, "post.obs"))
    common = set(o0) & set(o1)  # processes alive at both ends
    if common:
        hz = os.sysconf("SC_CLK_TCK")
        wo = span("obs")
        dv = [sum(o1[p][i] - o0[p][i] for p in common) for i in range(5)]
        row["obs_nproc"] = len(common)
        row["obs_vol_ctxt_per_s"] = dv[0] / wo
        row["obs_nonvol_ctxt_per_s"] = dv[1] / wo
        if any(o1[p][4] for p in common):  # schedstat run time, ns resolution
            row["obs_cpu_ms_per_s"] = dv[4] / 1e6 / wo
        else:                              # fallback: clock ticks (10 ms resolution)
            row["obs_cpu_ms_per_s"] = (dv[2] + dv[3]) / hz * 1000.0 / wo
    elif meta.get("obs_pid"):
        row["obs_lost"] = 1  # observer configured but not alive at both ends
    for k, v in parse_perf(os.path.join(d, "perf.csv")).items():
        row[k] = v
        row[k[:-2] + "_W"] = v / dur  # perf counted exactly while 'sleep dur' ran
    row.update(parse_turbostat(os.path.join(d, "turbostat.txt")))
    # Hardware residency counters count TSC ticks per PMU instance (package / core).
    tsc_hz = (row.get("ts_TSC_MHz") or TSC_MHZ_FALLBACK[0] or 0) * 1e6
    for k in [k for k in row if k.endswith("_ticks")]:
        n = int(meta.get("n_pkg" if k.startswith("pkg_") else "n_core", 0))
        if tsc_hz and n:
            row[k[:-6] + "_pct"] = row[k] / (tsc_hz * dur * n) * 100.0
    tr, tdur = parse_trace(os.path.join(d, "trace.txt")), float(meta.get("trace_dur", 0))
    if tdur > 0 and tr:
        row["idle_exit_total_per_s"] = tr.get("idle_exit_total", 0.0) / tdur
        row["idle_exit_obs_per_s"] = tr.get("idle_exit_obs", 0.0) / tdur
    ip = parse_ipmi(os.path.join(d, "post.ipmi"))
    if ip is not None:
        row["ipmi_avg_W"] = ip
    return row


def mean_ci(xs):
    n = len(xs)
    if n == 0:
        return None, None, 0
    m = statistics.fmean(xs)
    if n < 2:
        return m, None, n
    sd = statistics.stdev(xs)
    t = T95[n - 2] if n - 1 <= len(T95) else 1.96
    return m, t * sd / math.sqrt(n), n


def fmt(m, ci, digits=3):
    if m is None:
        return "-"
    s = "%.*f" % (digits, m)
    return s + (" ± %.*f" % (digits, ci) if ci is not None else "")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("outdir")
    ap.add_argument("--baseline", default="none")
    ap.add_argument("--deep-latency-us", type=float, default=30.0,
                    help="cpuidle states with exit latency >= this count as deep (default 30: "
                         "C3 40us / C6 133us on Xeon E5 v4, C2 120us on ACPI idle; C1E 10us is shallow)")
    ap.add_argument("--max-rate", type=float, default=None,
                    help="dose-response fit uses only runs with sample rate <= this (Hz); "
                         "at high rates deep C-states are lost and power saturates")
    ap.add_argument("--tsc-mhz", type=float, default=None,
                    help="TSC frequency for hardware C-state %% when turbostat output is missing")
    args = ap.parse_args()
    TSC_MHZ_FALLBACK[0] = args.tsc_mhz

    dirs = sorted(glob.glob(os.path.join(args.outdir, "raw", "*", "meta")))
    if not dirs:
        sys.exit("no runs found under %s/raw" % args.outdir)
    rows = [run_metrics(os.path.dirname(m), args.deep_latency_us) for m in dirs]

    cols = ["scenario", "trial", "pos", "window_s", "obs_comm"]
    cols += sorted({k for r in rows for k in r} - set(cols))
    with open(os.path.join(args.outdir, "runs.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols)
        w.writeheader()
        w.writerows(rows)

    warnings = []
    envp = os.path.join(args.outdir, "env.txt")
    if os.path.exists(envp):
        with open(envp) as f:
            off = [l.split()[1][5:] for l in f if l.startswith("state") and " disable=1" in l]
        if off:
            warnings.append("cpuidle states disabled by software on cpu0 (see env.txt): %s -- "
                            "deep residency cannot be observed in this configuration" % ", ".join(off))
    n = len(rows)
    # Outlier runs: external activity during a window ruins a pairing (n is small).
    pw = sorted(r["pkg_W"] for r in rows if "pkg_W" in r)
    if len(pw) >= 5:
        med = pw[len(pw) // 2] if len(pw) % 2 else (pw[len(pw) // 2 - 1] + pw[len(pw) // 2]) / 2
        mad = sorted(abs(x - med) for x in pw)[len(pw) // 2]
        lim = 3 * max(1.4826 * mad, 0.1)
        for r in rows:
            if "pkg_W" in r and abs(r["pkg_W"] - med) > lim:
                warnings.append("trial %d %s: pkg_W %.2f W is an outlier (median %.2f W, idle entries "
                                "%.0f/s); other activity during the window? see raw/%03d_%s"
                                % (r["trial"], r["scenario"], r["pkg_W"], med,
                                   r.get("idle_entries_per_s", float("nan")), r["trial"], r["scenario"]))
    for key, what in (("pkg_J", "RAPL energy (perf.csv missing or unparsable; see raw/*/window.log)"),
                      ("ts_Busy%", "turbostat summary (turbostat.txt missing or unparsable)")):
        miss = sum(1 for r in rows if key not in r)
        if miss:
            warnings.append("%d/%d runs have no %s" % (miss, n, what))
    lost = [r for r in rows if r.get("obs_lost")]
    if lost:
        warnings.append("%d runs: observer not alive at both ends of the window" % len(lost))
    idle_obs = [r for r in rows if r["scenario"] != args.baseline and "obs_nproc" in r
                and r["obs_vol_ctxt_per_s"] == 0 and r["obs_cpu_ms_per_s"] == 0]
    if idle_obs:
        warnings.append("%d observer runs show zero wakeups and zero CPU: the observer may be blocked "
                        "or an unrelated process is being tracked (see obs_comm in raw/*/meta)" % len(idle_obs))
    if all(r.get("deep_resid_pct", 0) == 0 for r in rows):
        warnings.append("deep_resid_pct is 0 everywhere: no cpuidle state with exit latency >= %g us "
                        "had residency (C-states disabled, or lower --deep-latency-us); "
                        "see the C-state residency table" % args.deep_latency_us)

    scen = sorted({r["scenario"] for r in rows}, key=lambda s: (s != args.baseline, s))
    metrics = [m for m in KEY_METRICS if any(m in r for r in rows)]
    lines = ["# Observer-effect summary", ""]
    if warnings:
        lines += ["## Warnings", ""] + ["- " + w for w in warnings] + [""]
    lines += [
             "Mean ± 95%% CI over trials. Baseline scenario: `%s`." % args.baseline, ""]
    lines += ["## Per scenario", "", "| scenario | n | " + " | ".join(metrics) + " |",
              "|---|---|" + "---|" * len(metrics)]
    for s in scen:
        sr = [r for r in rows if r["scenario"] == s]
        cells = []
        for m in metrics:
            mm, ci, _ = mean_ci([r[m] for r in sr if m in r])
            cells.append(fmt(mm, ci))
        lines.append("| %s | %d | %s |" % (s, len(sr), " | ".join(cells)))

    states = sorted({k for r in rows for k in r if k.startswith("resid_")})
    if states:
        lines += ["", "## C-state residency (% of all CPU time)", "",
                  "| scenario | nonidle | " + " | ".join(k[6:-4] for k in states) + " |",
                  "|---|---|" + "---|" * len(states)]
        for s in scen:
            sr = [r for r in rows if r["scenario"] == s]
            cells = [fmt(mean_ci([r["nonidle_pct"] for r in sr])[0], None, 1)]
            cells += [fmt(mean_ci([r[k] for r in sr if k in r])[0], None, 1) for k in states]
            lines.append("| %s | %s |" % (s, " | ".join(cells)))

    base = {r["trial"]: r for r in rows if r["scenario"] == args.baseline}
    if base:
        lines += ["", "## Paired difference vs `%s` (same trial)" % args.baseline, "",
                  "`*` = 95% CI excludes 0. Observer-only metrics (`obs_*`, `idle_exit_obs_*`) "
                  "count as 0 in the baseline.", "",
                  "| scenario | n | " + " | ".join(metrics) + " |",
                  "|---|---|" + "---|" * len(metrics)]
        for s in scen:
            if s == args.baseline:
                continue
            sr = [r for r in rows if r["scenario"] == s and r["trial"] in base]
            cells = []
            for m in metrics:
                zero = 0.0 if m.startswith(("obs_", "idle_exit_obs")) else None
                diffs = [r[m] - base[r["trial"]].get(m, zero) for r in sr
                         if m in r and base[r["trial"]].get(m, zero) is not None]
                mm, ci, _ = mean_ci(diffs)
                star = "*" if mm is not None and ci is not None and abs(mm) > ci else ""
                cells.append(fmt(mm, ci) + star)
            lines.append("| %s | %d | %s |" % (s, len(sr), " | ".join(cells)))
    # Dose-response: package power against the observer's sample rate (slope = J per sample).
    fit_rows = [r for r in rows if r.get("rate_hz") is not None and "pkg_W" in r
                and (args.max_rate is None or r["rate_hz"] <= args.max_rate)]
    rates = sorted({r["rate_hz"] for r in fit_rows})
    if len(rates) >= 3:
        lines += ["", "## Dose-response (observer sample rate -> package power)", "",
                  "Rates in the fit: %s Hz%s. Slope is the energy cost of one observer sample "
                  "(timer wake plus the remote wakeups it causes)." %
                  (", ".join("%g" % x for x in rates),
                   "" if args.max_rate is None else " (limited by --max-rate %g)" % args.max_rate),
                  "", "| response | intercept | slope per Hz | 95% CI | R² | runs |", "|---|---|---|---|---|---|"]
        for key, scale, unit in (("pkg_W", 1000.0, "mW/Hz = mJ/sample"),
                                 ("core_c6_pct", 1.0, "%-points/Hz"),
                                 ("idle_entries_per_s", 1.0, "entries/s per Hz")):
            pts = [(r["rate_hz"], r[key]) for r in fit_rows if key in r]
            fit = ols([p[0] for p in pts], [p[1] for p in pts]) if pts else None
            if fit:
                a, b, ci, r2, n = fit
                lines.append("| %s | %.3f | %.3f %s | ± %.3f%s | %s | %d |" %
                             (key, a, b * scale, unit, ci * scale,
                              "" if abs(b) > ci else " (n.s.)",
                              "-" if r2 != r2 else "%.2f" % r2, n))
        lines += ["", "Check linearity before trusting the slope: compare fits with `--max-rate` "
                  "set to the lower and upper half of the rates."]
    lines += ["", "_Trace-pass metrics (`idle_exit_*`) come from a separate perturbing "
              "bpftrace window and are not comparable to power numbers._", ""]
    with open(os.path.join(args.outdir, "summary.md"), "w") as f:
        f.write("\n".join(lines))
    print("\n".join(lines))


if __name__ == "__main__":
    main()
