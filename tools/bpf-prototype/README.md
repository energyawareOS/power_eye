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

- `-i` print interval (s), `-m` minimum interval between BPF samples per
  package (ms, default 100), `-t` total run time (s, default: until Ctrl-C).

Each line shows, per package: power and C6 residency from BPF samples only,
`reads/s` (BPF samples taken), `age` (time since the last sample at print
time); then total idle exits per second and the BPF program's own cost
(runs/s, average ns per run, from kernel BPF run-time stats).

At exit it compares the mean power derived from the BPF samples with a direct
`read()` of the same perf events over the whole run; the two should agree to
well under 1% on an idle machine.

## What to look at

- `ERR` in a line: `bpf_perf_event_read_value()` failed in that context (the
  last errno is printed); this is the open question for the idle-exit hook.
- `age` growing without bound on a package: no CPU of it left idle, i.e. the
  package is in a long idle. A real tool needs a safety timer for that case
  (RAPL counter wrap bound), not part of this prototype.
- `prog avg ns`: cost of one program run including a counter read when it is
  not rate limited.

## Not in the prototype yet

Attribution of idle exits to power-eye (first task after the exit), a safety
timer, integration into `power_eye`.
