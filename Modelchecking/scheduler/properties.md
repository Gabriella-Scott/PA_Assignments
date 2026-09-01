# Correctness properties: FCFS process scheduler (Program 1)

This file specifies the correctness properties verified for the Promela model
in `scheduler/model.pml`. For each property it gives the formula, the model
state it refers to, what that state corresponds to in `manager.c`, and why
the property is relevant to the correctness of the implementation.

The commands, flags and numerical results are in
`scheduler/results/run_commands.md`. This file is about what was checked and
why, not how it was run.

## What the model covers

The system under analysis is the first-come first-served scheduling path in
`manager.c`: `schedule_fcfs`, `execute_instr`, `request_resource`,
`release_resource`, `enqueue_pcb`, `dequeue_pcb` and `terminate`. The other
two schedulers, `schedule_rr` and `schedule_priority`, are not modelled. They
share the same underlying concurrency machinery, so modelling all three would
have multiplied the state space without exposing a different class of defect.

Two worker processes stand for the OpenMP threads created by

    #pragma omp parallel num_threads(num_thr)

The three PCB queues become buffered channels rather than linked lists, since
no property depends on the pointer structure. The six `omp_nest_lock_t` locks
are modelled only where they affect interleaving: what matters is which reads
and writes are protected and which are not, not the reentrancy of the locks
themselves.

PCB and resource identifiers start at 1 so that 0 can stand for "none",
matching the `NULL` used throughout `manager.c`.

## Model state and verification-only state

These arrays mirror state that genuinely exists in the implementation:

| Variable | Corresponds to |
| --- | --- |
| `pcb_state[]` | `pcb->state`, values from `state_t` in `proc_structs.h` |
| `instr_count[]` | how far `pcb->next_instruction` has advanced through the instruction list |
| `waiting_for[]` | the resource named by the instruction a waiting PCB is blocked on |
| `resource_owner[]` | `resource->allocated` |

Two arrays are auxiliary. They exist so that properties can be stated and
they never influence the behaviour of any process.

| Variable | Meaning |
| --- | --- |
| `executing[]` | how many workers are currently inside the instruction loop for a given PCB |
| `in_ready[]` | how many times a PCB id is currently present in the ready queue |

`in_ready[]` is incremented at every send to `ready_q` and decremented at the
dequeue. The increment and the send are wrapped in `atomic` so that no
intermediate state exists in which the counter and the channel disagree,
which would produce spurious counterexamples about the model rather than
about the implementation.

---

## Property 1: pcb_mutex

Category: safety, mutual exclusion.

    #define one_worker_per_pcb (executing[1] <= 1 && executing[2] <= 1 && executing[3] <= 1)
    ltl pcb_mutex { [] one_worker_per_pcb }

**In English.** No process control block is ever being executed by two
workers at the same time.

**What it refers to in the implementation.** `schedule_fcfs` dequeues a PCB
under `q_locks[0]` inside an `omp critical` region, which is correct. What is
not protected is everything afterwards. The loop

    while (running_p->state == RUNNING)

reads `running_p->state` with no lock held, and after `execute_instr` returns
and `i_lock` is released the thread continues to read and write that PCB's
fields unprotected.

**Why it matters.** A PCB is the unit of work in this scheduler. Two threads
holding the same one is the fundamental invariant of any dispatcher, and
every other defect found in this program follows from it. `executing[]`
reaching 2 means the mutual exclusion the design assumes is not actually
enforced anywhere.

**Category.** Safety, in Lamport's sense: it asserts that nothing bad
happens, and a violation has a finite counterexample.

---

## Property 2: no_self_wait

Category: safety, resource ownership.

    #define no_self_wait_p ( \
        (waiting_for[1] == 0 || resource_owner[waiting_for[1]] != 1) && \
        (waiting_for[2] == 0 || resource_owner[waiting_for[2]] != 2) && \
        (waiting_for[3] == 0 || resource_owner[waiting_for[3]] != 3))
    ltl no_self_wait { [] no_self_wait_p }

**In English.** No process is ever waiting for a resource that it already
owns.

**What it refers to in the implementation.** `request_resource` walks the
resource list and tests only availability:

    if (resource->allocated == NULL)
    {
        resource->allocated = cur_pcb;
        ...
    }

There is no test of whether `resource->allocated == cur_pcb`. A process that
requests a resource it already holds falls through to

    if (!resource_found)
    {
        enqueue_pcb(cur_pcb, &waitingq, WAITING);
    }

