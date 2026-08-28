# RW796 — Model Checking Assignment

SPIN/Promela models and verification of two concurrent systems.

* **Program 1** — the multicore process scheduler from RW314 Project 1 (`manager.c`)
* **Program 2** — *(to be added)*

## Requirements

* SPIN 6.5+ (`spin -V`)
* a C compiler (`gcc`)
* GNU make

## Layout

```
models/         Promela models (one per program / variant)
properties/     LTL claims, kept out of the model so that pan's
                invalid-end-state check can be enabled separately
scripts/        reproduction driver
results/        captured SPIN output quoted in the report
report/         the report source
build/          generated (pan.c, pan, trails) — gitignored
```

## Reproducing the results

```
make safety MODEL=sched_fcfs                 # assertions + invalid end states
make ltl    MODEL=sched_fcfs P=p_mutual_dispatch
make trail  MODEL=sched_fcfs                 # replay last counterexample
bash scripts/run_all.sh                      # everything -> results/
```

### Why two builds

`pan` disables its invalid-end-state (deadlock) check whenever a never claim is
compiled in. The `safety` target therefore builds from the bare model with no
claim; the `ltl` target concatenates `models/*.pml` with `properties/*.ltl` and
selects one claim with `-N`.

### Flags used

| flag | meaning |
|---|---|
| `-a`   | search for acceptance cycles (needed for liveness claims) |
| `-f`   | weak fairness |
| `-N n` | select never claim `n` |
| `-m N` | maximum search depth |
| `-w N` | hash table size 2^N |
| `-DCOLLAPSE` | state compression, for the larger scenarios |

## Model configuration

`models/sched_fcfs.pml` is parameterised at the top:

| constant | value | meaning |
|---|---|---|
| `NP` | 3 | simulated processes |
| `NR` | 2 | resources that exist |
| `NT` | 2 | scheduler threads ("cores") |
| `NI` | 4 | instruction slots per process |

Prepend `#define SCEN_DEADLOCK` to select the circular-wait workload instead of
the default contention workload. That scenario exceeds 5x10^7 states with
`NP=3`; run it with `NP=2` or with `-DCOLLAPSE -w28`.

## Variants

| file | description |
|---|---|
| `sched_fcfs.pml`     | faithful model of `schedule_fcfs()` as written |
| `sched_fcfs_fix.pml` | with the proposed fix: the thread tracks the outcome of an instruction in a **local** variable and drops the PCB pointer at hand-off, instead of re-reading `pcb->state` after the PCB may already belong to another core |

## Results summary

| property | `sched_fcfs` | `sched_fcfs_fix` |
|---|---|---|
| safety (assertions, invalid end states) | **fails** — `qlen[TERMQ]` overflows: a PCB is enqueued on the terminated queue twice | holds (1 163 685 states) |
| `p_mutual_dispatch` | **fails** — two threads hold the same PCB | holds |
| `p_no_premature_stop` | **fails** (consequence of the above) | holds |
| `p_termination` (with `-f`) | **fails** | **fails** — see report: not provable under weak fairness with non-queueing mutexes |

## Running
spin -a model.pml
gcc -o pan pan.c
./pan -a -N pcb_mutex