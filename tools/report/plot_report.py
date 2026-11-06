#!/usr/bin/env python3
"""Draw the three report figures from the committed result files.

    tools/report/plot_report.py [--out results/report] [--format png,svg]

Figure 1  observer cost per condition   <- results/realistic/runs.csv
Figure 2  adaptive interval and power   <- results/coma-long/coma-long-power.txt
Figure 3  sample rate -> extra power    <- results/dose-pkgc6/runs.csv, results/realistic/runs.csv
Each figure's plotted numbers are also written as figN_*.csv next to the images.
Needs matplotlib. Labels are English.
"""
import argparse, csv, math, os, re, statistics, sys
from datetime import datetime

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "bench"))
from aggregate import mean_ci, baseline_rows  # noqa: E402

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

# reference categorical palette (light mode), fixed slot order; the family, not the rank, owns a color
BLUE, ORANGE, AQUA, MAGENTA = "#2a78d6", "#eb6834", "#1baf7a", "#e87ba4"
INK, INK2, GRID, SURFACE = "#0b0b0b", "#52514e", "#e4e3df", "#fcfcfb"
plt.rcParams.update({
    "figure.facecolor": SURFACE, "axes.facecolor": SURFACE, "savefig.facecolor": SURFACE,
    "text.color": INK, "axes.labelcolor": INK2, "xtick.color": INK2, "ytick.color": INK2,
    "axes.edgecolor": GRID, "axes.grid": True, "grid.color": GRID, "grid.linewidth": 0.8,
    "axes.spines.top": False, "axes.spines.right": False, "font.size": 10,
    "axes.titlesize": 11, "axes.titleweight": "regular", "axes.titlelocation": "left",
})
FAMILY = {"sysfs": (BLUE, "o"), "ebpf": (ORANGE, "s"), "adaptive": (AQUA, "D"), "node_exporter": (MAGENTA, "^")}


