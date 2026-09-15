# BPF perf-event read feasibility probe

Question: can a BPF program read a package-scoped perf event (RAPL energy,
`cstate_pkg` residency) with `bpf_perf_event_read_value()`, and from which CPUs?
This decides the design of the "piggyback on idle exit" sampler: if the read only
works on the PMU's lead CPU (the `cpumask` CPU), the sampler must hook idle exit
of those CPUs only.

For every online CPU the tool opens the event with `perf_event_open(pid=-1, cpu=c)`,
stores the fds in a `PERF_EVENT_ARRAY`, and pins itself to each CPU in turn while a
tracepoint program (`sys_enter_getppid`) reads the counter two ways:

- **A**: slot `BPF_F_CURRENT_CPU` (the CPU the program runs on)
- **B**: the slot of the package's lead CPU (read from a different CPU)

Each read is done twice, 50 ms apart, so "works" means the counter actually advanced.

```bash
make
sudo ./rapl_probe                                   # power / energy-pkg
sudo ./rapl_probe -P cstate_pkg -e c6-residency     # package C6 residency
sudo ./rapl_probe | tee probe-$(hostname).txt
```

Requires root (CAP_BPF + CAP_PERFMON), `libbpf-dev` and `clang`. Only
package-scoped PMUs (`power`, `cstate_pkg`) are handled.
