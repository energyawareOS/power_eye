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
import statistics
import sys

# two-sided 95% Student t, df = 1..30
T95 = [12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228,
       2.201, 2.179, 2.160, 2.145, 2.131, 2.120, 2.110, 2.101, 2.093, 2.086,
       2.080, 2.074, 2.069, 2.064, 2.060, 2.056, 2.052, 2.048, 2.045, 2.042]

KEY_METRICS = [
    "pkg_W", "ram_W", "deep_resid_pct", "idle_entries_per_s", "intr_per_s",
    "ctxt_per_s", "obs_vol_ctxt_per_s", "obs_cpu_ms_per_s",
    "idle_exit_total_per_s", "idle_exit_obs_per_s", "ts_Pkg%pc6", "ts_PkgWatt",
    "ipmi_avg_W",
]


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


def read_obs(path):
    """pre/post.obs: 'pid vol nonvol utime stime' per process."""
    d = {}
    if os.path.exists(path):
        with open(path) as f:
            for line in f:
                p = line.split()
                if len(p) == 5:
                    try:
                        d[int(p[0])] = tuple(int(x) for x in p[1:])
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


def run_metrics(d, deep_lat_us):
    meta = read_kv(os.path.join(d, "meta"))
    window = float(meta["t1"]) - float(meta["t0"])
    row = {"scenario": meta["scenario"], "trial": int(meta["trial"]),
           "pos": int(meta.get("pos", 0)), "window_s": window,
           "obs_comm": meta.get("obs_comm", "")}
    row.update(idle_metrics(read_idle(os.path.join(d, "pre.idle")),
                            read_idle(os.path.join(d, "post.idle")),
                            window, deep_lat_us))
    s0, s1 = read_pairs(os.path.join(d, "pre.stat")), read_pairs(os.path.join(d, "post.stat"))
    if "intr" in s0 and "intr" in s1:
        row["intr_per_s"] = (s1["intr"] - s0["intr"]) / window
        row["ctxt_per_s"] = (s1["ctxt"] - s0["ctxt"]) / window
    o0, o1 = read_obs(os.path.join(d, "pre.obs")), read_obs(os.path.join(d, "post.obs"))
    common = set(o0) & set(o1)  # processes alive at both ends
    if common:
        hz = os.sysconf("SC_CLK_TCK")
        dv = [sum(o1[p][i] - o0[p][i] for p in common) for i in range(4)]
        row["obs_nproc"] = len(common)
        row["obs_vol_ctxt_per_s"] = dv[0] / window
        row["obs_nonvol_ctxt_per_s"] = dv[1] / window
        row["obs_cpu_ms_per_s"] = (dv[2] + dv[3]) / hz * 1000.0 / window
    elif meta.get("obs_pid"):
        row["obs_lost"] = 1  # observer configured but not alive at both ends
    for k, v in parse_perf(os.path.join(d, "perf.csv")).items():
        row[k] = v
        row[k[:-2] + "_W"] = v / window
    row.update(parse_turbostat(os.path.join(d, "turbostat.txt")))
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
    ap.add_argument("--deep-latency-us", type=float, default=100.0,
                    help="cpuidle states with exit latency >= this count as deep (default 100)")
    args = ap.parse_args()

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
    n = len(rows)
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
    lines += ["", "_Trace-pass metrics (`idle_exit_*`) come from a separate perturbing "
              "bpftrace window and are not comparable to power numbers._", ""]
    with open(os.path.join(args.outdir, "summary.md"), "w") as f:
        f.write("\n".join(lines))
    print("\n".join(lines))


if __name__ == "__main__":
    main()
