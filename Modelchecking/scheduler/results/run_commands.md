# Verification runs: FCFS scheduler (Program 1)

This file records every verification run reported for the FCFS scheduler
model, in enough detail to reproduce each result from a clean checkout.

## Environment
| Item | Value |
| --- | --- |
| Model checker | Spin Version 6.5.2, 6 December 2019 |
| Compiler | gcc |
| Platform | Ubuntu Linux |
| Memory | 7 GB RAM, 1 GB swap |
| Model file | `scheduler/model.pml` |
| Repository | `PA_Assignments/Modelchecking` |
| Model version | commit `517e9be` |

All results below were produced from the single committed `model.pml` at the
commit above. No local edits to the model were made between runs. Where a
run uses a different configuration, that configuration is supplied on the
command line rather than by editing the file, so that every result is
reproducible from the same source.

## Model configuration

The defaults compiled into `model.pml` are:

    #define NUM_PROCS     3
    #define NUM_WORKERS   2
    #define NUM_RESOURCES 2
    #ifndef MAX_INSTR
    #define MAX_INSTR 3
    #endif

`MAX_INSTR` is wrapped in an include guard so that it can be overridden at
parse time without changing the file:

    spin -a -DMAX_INSTR=2 model.pml

Spin passes preprocessor definitions given before the model file through to
the C preprocessor, so this changes the bound without any edit to the
committed model.

`MAX_INSTR 3` is the default configuration and is used for every property
that fails. `MAX_INSTR 2` is used for the single property that holds, for
the reason given in the section "State space and the reduced configuration"
below.

## Build steps

Two verifier binaries are built from the same generated `pan.c`.

### Standard verifier, used for all LTL properties

    spin -a model.pml
    gcc -O2 -o pan pan.c

`spin -a` prints the list of never claims found in the model and warns that
only one claim is used per run:

    the model contains 6 never claims: ready_dispatched, all_terminate,
    no_dup_ready, waiting_consistent, no_self_wait, pcb_mutex
    only one claim is used in a verification run
    choose which one with ./pan -a -N name (defaults to -N pcb_mutex)

Every LTL run below therefore names its property explicitly with `-N`. If
`-N` is omitted, pan silently uses the first claim in the file. The header
line `pan: ltl formula <name>` at the top of each run confirms which claim
was actually used.

### Claim-free verifier, used for the deadlock check

    gcc -O2 -DNOCLAIM -o pan_noclaim pan.c

While a never claim is active, pan disables the invalid end state check. The
`-DNOCLAIM` compile-time directive excludes the claim from the generated
verifier, which re-enables that check without removing the `ltl` blocks from
`model.pml`. This keeps one committed model file for every result.

### Note on compiler warnings

`gcc` emits warnings about possible `sprintf` overflow in `pan.c`. These come
from Spin's own generated code, not from the model, and can be ignored.

## Summary of results

| Property | Category | Configuration | Result |
| --- | --- | --- | --- |
| `pcb_mutex` | safety, mutual exclusion | MAX_INSTR 3 | violated, depth 901, 65,957 states |
| `no_self_wait` | safety, resource ownership | MAX_INSTR 3 | violated, depth 379, 188 states |
| `waiting_consistent` | safety, state consistency | MAX_INSTR 3 | violated, depth 962, 69,699 states |
| `no_dup_ready` | safety, queue integrity | MAX_INSTR 2 | holds, 20,861,451 states, 0 errors |
| `all_terminate` | liveness | MAX_INSTR 3, `-f` | violated, depth 1069, 493 states |
| `ready_dispatched` | liveness | MAX_INSTR 3, `-f` | violated, depth 1088, 11,372,991 states |
| deadlock | safety, invalid end state | MAX_INSTR 3 | violated, depth 504, 67,872 states |

## Saving and replaying error trails

Each verification run overwrites `model.pml.trail`, so trails that are
referenced in the report are copied into `results/` immediately after the run
that produced them:

    ./pan -a -N no_self_wait
    cp model.pml.trail results/no_self_wait.trail

To replay a saved trail, copy it back to `model.pml.trail` first and then:

    spin -t -p model.pml

Adding `-c` prints the run in column format, one column per process, which
makes the interleaving between the two workers easier to follow:

    spin -t -p -c model.pml

If Spin reports that `model.pml` is newer than `model.pml.trail`, the trail
belongs to an earlier version of the model and must not be used.

---

## Property: pcb_mutex

Category: safety, mutual exclusion.

    #define one_worker_per_pcb (executing[1] <= 1 && executing[2] <= 1 && executing[3] <= 1)
    ltl pcb_mutex { [] one_worker_per_pcb }