and is placed on the waiting queue. It will be woken only when that resource
is released, and the only process that can release it is itself. It waits
forever.

**Why it matters.** This is the one defect in the set that is not a race. The
counterexample involves a single worker and `executing[]` never exceeds 1, so
it is a plain logic error reachable in a sequential execution. It is also the
root of the two liveness failures below, which makes it the most consequential
two-line bug in the program.

---

## Property 3: waiting_consistent

Category: safety, state consistency.

    #define waiting_consistent_p ( \
        (pcb_state[1] != WAITING || waiting_for[1] != 0) && \
        (pcb_state[2] != WAITING || waiting_for[2] != 0) && \
        (pcb_state[3] != WAITING || waiting_for[3] != 0))
    ltl waiting_consistent { [] waiting_consistent_p }

**In English.** A process marked WAITING is always actually waiting for
something.

**What it refers to in the implementation.** `enqueue_pcb` sets
`pcb->state = status` and appends to the queue, but the state field and the
queue membership are written at different times from the resource
bookkeeping in `request_resource`, and the PCB may be read concurrently in
between.

**Why it matters.** It is a second, independent symptom of the same double
dispatch race as Property 1, and it establishes a stronger consequence. The
counterexample ends with a PCB simultaneously marked WAITING, sitting on the
waiting queue, and owning a resource. Because it is on the waiting queue it
is never dispatched again, so the resource it holds is never released and
every other process that needs that resource blocks behind it.

Stating a second property that fails for the same underlying reason is
deliberate. Property 1 shows the race exists; this one shows it corrupts
observable scheduler state rather than merely overlapping in time.

---

## Property 4: no_dup_ready

Category: safety, queue integrity. This is the only property in the set that
holds.

    #define no_dup_ready_p (in_ready[1] <= 1 && in_ready[2] <= 1 && in_ready[3] <= 1)
    ltl no_dup_ready { [] no_dup_ready_p }

**In English.** The same process never appears twice in the ready queue at
once.

**What it refers to in the implementation.** The ready queue is written from
three places: `init_system` when the long-term scheduler seeds it,
`load_new_processes` on arrival, and `release_resource` when a waiting
process is woken. All three go through `enqueue_pcb` under
`q_locks[queue->lock_index]`.

**Why it matters.** A duplicate entry would mean the same PCB could be
dequeued twice and dispatched twice from a single enqueue, which is a
different route to the Property 1 violation and one that no amount of locking
around dispatch would fix.

**What a passing result does and does not establish.** It establishes that
within three processes, two workers, two resources and two instructions per
process, no reachable state has a duplicate. It does not establish absence
for larger configurations. Zero unreached statements in both `init` and
`Worker` is what makes the pass meaningful rather than a consequence of dead
code in the abstraction. The specific bound is justified in
`run_commands.md`: the shortest possible witness requires one blocking
request and one wakeup, which is two instructions, so raising the bound would
admit only longer instances of the same shape.

---

## Property 5: all_terminate

Category: liveness.

    #define all_done (pcb_state[1] == TERMINATED && pcb_state[2] == TERMINATED && pcb_state[3] == TERMINATED)
    ltl all_terminate { <> all_done }

**In English.** Every process eventually reaches the terminated state.

