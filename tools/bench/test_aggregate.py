#!/usr/bin/env python3
"""Unit test for aggregate.py using synthetic raw data (no root needed)."""
import csv
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
NCPU, WINDOW = 4, 100.0


def write(path, text):
    with open(path, "w") as f:
        f.write(text)


def make_run(out, trial, scen, pkg_j, c6_us, entries, vol0, vol1, trace=None):
    d = os.path.join(out, "raw", "%03d_%s" % (trial, scen))
    os.makedirs(d)
    write(os.path.join(d, "meta"),
          "scenario=%s\ntrial=%d\npos=1\ndur=100\nt0=1000.0\nt1=%.1f\n"
          "trace_dur=%d\nobs_pid=1\nobs_comm=x\ncmd=\n" %
          (scen, trial, 1000.0 + WINDOW, 10 if trace else 0))
    pre = post = ""
    for cpu in range(NCPU):
        pre += "%d\t0\tPOLL\t0\t0\t0\n%d\t1\tC1\t0\t0\t1\n%d\t2\tC6\t0\t0\t133\n" % (cpu, cpu, cpu)
        post += ("%d\t0\tPOLL\t0\t0\t0\n%d\t1\tC1\t%d\t%d\t1\n%d\t2\tC6\t%d\t%d\t133\n" %
                 (cpu, cpu, entries // 2, 10_000_000, cpu, entries // 2, c6_us))
    write(os.path.join(d, "pre.idle"), pre)
    write(os.path.join(d, "post.idle"), post)
    write(os.path.join(d, "pre.stat"), "intr 1000\nctxt 5000\n")
    write(os.path.join(d, "post.stat"), "intr 3000\nctxt 15000\n")
    # one wrapper (idle) + one worker; a third process exists only at the end and must be ignored
    write(os.path.join(d, "pre.obs"), "10 5 0 0 0\n11 %d 0 0 0\n" % vol0)
    write(os.path.join(d, "post.obs"), "10 5 0 0 0\n11 %d 0 50 50\n12 999 0 999 999\n" % vol1)
    write(os.path.join(d, "perf.csv"),
          "# started on ...\n\n%.2f,Joules,power/energy-pkg/,100000000000,100.00,,\n"
          "<not supported>,,power/energy-gpu/,0,100.00,,\n" % pkg_j)
    write(os.path.join(d, "turbostat.txt"),
          "Core\tCPU\tAvg_MHz\tBusy%\tPkg%pc6\tPkgWatt\n-\t-\t800\t1.50\t42.5\t7.25\n")
    if trace:
        write(os.path.join(d, "trace.txt"),
              "Attaching 1 probe...\n\n@idle_exit_obs: %d\n@idle_exit_total: %d\n" % trace)


class AggregateTest(unittest.TestCase):
    def test_end_to_end(self):
        with tempfile.TemporaryDirectory() as out:
            for t in (1, 2, 3):
                make_run(out, t, "none", 500.0 + t, 90_000_000, 200, 0, 0)
                make_run(out, t, "obs", 600.0 + t, 85_000_000, 400, 0, 200, trace=(30, 100))
            subprocess.run([sys.executable, os.path.join(HERE, "aggregate.py"), out],
                           check=True, stdout=subprocess.DEVNULL)
            with open(os.path.join(out, "runs.csv")) as f:
                rows = {(r["scenario"], r["trial"]): r for r in csv.DictReader(f)}
            r = rows[("none", "1")]
            self.assertAlmostEqual(float(r["pkg_W"]), 5.01)
            # deep (C6, latency 133us >= 100): 90e6us per cpu over a 100 s window -> 90%
            self.assertAlmostEqual(float(r["deep_resid_pct"]), 90.0)
            self.assertAlmostEqual(float(r["idle_entries_per_s"]), 8.0)  # 4 cpus * 200 / 100
            self.assertAlmostEqual(float(r["intr_per_s"]), 20.0)
            self.assertAlmostEqual(float(r["ts_Pkg%pc6"]), 42.5)
            o = rows[("obs", "2")]
            self.assertAlmostEqual(float(o["obs_vol_ctxt_per_s"]), 2.0)
            self.assertAlmostEqual(float(o["obs_cpu_ms_per_s"]), 10000 / 100 / 100 * 10, places=3)
            self.assertAlmostEqual(float(o["idle_exit_obs_per_s"]), 3.0)
            with open(os.path.join(out, "summary.md")) as f:
                summary = f.read()
            self.assertIn("Paired difference", summary)
            self.assertIn("1.000", summary)  # 100 J more over 100 s -> +1.000 W
            self.assertRegex(summary, r"1\.000 ± 0\.000\*")


if __name__ == "__main__":
    unittest.main()