Command:

    spin -a model.pml
    gcc -O2 -o pan pan.c
    ./pan -a -N pcb_mutex

Result:

    errors: 1
    claim violated at depth 901
    depth reached 1940
    65,957 states stored

Trail: `results/pcb_mutex.trail`

Interpretation: `executing[]` reaches 2 for a single PCB, so two workers are
inside the instruction loop for the same process control block at the same
time. This is the double dispatch race in `schedule_fcfs`.

---

## Property: no_self_wait

Category: safety, resource ownership.

    #define no_self_wait_p ( \
        (waiting_for[1] == 0 || resource_owner[waiting_for[1]] != 1) && \
        (waiting_for[2] == 0 || resource_owner[waiting_for[2]] != 2) && \
        (waiting_for[3] == 0 || resource_owner[waiting_for[3]] != 3))
    ltl no_self_wait { [] no_self_wait_p }

Command:

    ./pan -a -N no_self_wait

Result:

    errors: 1
    claim violated at depth 379
    depth reached 379
    188 states stored

Trail: `results/no_self_wait.trail`

Interpretation: the shortest counterexample in the set. A single worker is
involved and `executing[]` never exceeds 1, so this is not a race. It is a
logic error in `request_resource`, which tests only whether a resource is
free and never whether the requester already owns it.

---

## Property: waiting_consistent

Category: safety, state consistency.

    #define waiting_consistent_p ( \
        (pcb_state[1] != WAITING || waiting_for[1] != 0) && \
        (pcb_state[2] != WAITING || waiting_for[2] != 0) && \
        (pcb_state[3] != WAITING || waiting_for[3] != 0))
    ltl waiting_consistent { [] waiting_consistent_p }

Command:

    ./pan -a -N waiting_consistent

Result:

    errors: 1
    claim violated at depth 962
    depth reached 1940
    69,699 states stored

Trail: `results/waiting_consistent.trail`

Interpretation: a second symptom of the double dispatch race. The
counterexample ends with a PCB simultaneously marked WAITING, queued on
`waiting_q`, and owning a resource. Because it is on the waiting queue it is
never dispatched again, so the resource it holds is never released.

---

## Property: no_dup_ready

Category: safety, queue integrity. This is the only property in the set that
holds.

    #define no_dup_ready_p (in_ready[1] <= 1 && in_ready[2] <= 1 && in_ready[3] <= 1)
    ltl no_dup_ready { [] no_dup_ready_p }

`in_ready[]` is an auxiliary variable added for verification only. It has no
counterpart in `manager.c`. It is incremented at every send to `ready_q` and
decremented at the dequeue, so its value is the number of times a PCB id is
currently present in the ready queue.

Command, run at the reduced bound:

    spin -a -DMAX_INSTR=2 model.pml
    gcc -O2 -o pan pan.c
    ./pan -a -N no_dup_ready -m20000 -w26

Result:

    errors: 0
    depth reached 2991
    20,861,451 states stored
    7,749,419 states matched
    2,421 MB total actual memory usage
    approximately 21 seconds

Statement coverage reported at the end of the run:

    unreached in init
            (0 of 12 states)
    unreached in proctype Worker
            (0 of 119 states)

Flags used and why:

- `-m20000` raises the maximum search depth above the default of 10,000. The
  search reaches depth 2991, but the default stack is also used for the
  nested depth-first search, so the higher limit avoids a truncated search.
- `-w26` sets the hash table to 2^26 entries. The default in exhaustive
  search mode is 2^19, which is far too small for a state space of this size
  and produces a large number of hash conflicts.

Interpretation: no reachable state has the same PCB present in the ready
queue twice. Zero unreached statements in both `init` and `Worker` means the
search covered every control point of the model, so the result is not the
consequence of dead code in the abstraction.

---

## Property: all_terminate

Category: liveness.

    #define all_done (pcb_state[1] == TERMINATED && pcb_state[2] == TERMINATED && pcb_state[3] == TERMINATED)
    ltl all_terminate { <> all_done }

Run twice, first without fairness and then with weak fairness.

    ./pan -a -N all_terminate -m50000
    ./pan -a -N all_terminate -f -m50000

Result without `-f`:

    errors: 1
    acceptance cycle at depth 14
    25 states stored

Result with `-f`:

    errors: 1
    acceptance cycle at depth 1069
    493 states stored

Trail: `results/all_terminate_fair.trail`