def read(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def save(fig, out, name, formats):
    for fmt in formats:
        fig.savefig(os.path.join(out, "%s.%s" % (name, fmt)), dpi=200, bbox_inches="tight")
    plt.close(fig)


def write_csv(out, name, header, rows):
    with open(os.path.join(out, name), "w", newline="") as f:
        w = csv.writer(f); w.writerow(header); w.writerows(rows)


# ---------------------------------------------------------------- figure 1
ORDER = [("fixed_1", "sysfs", "sysfs  -i 1"), ("fixed_2", "sysfs", "sysfs  -i 2"), ("fixed_4", "sysfs", "sysfs  -i 4"),
         ("ebpf_1", "ebpf", "-B  -i 1"), ("ebpf_2", "ebpf", "-B  -i 2"), ("ebpf_4", "ebpf", "-B  -i 4"),
         ("adaptive", "adaptive", "adaptive  -A 600"), ("node_exporter", "node_exporter", "node_exporter + Prometheus")]


def figure1(root, out, formats):
    rows = read(os.path.join(root, "results/realistic/runs.csv"))
    panels = [("obs_cpu_ms_per_s", "Observer CPU time (ms per second)"),
              ("obs_vol_ctxt_per_s", "Observer wakeups (times scheduled per second)")]
    floor = 1e-3
    fig, axes = plt.subplots(1, 2, figsize=(10.5, 4.2), sharey=True)
    data = []
    for ax, (col, title) in zip(axes, panels):
        for y, (scen, fam, label) in enumerate(reversed(ORDER)):
            vals = [float(r[col]) for r in rows if r["scenario"] == scen and r[col] != ""]
            m, ci, n = mean_ci(vals)
            color, mk = FAMILY[fam]
            ax.scatter([max(v, floor) for v in vals], [y] * len(vals), s=14, color=color, alpha=0.35, marker=mk, linewidths=0)
            lo, hi = max(m - (ci or 0), floor), max(m + (ci or 0), floor)
            ax.plot([lo, hi], [y, y], color=color, lw=2, solid_capstyle="round")
            ax.scatter([max(m, floor)], [y], s=46, color=color, marker=mk, edgecolor=SURFACE, linewidths=1.5, zorder=3)
            ax.annotate("<0.001" if m < floor else ("%.3g" % m), (max(hi, m, floor), y), xytext=(8, 0),
                        textcoords="offset points", va="center", fontsize=8.5, color=INK2)
            data.append([scen, col, n, m, ci])
        ax.set_xscale("log"); ax.set_xlim(floor * 0.6, 400)
        ax.set_title(title); ax.set_yticks(range(len(ORDER)))
        ax.set_yticklabels([o[2] for o in reversed(ORDER)])
        ax.grid(axis="y", visible=False)
    fig.tight_layout()
    save(fig, out, "fig1_observer_cost", formats)
    write_csv(out, "fig1_observer_cost.csv", ["scenario", "metric", "n", "mean", "ci95"], data)


# ---------------------------------------------------------------- figure 2
def figure2(root, out, formats):
    ticks, wins, coma, decl = [], [], None, None
    day = None
    with open(os.path.join(root, "results/coma-long/coma-long-power.txt")) as f:
        for l in f:
            if l.startswith("# window"):
                m = re.search(r"window (\d\d-\d\d) (\d\d:\d\d:\d\d) \.\. (\d\d-\d\d) (\d\d:\d\d:\d\d).*-> (\w+)", l)
                wins.append((m.group(2), m.group(4), m.group(5)))
            elif l.startswith("# COMA CANDIDATE"):
                coma = re.search(r"since \d\d-\d\d (\d\d:\d\d:\d\d)", l).group(1)
                decl = wins[-1][1]          # the window that closed just before the verdict
            elif l[:1].isdigit():
                p = l.split()
                ticks.append((p[0], float(p[1]), float(p[-1])))     # end time, CPU W, INT s

    def sec(t):
        h, m, s = map(int, t.split(":")); return h * 3600 + m * 60 + s
    t0 = sec(ticks[0][0]) - ticks[0][2]
    mins = lambda t: (sec(t) - t0) / 60.0
    fig, (a1, a2) = plt.subplots(2, 1, figsize=(10.5, 6), sharex=True, gridspec_kw={"height_ratios": [1, 1.2]})
    rows = []
    for end, w, iv in ticks:
        x1 = mins(end); x0 = x1 - iv / 60.0
        a1.plot([x0, x1], [w, w], color=BLUE, lw=1.8, solid_capstyle="butt")
        a2.plot([x0, x1], [iv, iv], color=AQUA, lw=2.2, solid_capstyle="butt")
        a2.scatter([x1], [iv], s=14, color=AQUA, zorder=3, edgecolor=SURFACE, linewidths=1)
        rows.append([end, round(x0, 3), round(x1, 3), w, iv])
    for a in (a1, a2):
        for s, e, verdict in wins:
            if verdict == "busy":
                a.axvspan(mins(s), mins(e), color="#52514e", alpha=0.10, lw=0)
        if coma:
            a.axvline(mins(coma), color=INK2, lw=1, ls=(0, (4, 3)))
            a.axvline(mins(decl), color=INK, lw=1.2)
    a1.set_ylabel("Package power (W)"); a1.set_ylim(24, 28)
    a2.set_yscale("log"); a2.set_ylabel("Sampling interval (s)")
    a2.set_yticks([1, 10, 100, 600]); a2.set_yticklabels(["1", "10", "100", "600"])
    a2.set_ylim(0.8, 900); a2.axhline(600, color=INK2, lw=0.8, ls=":")
    a2.set_xlabel("Minutes since start")
    busy = [w for w in wins if w[2] == "busy"]
    if busy:
        a1.annotate("busy window\n(other job)", (mins(busy[0][0]), 24.45), xytext=(5, 0),
                    textcoords="offset points", va="center", fontsize=8.5, color=INK2)
    if coma:
        a1.annotate("idle starts\n(dashed)", (mins(coma), 27.55), xytext=(6, 0),
                    textcoords="offset points", va="center", fontsize=8.5, color=INK2)
        a1.annotate("coma candidate\ndeclared (solid)", (mins(decl), 27.55),
                    xytext=(6, 0), textcoords="offset points", va="center", fontsize=8.5, color=INK2)
    a2.annotate("cap 600 s", (a2.get_xlim()[0] + 1, 600), xytext=(0, 4), textcoords="offset points", fontsize=8.5, color=INK2)
    fig.tight_layout()
    save(fig, out, "fig2_adaptive_interval", formats)
    write_csv(out, "fig2_adaptive_interval.csv", ["tick_end", "start_min", "end_min", "cpu_W", "interval_s"], rows)


# ---------------------------------------------------------------- figure 3
def paired(rows, scen_rate):
    """scen -> list of (rate_hz, dP_mW) paired with the neighbouring / same-trial none runs."""
    num = [{"scenario": r["scenario"], "trial": int(r["trial"]), "pos": int(r["pos"] or 0), "pkg_W": float(r["pkg_W"])}
           for r in rows if r.get("pkg_W")]
    out = []
    for r in num:
        if r["scenario"] in scen_rate:
            b = baseline_rows(num, r)
            if b:
                out.append((scen_rate[r["scenario"]], 1000.0 * (r["pkg_W"] - statistics.fmean(x["pkg_W"] for x in b))))
    return out


def figure3(root, out, formats):
    dose = paired(read(os.path.join(root, "results/dose-pkgc6/runs.csv")),
                  {"hz1": 1, "hz5": 5, "hz20": 20, "hz100": 100})
    real = paired(read(os.path.join(root, "results/realistic/runs.csv")),
                  {"fixed_4": 0.25, "fixed_2": 0.5, "fixed_1": 1})
    # least squares through the origin on the runs above 1 Hz: slope = mJ per sample
    hi = [(x, y) for x, y in dose if x > 1]
    slope = sum(x * y for x, y in hi) / sum(x * x for x, _ in hi)
    resid = [y - slope * x for x, y in hi]
    se = math.sqrt(sum(e * e for e in resid) / (len(hi) - 1) / sum(x * x for x, _ in hi))
    fig, (a, b) = plt.subplots(1, 2, figsize=(10.5, 4.4), gridspec_kw={"width_ratios": [1.15, 1]})
    data = []

    def draw(ax, pts, color, marker, label, dx=0.0):
        for rate in sorted({x for x, _ in pts}):
            ys = [y for x, y in pts if x == rate]
            m, ci, n = mean_ci(ys)
            xp = rate + dx * (1.25 if ax is b else 100.0)     # side-by-side offset so the series do not overlap
            ax.scatter([xp] * len(ys), ys, s=12, color=color, alpha=0.3, marker=marker, linewidths=0)
            ax.errorbar([xp], [m], yerr=[ci or 0], color=color, lw=2, capsize=3, zorder=3)
            ax.scatter([xp], [m], s=46, color=color, marker=marker, edgecolor=SURFACE, linewidths=1.5, zorder=4,
                       label=label if rate == min(x for x, _ in pts) else None)
            if ax is a:
                data.append([label, rate, n, m, ci])
    for ax, xmax in ((a, 110), (b, 1.25)):
        xs = [0, xmax]
        ax.plot(xs, [slope * x for x in xs], color=INK2, lw=1.2, ls=(0, (4, 3)))
        ax.axhline(0, color=INK2, lw=0.8)
        draw(ax, dose, BLUE, "o", "dose-response (1-100 Hz)", -0.012)
        draw(ax, real, ORANGE, "s", "realistic periods (-i 4 / 2 / 1)", 0.012)
        ax.set_xlim(-xmax * 0.03, xmax)
        ax.set_xlabel("Observer sample rate (Hz)")
    a.set_ylabel("Extra package power vs none (mW)")
    a.set_title("Full range")
    b.set_title("Zoom (<= 1 Hz)")
    b.set_ylim(-700, 700)
    a.legend(loc="upper left", fontsize=8.5, frameon=False)
    a.text(52, 250, "fit: %.1f ± %.1f mJ/sample" % (slope, 2.0 * se),
           ha="left", va="center", fontsize=8.5, color=INK2)
    fig.tight_layout()
    save(fig, out, "fig3_dose_response", formats)
    write_csv(out, "fig3_dose_response.csv", ["series", "rate_hz", "n", "mean_dP_mW", "ci95_mW"], data)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="results/report")
    ap.add_argument("--format", default="png,svg")
    ap.add_argument("--root", default=".", help="repository root (default: current directory)")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    fm = a.format.split(",")
    figure1(a.root, a.out, fm); figure2(a.root, a.out, fm); figure3(a.root, a.out, fm)
    print("wrote figures and data to", a.out)


if __name__ == "__main__":
    main()
