#!/usr/bin/env bash
# bench.sh - non-perturbing A/B harness for observer-effect experiments.
#
# Each trial runs every scenario once (in random order). A scenario is an
# "observer" command (power_eye, node_exporter, ...) or nothing (baseline
# "none"). The reference measurement only reads counters at the START and END
# of the window, so the measurement itself causes no wakeups in between:
#
#   * turbostat in command-wrapping mode (no --interval)
#   * perf stat -a without -I (counting mode, read once at exit)
#   * cpuidle sysfs, /proc/stat and the observer's /proc/<pid> before/after
#
# The optional trace pass (--trace) is a SEPARATE window with bpftrace; it
# perturbs the system and must never be used for power numbers.
set -u -o pipefail

DUR=600 TRIALS=5 SETTLE=60 COOLDOWN=10 TRACE_DUR=120
OUT="" NOTE="" SEED="" TRACE=0 IPMI=0 DRY=0 BASELINE=1 TURBOSTAT=1
SC_NAME=() SC_CMD=()
SYS=/sys/devices/system/cpu

usage() {
    cat <<USAGE
Usage: sudo $0 -o OUTDIR [options] -S name=command [-S name=command ...]

  -o DIR           Output directory (required)
  -d SEC           Measurement window per run (default $DUR)
  -n N             Trials; each trial runs every scenario once (default $TRIALS)
  -s SEC           Settle time after starting the observer (default $SETTLE)
  -c SEC           Cooldown after stopping the observer (default $COOLDOWN)
  -S name=cmd      Observer scenario; the command is exec'd in its own session
                   and must not need sudo. Baseline "none" is added implicitly.
  --no-baseline    Do not add the implicit "none" scenario
  --no-turbostat   Use only perf stat for the window
  --trace [SEC]    Extra bpftrace pass per run counting idle exits (default $TRACE_DUR s)
  --ipmi           Record 'ipmitool dcmi power reading' before/after (unvalidated)
  --note TEXT      Free text stored in env.txt (e.g. BIOS C-state settings)
  --seed S         Reproducible scenario order
  --dry-run        Print the plan and environment, run nothing
  -h               This help

Env: BENCH_WINDOW_CMD overrides the measured window command (testing only).
USAGE
}

die() { echo "bench.sh: $*" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case $1 in
    -o) OUT=$2; shift 2 ;;
    -d) DUR=$2; shift 2 ;;
    -n) TRIALS=$2; shift 2 ;;
    -s) SETTLE=$2; shift 2 ;;
    -c) COOLDOWN=$2; shift 2 ;;
    -S)
        [[ $2 == *=* ]] || die "-S expects name=command"
        n=${2%%=*}
        [[ $n =~ ^[A-Za-z0-9_.-]+$ ]] || die "bad scenario name: $n"
        SC_NAME+=("$n"); SC_CMD+=("${2#*=}"); shift 2 ;;
    --no-baseline) BASELINE=0; shift ;;
    --no-turbostat) TURBOSTAT=0; shift ;;
    --trace)
        TRACE=1
        if [[ ${2:-} =~ ^[0-9]+$ ]]; then TRACE_DUR=$2; shift; fi
        shift ;;
    --ipmi) IPMI=1; shift ;;
    --note) NOTE=$2; shift 2 ;;
    --seed) SEED=$2; shift 2 ;;
    --dry-run) DRY=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
    esac
done

[ -n "$OUT" ] || { usage >&2; exit 2; }
for v in DUR TRIALS SETTLE COOLDOWN TRACE_DUR; do
    [[ ${!v} =~ ^[0-9]+$ ]] || die "$v must be a non-negative integer"
done
[ "$DUR" -gt 0 ] && [ "$TRIALS" -gt 0 ] || die "-d and -n must be > 0"

if [ "$BASELINE" = 1 ]; then
    have=0
    for n in "${SC_NAME[@]}"; do [ "$n" = none ] && have=1; done
    if [ $have = 0 ]; then SC_NAME=(none "${SC_NAME[@]}"); SC_CMD=("" "${SC_CMD[@]}"); fi
