# idle-exit piggyback sampler (prototype)

A BPF program on `raw_tp/cpu_idle` reads the package RAPL energy and package
C6 residency counters on the idle exit of whichever CPU wakes first, at most
once per `-m` milliseconds per package. There is no timer and no IPI: when
nothing leaves idle, nothing runs. The user-space part only reads BPF maps.

Why it can work: `../bpf-feasibility` showed that `bpf_perf_event_read_value()`
reads the package-scoped perf events from every CPU of the package (one event
per package, opened on its lead CPU, is enough).

## Build and run (root needed)

    make
    sudo ./idle_sampler -i 1 -m 100 -t 60

- `-o` BPF object (default: `idle_sampler.bpf.o` next to the executable)
- `-i` print interval (s), `-m` minimum interval between BPF samples per
  package (ms, default 100), `-t` total run time (s, default: until Ctrl-C),
  `-a` maximum sample age cap (s, default 600), see the safety timer below.

Each line shows, per package: power and C6 residency from BPF samples only,
`reads/s` (BPF samples taken), `age` (time since the last sample at print
time); then total idle exits per second and the BPF program's own cost
(runs/s, average ns per run, from kernel BPF run-time stats).

At exit it compares the mean power derived from the BPF samples with a direct
`read()` of the same perf events over the whole run; the two should agree to
well under 1% on an idle machine.

## Safety timer

Idle exits only happen when something wakes a CPU. On a node in a long idle
nothing may do so for minutes, and the 32-bit RAPL counter must not wrap
unseen. The loader therefore wakes at most every `max_age/2` and, for a
package whose last sample is at least that old, pins itself to a CPU of that
package and calls `getppid()`; a second BPF hook on that syscall (only for the
loader's own pid) takes the sample on that CPU and ignores the rate limit.

`max_age` is the `-a` cap (default 600 s, the idle-sampling cap) tightened by
the RAPL wrap bound: `max_energy_range_uj / max_power_uw / 2` from powercap
(`constraint_*_max_power_uw`; if unreadable only `-a` applies). The startup
line prints the effective forcing age. Forced samples are counted (`forced`
in the output).

Test it on a busy machine by making the idle-exit samples rare:

    sudo ./idle_sampler -i 5 -m 1000000 -a 20 -t 60

Expect `forced` to grow roughly every 10 s per package, `age` to stay below
`-a`, and the final BPF-vs-direct comparison to still agree.

Cost note: a forced sample wakes one CPU of the package (the loader's own
timer wake is the only other wakeup). The RAPL perf PMU may additionally run
its own periodic kernel timer on the lead CPU while the event is open to
avoid missing counter wraps; this is from memory and not verified here.

## What to look at

- `ERR` in a line: `bpf_perf_event_read_value()` failed in that context (the
  last errno is printed); this is the open question for the idle-exit hook.
- `age` is bounded by the safety timer (see above); a `forced` count that
  keeps growing means the package rarely leaves idle.
- `prog avg ns`: cost of one program run including a counter read when it is
  not rate limited.

## Not in the prototype yet

Attribution of idle exits to power-eye (first task after the exit), integration
into `power_eye`.
