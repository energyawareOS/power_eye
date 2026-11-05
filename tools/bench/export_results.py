#!/usr/bin/env python3
"""Export raw per-run data (no averaging) to results.csv and env.txt.

Reads OUTDIR/runs.csv (made by aggregate.py) and OUTDIR/env.txt (made by
bench.sh); writes OUTDIR/results.csv and OUTDIR/env.txt (overwritten with a
short summary; the full bench environment is kept as env_full.txt).

Scenario names -> condition: none, fixed_<interval> (sysfs), ebpf_<interval>
(-B, fixed interval), adaptive, ebpf, node_exporter, scaphandre. interval_s is
set for fixed_* and ebpf_*. Anything else is an error, so nothing is silently
relabeled.

Columns:
  pkg_power_w            pkg_W (RAPL energy-pkg over the window)
  deep_cstate_pct        pkg_c6_pct + pkg_c7_pct (package C6 or deeper)
  wakeups_per_s          idle_entries_per_s (system-wide, all CPUs)
  tool_idle_exits_per_s  extra system-wide idle entries per second compared with the
                         neighbouring `none` runs of the same trial (idle_entries_per_s
                         minus their mean). Because it is system-wide it includes CPUs
                         woken by IPI for remote RAPL reads. Noisy per run (it is a
                         difference of two ~400/s counts); use means over runs.
                         If the run has no `none` neighbour (non-interleaved data
                         without a baseline) it is empty, also for `none` itself.
  source                 <results dir>/<scenario>
  tool_cpu_ms_per_s      CPU time of the observer (all its processes) per second
"""
import csv, os, re, shutil, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from aggregate import baseline_mean

FIELDS = ["condition", "interval_s", "run", "pkg_power_w", "deep_cstate_pct",
          "wakeups_per_s", "tool_idle_exits_per_s", "source", "tool_cpu_ms_per_s"]


def condition(name):
    if name in ("none", "adaptive", "ebpf", "node_exporter", "scaphandre"):
        return name, ""
    m = re.fullmatch(r"(fixed|ebpf)_([0-9.]+)", name)
    if m:
        return m.group(1), m.group(2)
    raise SystemExit("unknown scenario name %r (expected none, fixed_<s>, "
                     "adaptive, ebpf, node_exporter)" % name)


def num(row, key):
    v = row.get(key, "")
    return float(v) if v not in ("", None) else None


def export(rows, src="", numeric=None):
    out = []
    for r in rows:
        cond, iv = condition(r["scenario"])
        p6, p7 = num(r, "pkg_c6_pct"), num(r, "pkg_c7_pct")
        deep = None if p6 is None else p6 + (p7 or 0.0)
        tool = None
        if cond != "none" and numeric is not None:
            b = baseline_mean(numeric, numeric[r["_i"]], "idle_entries_per_s")
            e = num(r, "idle_entries_per_s")
            if b is not None and e is not None:
                tool = e - b
        vals = [num(r, "pkg_W"), deep, num(r, "idle_entries_per_s"), tool]
        out.append([cond, iv, r["trial"]] +
                   ["" if v is None else repr(round(v, 6)) for v in vals] +
                   ["%s/%s" % (src, r["scenario"]),
                    "" if num(r, "obs_cpu_ms_per_s") is None else repr(round(num(r, "obs_cpu_ms_per_s"), 6))])
    out.sort(key=lambda x: (x[0], float(x[1] or 0), int(x[2]), x[7]))
    return out


def field(lines, prefix):
    for l in lines:
        if l.startswith(prefix):
            return l[len(prefix):].strip()
    return ""


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else sys.exit(__doc__)
    with open(os.path.join(d, "runs.csv"), newline="") as f:
        rows = list(csv.DictReader(f))
    with open(os.path.join(d, "results.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(FIELDS)
        numeric = []
        for i, r in enumerate(rows):
            r["_i"] = i
            numeric.append({"scenario": r["scenario"], "trial": int(r["trial"]),
                            "pos": int(r["pos"] or 0),
                            **({"idle_entries_per_s": float(r["idle_entries_per_s"])}
                               if r.get("idle_entries_per_s") else {})})
        w.writerows(export(rows, os.path.basename(os.path.normpath(d)), numeric))

    envp = os.path.join(d, "env.txt")
    full = os.path.join(d, "env_full.txt")
    if not os.path.exists(full):
        shutil.copy(envp, full)
    lines = open(full).read().splitlines()
    wins = [float(r["window_s"]) for r in rows if r.get("window_s")]
    cpu = field(lines, "Model name:")
    with open(envp, "w") as f:
        f.write("cpu_model: %s\n" % cpu)
        f.write("kernel: %s\n" % (lines[lines.index("## uname -a") + 1].split()[2]
                                  if "## uname -a" in lines else ""))
        f.write("bios_cstate_settings: %s\n" % field(lines, "note:"))
        f.write("run_config: %s\n" % next((l for l in lines if l.startswith("window=")), ""))
        if wins:
            f.write("measured_window_s: min %.1f, max %.1f (actual, per run)\n"
                    % (min(wins), max(wins)))
        f.write("runs: %d rows, scenarios: %s\n" % (
            len(rows), ", ".join(sorted({r["scenario"] for r in rows}))))
        f.write("columns: see tools/bench/export_results.py (deep_cstate_pct = "
                "pkg C6 + C7; tool_idle_exits_per_s = system-wide idle entries minus "
                "the neighbouring none runs, includes IPI-woken CPUs; "
                "tool_cpu_ms_per_s = observer CPU time)\n")
        f.write("full environment: env_full.txt\n")


if __name__ == "__main__":
    main()
