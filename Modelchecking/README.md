# RW796 - Model Checking Assignment

SPIN/Promela models and verification of two concurrent systems.

* **Program 1** - the multicore process scheduler from RW314 Project 1 (`manager.c`)
* **Program 2** - Othello MPI engine (`my_player.c`)

Each folder contains:
```
model.pml               the Promela model, all LTL properties included
properties.md           the properties and why each one matters
results/run_commands.md every command, flag and result, with commit hash
  ```

othello/verify.sh regenerates every Othello result in one run.
The scheduler results are reproduced by the commands in its run_commands.md.
iSpin was not used for any result.

Implementation repositories are linked in the report.

## Requirements

* SPIN 6.5+ (`spin -V`)
* a C compiler (`gcc`)

## Building the verifier

Two binaries are built from the same generated `pan.c`.

```bash
# Standard verifier - used for all LTL property checks
spin -a model.pml
gcc -O2 -o pan pan.c

# Claim-free verifier - re-enables the invalid end state (deadlock) check
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

./pan_noclaim  # deadlock / invalid end state check
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
spin -t -p -c model.pml  # column format: one column per process
```

If Spin warns that `model.pml` is newer than `model.pml.trail`, the trail
belongs to an earlier version of the model and must not be used.