Interpretation: the run without fairness is not evidence of a defect. Its
cycle is one in which `init` evaluates its loop guard and is then never
scheduled again, so `ready_q` is never seeded and a worker spins forever on
an empty queue. No OpenMP runtime behaves this way. Weak fairness excludes
cycles in which a process that could progress is passed over forever, and is
required for any liveness result here to be meaningful.

The fair counterexample ends with all three processes WAITING, process 1
holding resource 1 while also waiting for resource 1, and both workers
exited because `check_terminate` sees every process blocked. The cycle is
not a genuine circular wait: process 1 is at its head and already holds what
it is waiting for.

---

## Property: ready_dispatched

Category: liveness, responsiveness of the ready queue.

    ltl ready_dispatched { [] ((pcb_state[1] == READY) -> <> (pcb_state[1] == RUNNING)) }

The property is stated for process 1 only. The three processes are symmetric
in the model, since none of them has a distinguishing attribute, so a
per-process instance is representative and keeps the never claim small.

    ./pan -a -N ready_dispatched -m50000
    ./pan -a -N ready_dispatched -f -m50000 -w26

Result without `-f`:

    errors: 1
    acceptance cycle at depth 14
    25 states stored

Result with `-f`:

    errors: 1
    acceptance cycle at depth 1088
    depth reached 2825
    11,372,991 states stored
    1,642 MB total actual memory usage
    approximately 12 seconds

Trail: `results/ready_dispatched_fair.trail`

Interpretation: the unfair result is the same modelling artefact described
above. The fair result shows that a process can be left on or returned to
the ready path and never dispatched, because the system halts once
`check_terminate` observes every process blocked.

Note the cost of fairness. The same property without `-f` completes in 25
states; with `-f` it explores over eleven million. This matches the warning
in the course material that fairness slows the search.

---

## Deadlock check: invalid end states

Category: safety. Verified with the claim-free binary so that the invalid end
state check is enabled.

    spin -a model.pml
    gcc -O2 -DNOCLAIM -o pan_noclaim pan.c
    ./pan_noclaim

The header confirms the configuration:

    never claim             - (not selected)
    assertion violations    +
    acceptance   cycles     - (not selected)
    invalid end states      +

Result:

    errors: 1
    invalid end state at depth 504
    depth reached 983
    67,872 states stored

Trail: `results/deadlock.trail`

Interpretation: the final state of the trail shows `instr_count[2] = 4` with
`MAX_INSTR` set to 3. One worker is blocked at the selection statement that
branches on `instr_count`, because neither guard is executable once the
counter has passed the bound. In `manager.c` this corresponds to two threads
both executing

    running_p->next_instruction = running_p->next_instruction->next;

for the same PCB, which advances the instruction pointer past the end of the
linked list. Modelling that as a blocked control point is faithful: the
implementation has no defined behaviour at that point either.

---

## State space and the reduced configuration

Properties that fail are cheap to check, because the search stops at the
first counterexample. The property that holds must exhaust the reachable
state space. That space grows steeply with `MAX_INSTR`:

| MAX_INSTR | reachable states |
| --- | --- |
| 1 | 420,705 |
| 2 | 20,861,451 |
| 3 | approximately 10^9, extrapolated |

Measured with:

    spin -a -DMAX_INSTR=1 model.pml && gcc -O2 -o pan pan.c && ./pan -a -N no_dup_ready -m20000
    spin -a -DMAX_INSTR=2 model.pml && gcc -O2 -o pan pan.c && ./pan -a -N no_dup_ready -m20000 -w26

The growth factor is close to 50 per additional instruction. At `MAX_INSTR 3`
the state space is on the order of a billion states, which at the 96 bytes
per state reported by pan is roughly 100 GB. That is far beyond the 7 GB
available, so `no_dup_ready` is verified at `MAX_INSTR 2`.

Justification for the reduction, specific to this property: a duplicate ready
queue entry requires a PCB to be enqueued twice before it is dequeued once,
which requires one blocking request and one wakeup. That is two instructions,
so the shortest possible witness fits within `MAX_INSTR 2`. Raising the bound
to 3 would only admit longer instances of the same shape.

This is a bounded argument and not a proof. The result establishes that no
duplicate arises within three processes, two workers, two resources and two
instructions per process. It does not establish absence for larger
configurations.

---

## Reproducing every result

From a clean checkout of the repository at the commit recorded at the top of
this file:

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

    # reduced configuration, MAX_INSTR 2, for the property that holds
    spin -a -DMAX_INSTR=2 model.pml
    gcc -O2 -o pan pan.c
    ./pan -a -N no_dup_ready -m20000 -w26

iSpin was not used for any of the results reported here. All runs were
performed from the command line with the commands given above.