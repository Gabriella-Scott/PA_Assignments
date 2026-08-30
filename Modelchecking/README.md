# RW796 — Model Checking Assignment

SPIN/Promela models and verification of two concurrent systems.

* **Program 1** — the multicore process scheduler from RW314 Project 1 (`manager.c`)
* **Program 2** — Othello MPI engine (`my_player.c`)

## Requirements

* SPIN 6.5+ (`spin -V`)
* a C compiler (`gcc`)

## Layout

```
scheduler/
    model.pml           Promela model with all LTL claims embedded
    pan, pan_noclaim    compiled verifier binaries (generated)
    pan.c / pan.[bhmt]  generated verifier source (generated)
    model.pml.trail     last counterexample trail (generated)
    results/            saved trails and run notes
        run_commands.md     full reproduction log with commands and output
        pcb_mutex.trail
        no_self_wait.trail
        waiting_consistent.trail
        no_dup_ready (holds — no trail)
        all_terminate_fair.trail
        ready_dispatched_fair.trail
        deadlock.trail
othello/
    model.pml           Promela model of the MPI master/worker protocol
    results/            saved trails (in progress)
report/
    Report.tex          LaTeX source
```

## Model configuration

### Scheduler (`scheduler/model.pml`)

| constant | value | meaning |
|---|---|---|
| `NUM_PROCS` | 3 | simulated processes |
| `NUM_RESOURCES` | 2 | shared resources |
| `NUM_WORKERS` | 2 | scheduler threads ("cores") |
| `MAX_INSTR` | 3 | instruction slots per process (override with `-DMAX_INSTR=N`) |

### Othello (`othello/model.pml`)

| constant | value | meaning |
|---|---|---|
| `NUM_WORKERS` | 3 | MPI worker ranks (rank 0 is master) |
| `MAX_MOVES` | 3 | moves considered per round |
| `MAX_ROUNDS` | 2 | game rounds modelled |

## Building the verifier

Two binaries are built from the same generated `pan.c`.

```bash
# Standard verifier — used for all LTL property checks
spin -a model.pml
gcc -O2 -o pan pan.c

# Claim-free verifier — re-enables the invalid end state (deadlock) check
# (pan disables that check whenever a never claim is compiled in)
gcc -O2 -DNOCLAIM -o pan_noclaim pan.c
```

## Running

`spin -a` compiles all `ltl` blocks in the model into `pan.c`; select one per
run with `-N`:

```bash
./pan -a -N pcb_mutex
./pan -a -N no_self_wait
./pan -a -N waiting_consistent
./pan -a -N no_dup_ready
./pan -a -f -N all_terminate      # -f for weak fairness
./pan -a -f -N ready_dispatched

./pan_noclaim                      # deadlock / invalid end state check
```

Each `./pan` run overwrites `model.pml.trail`. Copy it before the next run:

```bash
./pan -a -N waiting_consistent
cp model.pml.trail results/waiting_consistent.trail
```

To replay a saved trail:

```bash
cp results/waiting_consistent.trail model.pml.trail
spin -t -p model.pml
spin -t -p -c model.pml            # column format: one column per process
```

If Spin warns that `model.pml` is newer than `model.pml.trail`, the trail
belongs to an earlier version of the model and must not be used.

## Flags

| flag | meaning |
|---|---|
| `-a`   | search for acceptance cycles (required for liveness claims) |
| `-f`   | weak fairness |
| `-N n` | select never claim `n` |
| `-DNOCLAIM` | compile out never claims to re-enable deadlock detection |
| `-DMAX_INSTR=N` | override `MAX_INSTR` at parse time without editing the model |

## Scheduler results summary

| property | category | config | result |
|---|---|---|---|
| `pcb_mutex` | safety, mutual exclusion | `MAX_INSTR 3` | **violated** — depth 901, 65,957 states |
| `no_self_wait` | safety, resource ownership | `MAX_INSTR 3` | **violated** — depth 379, 188 states |
| `waiting_consistent` | safety, state consistency | `MAX_INSTR 3` | **violated** — depth 962, 69,699 states |
| `no_dup_ready` | safety, queue integrity | `MAX_INSTR 2` | **holds** — 20,861,451 states, 0 errors |
| `all_terminate` | liveness | `MAX_INSTR 3`, `-f` | **violated** — depth 1069, 493 states |
| `ready_dispatched` | liveness | `MAX_INSTR 3`, `-f` | **violated** — depth 1088, 11,372,991 states |
| deadlock | safety, invalid end state | `MAX_INSTR 3` | **violated** — depth 504, 67,872 states |

See `scheduler/results/run_commands.md` for full reproduction details.