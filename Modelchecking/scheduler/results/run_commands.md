# Verification runs: FCFS scheduler (Program 1)

This file records the verification runs for the FCFS scheduler model and gives enough information to reproduce the results from a clean checkout.

## Environment

| Item          | Value                               |
| ------------- | ----------------------------------- |
| Model checker | Spin Version 6.5.2, 6 December 2019 |
| Compiler      | gcc                                 |
| Platform      | Ubuntu Linux                        |
| Memory        | 7 GB RAM, 1 GB swap                 |
| Model file    | `scheduler/model.pml`               |
| Repository    | `PA_Assignments/Modelchecking`      |
| Model version | commit `517e9be`                    |

All results were produced from the committed `model.pml` at this commit. No local changes were made between runs. Different `MAX_INSTR` values were passed on the command line so the same model file could be used throughout.

## Model configuration

The default values in `model.pml` are:

```
#define NUM_PROCS     3
#define NUM_WORKERS   2
#define NUM_RESOURCES 2
#ifndef MAX_INSTR
#define MAX_INSTR 3
#endif
```

`MAX_INSTR` can be changed without editing the model:

```
spin -a -DMAX_INSTR=2 model.pml
```

`MAX_INSTR 3` was used for the failing properties. `MAX_INSTR 2` was used for `no_dup_ready`, since this property requires an exhaustive search and the larger configuration is too large for the available memory.

## Build steps

The standard verifier was generated with:

```
spin -a model.pml
gcc -O2 -o pan pan.c
```

The model contains six LTL claims, so each run specifies the property using `-N`. If `-N` is omitted, Spin uses the default claim.

For the deadlock check, a claim-free verifier was built:

```
gcc -O2 -DNOCLAIM -o pan_noclaim pan.c
```

This enables Spin's invalid end state check while keeping the LTL properties in the model.

`gcc` reports some `sprintf` overflow warnings from Spin's generated `pan.c`. These warnings are from the generated verifier and do not come from the model.

## Summary of results

| Property             | Category                   | Configuration     | Result                                  |
| -------------------- | -------------------------- | ----------------- | --------------------------------------- |
| `pcb_mutex`          | safety, mutual exclusion   | MAX_INSTR 3       | violated, depth 901, 65,957 states      |
| `no_self_wait`       | safety, resource ownership | MAX_INSTR 3       | violated, depth 379, 188 states         |
| `waiting_consistent` | safety, state consistency  | MAX_INSTR 3       | violated, depth 962, 69,699 states      |
| `no_dup_ready`       | safety, queue integrity    | MAX_INSTR 2       | holds, 20,861,451 states, 0 errors      |
| `all_terminate`      | liveness                   | MAX_INSTR 3, `-f` | violated, depth 1069, 493 states        |
| `ready_dispatched`   | liveness                   | MAX_INSTR 3, `-f` | violated, depth 1088, 11,372,991 states |
| deadlock             | safety, invalid end state  | MAX_INSTR 3       | violated, depth 504, 67,872 states      |

## Saving and replaying trails

Each run overwrites `model.pml.trail`, so the trails used in the report were copied immediately after each run:

```
./pan -a -N no_self_wait
cp model.pml.trail results/no_self_wait.trail
```

To replay a saved trail:

```
spin -t -p model.pml
```

For a column-based view of the process interleaving:

```
spin -t -p -c model.pml
```

If Spin reports that `model.pml` is newer than the trail, the trail belongs to an older model version and should not be used.

---

## Property: `pcb_mutex`

Category: safety, mutual exclusion.

```
#define one_worker_per_pcb (executing[1] <= 1 && executing[2] <= 1 && executing[3] <= 1)

ltl pcb_mutex { [] one_worker_per_pcb }
```

Command:

```
./pan -a -N pcb_mutex
```

Result:

```
errors: 1
claim violated at depth 901
depth reached 1940
65,957 states stored
```

Trail: `results/pcb_mutex.trail`

Interpretation: `executing[]` reaches 2 for one PCB, meaning two workers execute the same PCB at the same time. This shows the double dispatch race in `schedule_fcfs`.

---

## Property: `no_self_wait`

Category: safety, resource ownership.

```
#define no_self_wait_p ( \
    (waiting_for[1] == 0 || resource_owner[waiting_for[1]] != 1) && \
    (waiting_for[2] == 0 || resource_owner[waiting_for[2]] != 2) && \
    (waiting_for[3] == 0 || resource_owner[waiting_for[3]] != 3))

ltl no_self_wait { [] no_self_wait_p }
```

Command:

```
./pan -a -N no_self_wait
```

Result:

```
errors: 1
claim violated at depth 379
depth reached 379
188 states stored
```

Trail: `results/no_self_wait.trail`

Interpretation: this is the shortest counterexample. Only one worker is involved, so it is not a race. The problem is in `request_resource`, which checks whether a resource is free but does not check whether the requester already owns it.

---

## Property: `waiting_consistent`

Category: safety, state consistency.

```
#define waiting_consistent_p ( \
    (pcb_state[1] != WAITING || waiting_for[1] != 0) && \
    (pcb_state[2] != WAITING || waiting_for[2] != 0) && \
    (pcb_state[3] != WAITING || waiting_for[3] != 0))

ltl waiting_consistent { [] waiting_consistent_p }
```

Command:

```
./pan -a -N waiting_consistent
```

Result:

```
errors: 1
claim violated at depth 962
depth reached 1940
69,699 states stored
```

Trail: `results/waiting_consistent.trail`

Interpretation: the counterexample shows a PCB that is marked `WAITING`, is in `waiting_q`, and still owns a resource. Since it is waiting, it is not dispatched again, so its resource is never released. This is another effect of the double dispatch race.

