#!/usr/bin/env python3
"""Export raw per-run data (no averaging) to results.csv and env.txt.

Reads OUTDIR/runs.csv (made by aggregate.py) and OUTDIR/env.txt (made by
bench.sh); writes OUTDIR/results.csv and OUTDIR/env.txt (overwritten with a
short summary; the full bench environment is kept as env_full.txt).

Scenario names -> condition: none, fixed_<interval>, adaptive, ebpf,
node_exporter. Anything else is an error, so nothing is silently relabeled.

Columns:
  pkg_power_w            pkg_W (RAPL energy-pkg over the window)
  deep_cstate_pct        pkg_c6_pct + pkg_c7_pct (package C6 or deeper)
  wakeups_per_s          idle_entries_per_s (system-wide, all CPUs)
  tool_idle_exits_per_s  obs_vol_ctxt_per_s: voluntary context switches of the
                         observer, i.e. how often it was put on a CPU after
                         sleeping. It does NOT count CPUs woken by IPI for
                         sysfs RAPL reads. Empty for 'none'.
"""
import csv, os, re, shutil, sys

FIELDS = ["condition", "interval_s", "run", "pkg_power_w", "deep_cstate_pct",
          "wakeups_per_s", "tool_idle_exits_per_s"]


def condition(name):
    if name in ("none", "adaptive", "ebpf", "node_exporter"):
        return name, ""
    m = re.fullmatch(r"fixed_([0-9.]+)", name)
    if m:
        return "fixed", m.group(1)
    raise SystemExit("unknown scenario name %r (expected none, fixed_<s>, "
                     "adaptive, ebpf, node_exporter)" % name)


def num(row, key):
    v = row.get(key, "")
    return float(v) if v not in ("", None) else None


def export(rows):
    out = []
    for r in rows:
        cond, iv = condition(r["scenario"])
        p6, p7 = num(r, "pkg_c6_pct"), num(r, "pkg_c7_pct")
        deep = None if p6 is None else p6 + (p7 or 0.0)
        vals = [num(r, "pkg_W"), deep, num(r, "idle_entries_per_s"),
                num(r, "obs_vol_ctxt_per_s")]
        out.append([cond, iv, r["trial"]] +
                   ["" if v is None else repr(round(v, 6)) for v in vals])
    out.sort(key=lambda x: (x[0], float(x[1] or 0), int(x[2])))
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
        w.writerows(export(rows))

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
                "pkg C6 + C7; tool_idle_exits_per_s = observer voluntary "
                "context switches, excludes IPI wakeups)\n")
        f.write("full environment: env_full.txt\n")


if __name__ == "__main__":
    main()
