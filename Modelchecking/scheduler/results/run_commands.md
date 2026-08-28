# Verification runs: FCFS scheduler

Environment: Spin Version 6.5.2 (6 December 2019), gcc on Ubuntu.
Model configuration: NUM_PROCS 3, NUM_WORKERS 2, NUM_RESOURCES 2, MAX_INSTR 3.

Build step for every run:

    spin -a model.pml
    gcc -o pan pan.c

The gcc sprintf warnings come from SPIN's generated code and can be ignored.

## Property: pcb_mutex (LTL, safety)

    ltl pcb_mutex { [] one_worker_per_pcb }

Model version: commit 7e4f991

    ./pan -a -N pcb_mutex

Result: errors 1. Violation at depth 1450. 68324 states stored,
20355 matched, 0.12 s.

Replay:

    spin -t -p model.pml

## Property: instruction pointer stays in bounds (assertion)

    assert(instr_count[pcb_id] <= MAX_INSTR)

Model version: commit 7e4f991, with the ltl block commented out so that
pan checks assertions and invalid end states rather than the never claim.

    ./pan

Result: errors 1. Assertion violated at depth 784. 72250 states stored.

## Note on running the deadlock check

While an ltl block is present in model.pml, every pan run uses it as a
never claim and the invalid end state (deadlock) check is disabled. To
check for deadlock, comment out the ltl block and run ./pan with no flags.