---

## Property: `no_dup_ready`

Category: safety, queue integrity. This is the only property that holds.

```
#define no_dup_ready_p (in_ready[1] <= 1 && in_ready[2] <= 1 && in_ready[3] <= 1)

ltl no_dup_ready { [] no_dup_ready_p }
```

`in_ready[]` is used only for verification. It counts how many times each PCB currently appears in the ready queue.

Command:

```
spin -a -DMAX_INSTR=2 model.pml
gcc -O2 -o pan pan.c
./pan -a -N no_dup_ready -m20000 -w26
```

Result:

```
errors: 0
depth reached 2991
20,861,451 states stored
7,749,419 states matched
2,421 MB total actual memory usage
approximately 21 seconds
```

Coverage:

```
unreached in init
        (0 of 12 states)

unreached in proctype Worker
        (0 of 119 states)
```

`-m20000` increases the maximum search depth, while `-w26` provides a larger hash table for the large state space.

Interpretation: no reachable state contains the same PCB in the ready queue more than once. The coverage result also shows that all control points in `init` and `Worker` were reached.

---

## Property: `all_terminate`

Category: liveness.

```
#define all_done (pcb_state[1] == TERMINATED && pcb_state[2] == TERMINATED && pcb_state[3] == TERMINATED)

ltl all_terminate { <> all_done }
```

Runs:

```
./pan -a -N all_terminate -m50000
./pan -a -N all_terminate -f -m50000
```

Without fairness:

```
errors: 1
acceptance cycle at depth 14
25 states stored
```

With weak fairness:

```
errors: 1
acceptance cycle at depth 1069
493 states stored
```

Trail: `results/all_terminate_fair.trail`

The 25-state result without fairness is a modelling artefact. `init` can be ignored indefinitely, meaning `ready_q` is never populated and a worker waits forever. Weak fairness removes this behaviour and makes the liveness result meaningful.

The fair counterexample shows all three processes in `WAITING`. Process 1 owns resource 1 while also waiting for resource 1. Both workers then exit because `check_terminate` sees all processes as blocked. This is caused by the self-wait bug rather than unfair scheduling.

---

## Property: `ready_dispatched`

Category: liveness, ready queue responsiveness.

```
ltl ready_dispatched { [] ((pcb_state[1] == READY) -> <> (pcb_state[1] == RUNNING)) }
```

Only process 1 is checked because the three processes are symmetric.

Runs:

```
./pan -a -N ready_dispatched -m50000
./pan -a -N ready_dispatched -f -m50000 -w26
```

Without fairness:

```
errors: 1
acceptance cycle at depth 14
25 states stored
```

With weak fairness:

```
errors: 1
acceptance cycle at depth 1088
depth reached 2825
11,372,991 states stored
1,642 MB total actual memory usage
approximately 12 seconds
```

Trail: `results/ready_dispatched_fair.trail`

The unfair result is caused by the same scheduling artefact described above. The fair result shows that a ready process can remain undispatched because `check_terminate` can observe every process as blocked.

Fairness greatly increases the search cost, from 25 states without `-f` to over 11 million with it.

---

## Deadlock check: invalid end states

Category: safety.

The claim-free verifier was used so that invalid end states are checked:

```
spin -a model.pml
gcc -O2 -DNOCLAIM -o pan_noclaim pan.c
./pan_noclaim
```

Result:

```
errors: 1
invalid end state at depth 504
depth reached 983
67,872 states stored
```

Trail: `results/deadlock.trail`

The counterexample reaches `instr_count[2] = 4` while `MAX_INSTR = 3`. The worker is then unable to execute either branch of the instruction selection.

This is caused by two workers executing the same PCB and both advancing:

```
running_p->next_instruction = running_p->next_instruction->next;
```

The instruction pointer is therefore moved beyond the end of the linked list. The deadlock is another consequence of the `pcb_mutex` race.

---

## State space and reduced configuration

Failing properties stop when a counterexample is found, but `no_dup_ready` must explore the complete reachable state space. The state space grows quickly with `MAX_INSTR`:

| MAX_INSTR | Reachable states     |
| --------- | -------------------- |
| 1         | 420,705              |
| 2         | 20,861,451           |
| 3         | approximately $10^9$ |

`MAX_INSTR 3` would require roughly 100 GB based on the reported 96 bytes per state, which is much more than the available 7 GB. Therefore, `no_dup_ready` was verified using `MAX_INSTR 2`.

This reduction is reasonable for this property because a duplicate ready queue entry requires a PCB to be enqueued twice before being dequeued once. The shortest blocking and wake-up sequence fits within two instructions.

This is still a bounded result. It shows that no duplicate occurs for three processes, two workers, two resources and two instructions per process. It does not prove the property for larger configurations.

---

## Reproducing the results

From a clean checkout at the commit specified above:

```
cd Modelchecking/scheduler

# default configuration, MAX_INSTR 3

spin -a model.pml
gcc -O2 -o pan pan.c
gcc -O2 -DNOCLAIM -o pan_noclaim pan.c

./pan -a -N pcb_mutex
./pan -a -N no_self_wait
./pan -a -N waiting_consistent
./pan -a -N all_terminate -m50000
./pan -a -N all_terminate -f -m50000
./pan -a -N ready_dispatched -m50000
./pan -a -N ready_dispatched -f -m50000 -w26
./pan_noclaim

# reduced configuration, MAX_INSTR 2

spin -a -DMAX_INSTR=2 model.pml
gcc -O2 -o pan pan.c
./pan -a -N no_dup_ready -m20000 -w26
```

iSpin was not used. All verification runs were performed from the command line using the commands above.
