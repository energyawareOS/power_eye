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


def make_run(out, trial, scen, pkg_j, c6_us, entries, vol0, vol1, trace=None, cmd="", pos=1, cal=None):
    d = os.path.join(out, "raw", "%03d_%02d_%s" % (trial, pos, scen))
    os.makedirs(d)
    write(os.path.join(d, "meta"),
          "scenario=%s\ntrial=%d\npos=%d\ndur=100\nt0=1000.0\nt1=%.1f\n"
          "trace_dur=%d\nobs_pid=1\nobs_comm=x\ncmd=%s\nn_pkg=2\nn_core=24\n" %
          (scen, trial, pos, 1000.0 + WINDOW + 3.0, 10 if trace else 0, cmd))  # outer window is 3 s longer
    # snapshots span exactly WINDOW even though the outer window is longer
    write(os.path.join(d, "times"),
          "pre.obs 999.0 1001.0\npost.obs 1099.0 1101.0\n"
          "pre.stat 999.9 1000.1\npost.stat 1099.9 1100.1\n"
          "pre.idle 1000.0 1000.0\npost.idle 1100.0 1100.0\n"
          "pre.irq 999.9 1000.1\npost.irq 1099.9 1100.1\n")
    write(os.path.join(d, "pre.irq"), "CAL 1000\nRES 500\n")
    write(os.path.join(d, "post.irq"), "CAL %d\nRES 1500\n" % (1000 + (cal or 0)))
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
    write(os.path.join(d, "pre.obs"), "10 5 0 0 0 0\n11 %d 0 0 0 0\n" % vol0)
    write(os.path.join(d, "post.obs"),
          "10 5 0 0 0 0\n11 %d 0 1 1 10000000\n12 999 0 999 999 999\n" % vol1)
    write(os.path.join(d, "perf.csv"),
          "# started on ...\n\n%.2f,Joules,power/energy-pkg/,100000000000,100.00,,\n"
          "<not supported>,,power/energy-gpu/,0,100.00,,\n"
          # 2 packages * 2200 MHz * 100 s * 80% = 352e9 ticks; 24 cores * 90% = 4752e9
          "352000000000,,cstate_pkg/c6-residency/,100000000000,100.00,,\n"
          "4752000000000,,cstate_core/c6-residency/,100000000000,100.00,,\n" % pkg_j)
    write(os.path.join(d, "turbostat.txt"),
          "Core\tCPU\tAvg_MHz\tBusy%\tTSC_MHz\tPkg%pc6\tPkgWatt\n-\t-\t800\t1.50\t2200\t42.5\t7.25\n")
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
            self.assertAlmostEqual(float(r["pkg_c6_pct"]), 80.0)
            self.assertAlmostEqual(float(r["core_c6_pct"]), 90.0)
            o = rows[("obs", "2")]
            self.assertAlmostEqual(float(o["obs_vol_ctxt_per_s"]), 2.0)
            # 10 ms of schedstat runtime over 100 s -> 0.1 ms/s (clock ticks would say 20)
            self.assertAlmostEqual(float(o["obs_cpu_ms_per_s"]), 0.1, places=4)
            self.assertAlmostEqual(float(o["idle_exit_obs_per_s"]), 3.0)
            with open(os.path.join(out, "summary.md")) as f:
                summary = f.read()
            self.assertIn("Paired difference", summary)
            self.assertIn("1.000", summary)  # 100 J more over 100 s -> +1.000 W
            self.assertRegex(summary, r"1\.000 ± 0\.000\*")


