#!/usr/bin/env python3
"""Compile results.csv / env.txt from already finished runs (no new measurement).

    compile_existing.py OUTDIR

Uses results/dose-pkgc6, results/sysfs-vs-bpf and results/coma-long (all taken
with package C6 enabled) and, separately, results/smoke2 for node_exporter
(different BIOS state, so it is NOT mixed into results.csv).
Columns as in export_results.py plus a trailing `source` column.
"""
import csv, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from export_results import num

FIELDS = ["condition", "interval_s", "run", "pkg_power_w", "deep_cstate_pct",
          "wakeups_per_s", "tool_idle_exits_per_s", "source"]
R = "results"


def fmt(v):
    return "" if v is None else repr(round(v, 6))


def bench_rows(d, mapping):
    """mapping: scenario -> (condition, interval_s)."""
    rows = []
    for r in csv.DictReader(open(os.path.join(R, d, "runs.csv"))):
        if r["scenario"] not in mapping:
            continue
        cond, iv = mapping[r["scenario"]]
        p6, p7 = num(r, "pkg_c6_pct"), num(r, "pkg_c7_pct")
        deep = None if p6 is None else p6 + (p7 or 0.0)
        tool = None if cond == "none" else num(r, "obs_vol_ctxt_per_s")
        rows.append([cond, iv, r["trial"], fmt(num(r, "pkg_W")), fmt(deep),
                     fmt(num(r, "idle_entries_per_s")), fmt(tool),
                     "%s/%s" % (d, r["scenario"])])
    return rows


def coma_rows():
    """One row per 10-minute window of the -A run (adaptive); run = window no."""
    rows, ticks, n = [], [], 0
    for l in open(os.path.join(R, "coma-long", "coma-long-power.txt")):
        if l.startswith("# window"):
            m = re.search(r"([\d.]+) W \(floor.*deep ([\d.]+)%", l)
            w, deep = float(m.group(1)), float(m.group(2))
            span = sum(t[0] for t in ticks)
            wakes = sum(max(1, round(t[1] * t[0])) for t in ticks)
            n += 1
            rows.append(["adaptive", "", str(n), fmt(w), fmt(deep), "",
                         fmt(wakes / span), "coma-long/window%d" % n])
            ticks = []
        elif l[:1].isdigit():
            f = l.split()
            ticks.append((float(f[-1]), float(f[6])))   # INT, OBS-WAKE
    return rows


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else sys.exit(__doc__)
    os.makedirs(out, exist_ok=True)
    rows = bench_rows("dose-pkgc6", {"none": ("none", ""), "hz1": ("fixed", "1"),
        "hz5": ("fixed", "0.2"), "hz20": ("fixed", "0.05"), "hz100": ("fixed", "0.01")})
    rows += bench_rows("sysfs-vs-bpf", {"none": ("none", ""),
        "sys20": ("fixed", "0.05"), "sys100": ("fixed", "0.01"),
        "bpf20": ("ebpf", ""), "bpf100": ("ebpf", "")})
    rows += coma_rows()
    order = {"none": 0, "fixed": 1, "adaptive": 2, "ebpf": 3}
    rows.sort(key=lambda x: (order[x[0]], float(x[1] or 0), x[7], int(x[2]) if x[2].isdigit() else 0))
    with open(os.path.join(out, "results.csv"), "w", newline="") as f:
        w = csv.writer(f); w.writerow(FIELDS); w.writerows(rows)

    ne = bench_rows("smoke2", {"none": ("none", ""), "pe1s": ("fixed", "1"),
                               "node_exporter": ("node_exporter", "")})
    with open(os.path.join(out, "results_node_exporter_old_bios.csv"), "w", newline="") as f:
        w = csv.writer(f); w.writerow(FIELDS); w.writerows(ne)
    print(len(rows), "rows in results.csv,", len(ne), "rows in node_exporter file")


if __name__ == "__main__":
    main()