fi
[ ${#SC_NAME[@]} -gt 0 ] || die "no scenarios"

# RAPL energy events that exist on this machine (servers lack energy-gpu etc.)
EVENTS=()
for e in energy-pkg energy-cores energy-ram; do
    [ -e /sys/bus/event_source/devices/power/events/$e ] && EVENTS+=("power/$e/")
done
EV=$(IFS=,; echo "${EVENTS[*]:-}")

if [ -z "${BENCH_WINDOW_CMD:-}" ] && [ "$DRY" = 0 ]; then
    [ "$(id -u)" = 0 ] || die "must run as root (perf -a, turbostat, bpftrace)"
    command -v perf >/dev/null || die "perf not found"
    [ -n "$EV" ] || die "no power/energy-* perf events (RAPL PMU missing)"
    [ "$TURBOSTAT" = 1 ] && ! command -v turbostat >/dev/null &&
        { echo "warning: turbostat not found, using perf only" >&2; TURBOSTAT=0; }
    [ "$TRACE" = 1 ] && ! command -v bpftrace >/dev/null && die "--trace needs bpftrace"
fi

now() { date +%s.%N; }

capture_env() {
    {
        echo "# bench.sh environment, $(date -Is)"
        echo "note: ${NOTE:-<none>}"
        [ -n "$NOTE" ] || echo "WARNING: no --note; record BIOS C-state settings by hand (not readable from the OS)"
        echo "window=${DUR}s trials=$TRIALS settle=${SETTLE}s cooldown=${COOLDOWN}s trace=$TRACE ipmi=$IPMI"
        echo "events=$EV"
        echo "scenarios:"
        for i in "${!SC_NAME[@]}"; do echo "  ${SC_NAME[$i]}: ${SC_CMD[$i]:-<idle baseline>}"; done
        echo
        for c in "uname -a" "cat /proc/cmdline" "lscpu" "perf --version" "turbostat --version" \
                 "bpftrace --version" "cpupower idle-info" "cpupower frequency-info -p" \
                 "dmidecode -s bios-vendor" "dmidecode -s bios-version" "dmidecode -s bios-release-date" \
                 "dmidecode -s processor-version" "ipmitool mc info"; do
            echo "## $c"
            command -v "${c%% *}" >/dev/null && $c 2>&1 || echo "(unavailable)"
            echo
        done
        echo "## cpuidle"
        echo "driver=$(cat $SYS/cpuidle/current_driver 2>&1) governor=$(cat $SYS/cpuidle/current_governor 2>&1)"
        for s in $SYS/cpu0/cpuidle/state*; do
            echo "$(basename "$s") name=$(<"$s/name") latency=$(<"$s/latency")us residency=$(<"$s/residency")us disable=$(<"$s/disable")"
        done
        for p in /sys/module/intel_idle/parameters/*; do [ -r "$p" ] && echo "intel_idle.$(basename "$p")=$(<"$p")"; done
        echo
        echo "## cpufreq / pstate"
        for p in $SYS/intel_pstate/status $SYS/intel_pstate/no_turbo $SYS/cpu0/cpufreq/scaling_governor \
                 $SYS/cpu0/cpufreq/energy_performance_preference; do
            [ -r "$p" ] && echo "$p=$(<"$p")"
        done
        echo
        echo "## RAPL limits"
        for p in /sys/class/powercap/intel-rapl:*/{name,max_energy_range_uj,constraint_*_power_limit_uw}; do
            [ -r "$p" ] && echo "$p=$(<"$p")"
        done
        echo "power PMU cpumask=$(cat /sys/bus/event_source/devices/power/cpumask 2>&1)"
    } > "$OUT/env.txt"
}

snap_idle() { # cpu state name usage time_us latency_us (tab separated)
    local d cpu st
    for d in $SYS/cpu[0-9]*/cpuidle/state[0-9]*; do
        cpu=${d#$SYS/cpu}; cpu=${cpu%%/*}; st=${d##*state}
        printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$cpu" "$st" "$(<"$d/name")" \
            "$(<"$d/usage")" "$(<"$d/time")" "$(<"$d/latency")"
    done > "$1"
}

snap_stat() { awk '$1=="intr"||$1=="ctxt"{print $1, $2}' /proc/stat > "$1"; }

descendants() { # $1 root pid -> root and all descendants (sudo/wrapper scripts safe)
    ps -e -o pid= -o ppid= | awk -v r="$1" '{p[$1]=$2}
        END{for(i in p){x=i; while((x in p) && x!=r && x>1) x=p[x]; if(x==r) print i}}'
}

snap_obs() { # $1 root pid, $2 outfile: one line per process: pid vol nonvol utime stime
    local pid=$1 out=$2 p s f v
    : > "$out"
    [ -n "$pid" ] || return 0
    for p in $(descendants "$pid"); do
        [ -r "/proc/$p/stat" ] || continue
        v=$(awk '/^voluntary_ctxt_switches/{a+=$2} /^nonvoluntary_ctxt_switches/{b+=$2}
                 END{print a+0, b+0}' /proc/$p/task/*/status 2>/dev/null) || continue
        s=$(<"/proc/$p/stat") || continue
        s=${s##*) }
        read -ra f <<< "$s"   # f[0]=state (field 3) -> utime/stime (fields 14/15) = f[11]/f[12]
        echo "$p $v ${f[11]:-0} ${f[12]:-0}" >> "$out"
    done
}

# Names of the processes under the observer, minus generic wrappers (for the trace pass).
obs_comms() {
    local p c
    for p in $(descendants "$1"); do
        c=$(<"/proc/$p/comm") 2>/dev/null || continue
        case $c in bash|sh|sudo|setsid|env|timeout|nohup|sleep|ps|awk) ;; *) echo "$c" ;; esac
    done | sort -u | paste -sd,
}

snap_ipmi() { [ "$IPMI" = 1 ] && command -v ipmitool >/dev/null && ipmitool dcmi power reading > "$1" 2>&1; }

OBS_PID="" OBS_COMM=""
start_observer() { # name cmd dir
    OBS_PID="" OBS_COMM=""
    [ -z "$2" ] && return 0
    setsid bash -c "exec $2" > "$3/obs.log" 2>&1 &
    OBS_PID=$!
    sleep 1
    kill -0 "$OBS_PID" 2>/dev/null || { echo "observer '$1' died; see $3/obs.log" >&2; OBS_PID=""; return 1; }
    OBS_COMM=$(obs_comms "$OBS_PID")
    [ -n "$OBS_COMM" ] || OBS_COMM=$(<"/proc/$OBS_PID/comm")
}
stop_observer() {
    [ -n "$OBS_PID" ] || return 0
    kill -TERM -- "-$OBS_PID" 2>/dev/null
    wait "$OBS_PID" 2>/dev/null
    OBS_PID=""
}
trap 'stop_observer' EXIT
trap 'stop_observer; exit 130' INT TERM

window_cmd() { # $1 dir
    if [ -n "${BENCH_WINDOW_CMD:-}" ]; then echo "$BENCH_WINDOW_CMD"; return; fi
    local perf="perf stat -a -x, -e $EV -o $1/perf.csv -- sleep $DUR"
    if [ "$TURBOSTAT" = 1 ]; then
        echo "turbostat --quiet --summary --out $1/turbostat.txt $perf"
    else
        echo "$perf"
    fi
}

trace_pass() { # $1 dir
    local prog='tracepoint:sched:sched_switch /args->prev_pid == 0/ { @idle_exit_total = count();'
    if [ -n "$OBS_COMM" ]; then
        local c cond=""
        for c in ${OBS_COMM//,/ }; do
            c=${c//[^A-Za-z0-9_.-]/}
            cond+="${cond:+ || }args->next_comm == \"$c\""
        done
        prog+=" if ($cond) { @idle_exit_obs = count(); }"
    fi
    prog+=' }'
    timeout -s INT "$TRACE_DUR" bpftrace -e "$prog" > "$1/trace.txt" 2>&1
}

run_one() { # trial pos name cmd
    local trial=$1 pos=$2 name=$3 cmd=$4
    local dir="$OUT/raw/$(printf '%03d' "$trial")_$name"
    mkdir -p "$dir"
    start_observer "$name" "$cmd" "$dir" || { stop_observer; return 1; }
    sleep "$SETTLE"

    snap_idle "$dir/pre.idle"; snap_stat "$dir/pre.stat"
    snap_obs "$OBS_PID" "$dir/pre.obs"; snap_ipmi "$dir/pre.ipmi"
    local t0 t1; t0=$(now)
    $(window_cmd "$dir") > "$dir/window.log" 2>&1
    t1=$(now)
    snap_idle "$dir/post.idle"; snap_stat "$dir/post.stat"
    snap_obs "$OBS_PID" "$dir/post.obs"; snap_ipmi "$dir/post.ipmi"

    if [ -z "${BENCH_WINDOW_CMD:-}" ]; then
        [ -s "$dir/perf.csv" ] || { echo "WARNING: no perf.csv (RAPL energy lost) in $dir; window.log:" >&2; tail -n 5 "$dir/window.log" >&2; }
        [ "$TURBOSTAT" = 1 ] && { [ -s "$dir/turbostat.txt" ] || { echo "WARNING: no turbostat.txt in $dir; window.log:" >&2; tail -n 5 "$dir/window.log" >&2; }; }
    fi
    if [ -n "$cmd" ] && [ ! -s "$dir/post.obs" ]; then
        echo "WARNING: observer '$name' was not running at the end of the window; see $dir/obs.log" >&2
    fi

    local tdur=0
    if [ "$TRACE" = 1 ] && [ -z "${BENCH_WINDOW_CMD:-}" ]; then trace_pass "$dir"; tdur=$TRACE_DUR; fi

    {
        echo "scenario=$name"; echo "trial=$trial"; echo "pos=$pos"
        echo "dur=$DUR"; echo "t0=$t0"; echo "t1=$t1"; echo "trace_dur=$tdur"
        echo "obs_pid=$OBS_PID"; echo "obs_comm=$OBS_COMM"; echo "obs_nproc=$(wc -l < "$dir/pre.obs")"; echo "cmd=$cmd"
    } > "$dir/meta"
    stop_observer
    sleep "$COOLDOWN"
}

shuffle() { if [ -n "$SEED" ]; then shuf --random-source=<(yes "$SEED-$1"); else shuf; fi; }

mkdir -p "$OUT/raw"
capture_env

if [ "$DRY" = 1 ]; then
    echo "DRY RUN. Environment written to $OUT/env.txt"
    echo "window command: $(window_cmd "$OUT/raw/<run>")"
    total=$(( TRIALS * ${#SC_NAME[@]} * (DUR + SETTLE + COOLDOWN + 1) ))
    for i in "${!SC_NAME[@]}"; do echo "scenario ${SC_NAME[$i]}: ${SC_CMD[$i]:-<idle baseline>}"; done
    echo "approx. total time: $(( total / 60 )) min (+ trace passes)"
    exit 0
fi

for trial in $(seq 1 "$TRIALS"); do
    mapfile -t order < <(printf '%s\n' "${!SC_NAME[@]}" | shuffle "$trial")
    pos=0
    for i in "${order[@]}"; do
        pos=$((pos + 1))
        echo "[trial $trial/$TRIALS] ${SC_NAME[$i]} (pos $pos)"
        run_one "$trial" "$pos" "${SC_NAME[$i]}" "${SC_CMD[$i]}"
    done
done
echo "done. Aggregate with: $(dirname "$0")/aggregate.py $OUT"