class InterleaveTest(unittest.TestCase):
    def test_pairs_with_neighbouring_baselines(self):
        with tempfile.TemporaryDirectory() as out:
            make_run(out, 1, "none", 500.0, 90_000_000, 200, 0, 0, pos=1)
            make_run(out, 1, "obs", 600.0, 90_000_000, 200, 0, 0, pos=2, cal=3000)
            make_run(out, 1, "none", 520.0, 90_000_000, 200, 0, 0, pos=3)
            make_run(out, 1, "obs2", 700.0, 90_000_000, 200, 0, 0, pos=4)
            make_run(out, 1, "none", 580.0, 90_000_000, 200, 0, 0, pos=5)
            subprocess.run([sys.executable, os.path.join(HERE, "aggregate.py"), out],
                           check=True, stdout=subprocess.DEVNULL)
            with open(os.path.join(out, "runs.csv")) as f:
                rows = [r for r in csv.DictReader(f)]
            self.assertEqual(len(rows), 5)
            o = [r for r in rows if r["scenario"] == "obs"][0]
            self.assertAlmostEqual(float(o["ipi_call_per_s"]), 30.0)   # 3000 IPIs / 100 s
            self.assertAlmostEqual(float(o["ipi_res_per_s"]), 10.0)
            with open(os.path.join(out, "summary.md")) as f:
                text = f.read()
            # obs: 6.00 W vs mean(5.00, 5.20) = 5.10 -> +0.900; obs2: 7.00 vs mean(5.20, 5.80) = 5.50 -> +1.500
            self.assertRegex(text, r"\| obs \| 1 \|[^\n]*0\.900")
            self.assertRegex(text, r"\| obs2 \| 1 \|[^\n]*1\.500")


class DoseResponseTest(unittest.TestCase):
    def test_slope_is_energy_per_sample(self):
        with tempfile.TemporaryDirectory() as out:
            # 45 W idle + 0.02 W per Hz of sampling (= 20 mJ per sample), tiny jitter
            for t in (1, 2, 3):
                for name, cmd, hz in (("none", "", 0), ("r1", "build/src/power_eye -i 1 -f cE", 1),
                                      ("r10", "build/src/power_eye -i 0.1 -f cE", 10),
                                      ("r50", "build/src/power_eye -i 0.02 -f cE", 50)):
                    make_run(out, t, name, (45.0 + 0.02 * hz + 0.001 * t) * 100, 90_000_000, 200, 0, 0, cmd=cmd)
            subprocess.run([sys.executable, os.path.join(HERE, "aggregate.py"), out],
                           check=True, stdout=subprocess.DEVNULL)
            with open(os.path.join(out, "summary.md")) as f:
                text = f.read()
            self.assertIn("Dose-response", text)
            self.assertIn("Rates in the fit: 0, 1, 10, 50 Hz", text)
            self.assertRegex(text, r"\| pkg_W \| 45\.00\d \| 20\.0\d\d mW/Hz = mJ/sample")
            subprocess.run([sys.executable, os.path.join(HERE, "aggregate.py"), out, "--max-rate", "10"],
                           check=True, stdout=subprocess.DEVNULL)
            with open(os.path.join(out, "summary.md")) as f:
                self.assertIn("Rates in the fit: 0, 1, 10 Hz (limited", f.read())


class OutlierTest(unittest.TestCase):
    def test_outlier_warning(self):
        with tempfile.TemporaryDirectory() as out:
            for t, j in enumerate((500.0, 501.0, 500.5, 502.0, 800.0), 1):
                make_run(out, t, "none", j, 90_000_000, 200, 0, 0)
            subprocess.run([sys.executable, os.path.join(HERE, "aggregate.py"), out],
                           check=True, stdout=subprocess.DEVNULL)
            with open(os.path.join(out, "summary.md")) as f:
                text = f.read()
            self.assertIn("trial 5 none: pkg_W 8.00 W is an outlier", text)
            self.assertNotIn("trial 1 none: pkg_W", text)

    def test_shifted_scenario_is_not_an_outlier(self):
        # a scenario that shifts power as a whole is judged against itself
        with tempfile.TemporaryDirectory() as out:
            for t, j in enumerate((500.0, 501.0, 500.5, 502.0, 500.8), 1):
                make_run(out, t, "none", j, 90_000_000, 200, 0, 0)
            for t, j in enumerate((700.0, 701.0, 700.5, 702.0, 900.0), 1):
                make_run(out, t, "fast", j, 90_000_000, 200, 0, 0)
            subprocess.run([sys.executable, os.path.join(HERE, "aggregate.py"), out],
                           check=True, stdout=subprocess.DEVNULL)
            with open(os.path.join(out, "summary.md")) as f:
                text = f.read()
            self.assertIn("trial 5 fast: pkg_W 9.00 W is an outlier", text)
            self.assertNotIn("trial 1 fast: pkg_W", text)
            self.assertNotIn("none: pkg_W", text)


if __name__ == "__main__":
    unittest.main()