**What it refers to in the implementation.** The scheduler's own termination
condition. `terminate()` returns TRUE when all processes are counted in the
waiting or terminated queues, and each worker's `do ... while (terminate() ==
FALSE)` loop exits at that point. A process left waiting forever satisfies
`terminate()` without ever having run to completion.

**Why it matters.** Completing the workload is the scheduler's entire
purpose. This is the property that turns the `no_self_wait` logic error into
an observable failure of the program's function.

---

## Property 6: ready_dispatched

Category: liveness, responsiveness of the ready queue.

    ltl ready_dispatched { [] ((pcb_state[1] == READY) -> <> (pcb_state[1] == RUNNING)) }

**In English.** A process that becomes ready is eventually dispatched.

**Why it is stated for process 1 only.** The three processes are symmetric in
the model. None has a distinguishing attribute, since FCFS has no priority
field in play and the instruction sequences are generated by the same
nondeterministic choice. A single instance is therefore representative, and
it keeps the never claim small, which matters because this is the most
expensive property in the set.

**Why it matters.** It is the difference between "the system eventually
stops" and "the system eventually serves everyone". A scheduler that halts
with work still queued has failed even though it terminated.

---

## Fairness, and why the unfair results are not evidence

Both liveness properties were run with and without `-f`. Without fairness,
each reports an acceptance cycle after 25 states. That result is a modelling
artefact and must not be presented as a defect.

The cycle is one in which `init` evaluates its loop guard and is then never
scheduled again, so `ready_q` is never seeded and a worker spins on an empty
queue forever. No OpenMP runtime behaves this way; a thread that can make
progress is not passed over indefinitely. Weak fairness excludes exactly
those cycles, which is why every liveness result reported for this program
uses `-f`.

The cost is substantial. `ready_dispatched` completes in 25 states without
fairness and explores over eleven million with it, which matches the warning
in `spin_properties.pdf` that fairness slows the search.

This is a useful contrast with Program 2, where `workers_finish` fails both
with and without `-f`, so its counterexample is genuine at either setting.

---

## Deadlock: invalid end states

Category: safety. Not an LTL property. Checked with a claim-free verifier,
because while any `ltl` block is active `pan` disables the invalid end state
check.

**What it checks.** That no reachable state has a process stopped somewhere
other than a valid halting point.

**Why the result is more interesting than it first appears.** The
counterexample ends with a worker blocked at

    if
    :: instr_count[pcb_id] == MAX_INSTR ->
        pcb_state[pcb_id] = TERMINATED
    :: instr_count[pcb_id] < MAX_INSTR ->
        ...

with `instr_count[2]` at 4 and `MAX_INSTR` at 3. Neither guard is executable,
so the selection blocks forever.

That is a faithful rendering of undefined behaviour rather than a modelling
error. In `schedule_fcfs`:

    running_p->next_instruction = running_p->next_instruction->next;

    if (running_p->next_instruction == NULL)
    {
        running_p->state = TERMINATED;
        break;
    }

If two threads are executing the same PCB, both perform this advance. The
second dereferences `next_instruction` after the first has already moved it
to the last element, so it reads past the end of the linked list. C defines
no successor state there, and a Promela control point with no executable
transition is exactly a state with no defined successor. The abstraction has
no defined behaviour where the implementation has no defined behaviour.

So the deadlock result and the `pcb_mutex` violation are the same defect seen
from two directions. `pcb_mutex` proves two workers held the same PCB. This
run shows the damage: an instruction counter past its bound, which in
`manager.c` is a pointer walked off the end of a list.

---

## The causal chain

The properties are not independent findings. They resolve into two defects.

**The double dispatch race** in `schedule_fcfs` is exposed by `pcb_mutex`,
confirmed by `waiting_consistent`, and its consequence is shown by the
invalid end state.

**The self-wait logic error** in `request_resource` is exposed by
`no_self_wait`, and it is the cause of both liveness failures. A process
waiting on a resource it owns is never woken, so `all_terminate` fails, and
the workers exit once `terminate()` sees every process blocked, so
`ready_dispatched` fails.

There is a third consequence that the model does not check but that follows
from the same bug. `detect_deadlock` walks the waiting queue looking for a
cycle. A process waiting on its own resource is a cycle of length one, so the
function will print a deadlock cycle that does not exist in the sense the
function intends. One two-line omission produces a safety violation, two
liveness violations, and a false diagnostic.

---

## Properties considered and not verified

**`assert(instr_count[pcb_id] <= MAX_INSTR)`.** An earlier version of the
model carried this assertion and it was violated at depth 784 after 72,250
states. It has been removed. The final state of the deadlock trail already
shows `instr_count[2] = 4` with `MAX_INSTR` at 3, so the same evidence is
available without adding a statement to the model. Keeping it would also mean
every LTL run could stop on the assertion rather than on its named property,
since `pan` reports assertion violations within the scope of a claim, and
suppressing that would require `-A` on every other run.

**The correctness of `detect_deadlock`.** It runs after the parallel region,
inside `#pragma omp single`, so it is sequential code and contains no
concurrency to model. Its false-positive behaviour is a consequence of
`no_self_wait` failing and is discussed as such rather than verified
separately.

**Lock reentrancy.** `manager.c` uses `omp_nest_lock_t` throughout and
acquires some locks redundantly, for instance `i_lock` in both
`schedule_fcfs` and `execute_instr`. The model does not reproduce nesting
depth, because no property depends on it. What matters for every property
here is which accesses are protected and which are not.

**The other two schedulers.** `schedule_rr` and `schedule_priority` share the
dequeue, dispatch and resource machinery with `schedule_fcfs`, so the defects
found here are present in them too. Modelling them would have multiplied the
state space without exposing a different class of defect.
