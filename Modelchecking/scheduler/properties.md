# Correctness properties: FCFS process scheduler (Program 1)

This file describes the correctness properties checked for the Promela model in `scheduler/model.pml`. It explains what each property checks, how it relates to `manager.c`, and why it is important. The verification commands and results are in `scheduler/results/run_commands.md`.

## What the model covers

The model focuses on the FCFS scheduling path in `manager.c`, including `schedule_fcfs`, `execute_instr`, `request_resource`, `release_resource`, `enqueue_pcb`, `dequeue_pcb` and `terminate`. The round-robin and priority schedulers are not modelled because they use the same main concurrency and resource handling code.

Two worker processes represent the OpenMP threads created by:

```c
#pragma omp parallel num_threads(num_thr)
```

The three PCB queues are represented using buffered channels instead of linked lists, since the properties do not depend on the pointer structure. The six `omp_nest_lock_t` locks are modelled where they affect process interleaving. PCB and resource IDs start at 1, allowing 0 to represent `NULL` or "none".

## Model state and verification-only state

These variables represent state from the implementation:

| Variable           | Corresponds to                           |
| ------------------ | ---------------------------------------- |
| `pcb_state[]`      | `pcb->state` from `state_t`              |
| `instr_count[]`    | Progress through `pcb->next_instruction` |
| `waiting_for[]`    | Resource requested by a waiting PCB      |
| `resource_owner[]` | `resource->allocated`                    |

The model also has two verification-only arrays:

| Variable      | Meaning                                                  |
| ------------- | -------------------------------------------------------- |
| `executing[]` | Number of workers currently executing each PCB           |
| `in_ready[]`  | Number of times each PCB is currently in the ready queue |

`in_ready[]` is updated together with the queue operation using `atomic`. This keeps the model state consistent and avoids counterexamples caused only by the abstraction.

---

## Property 1: `pcb_mutex`

Category: safety, mutual exclusion.

```text
#define one_worker_per_pcb (executing[1] <= 1 && executing[2] <= 1 && executing[3] <= 1)

ltl pcb_mutex { [] one_worker_per_pcb }
```

A PCB should never be executed by two workers at the same time.

**Implementation.** `schedule_fcfs` removes a PCB from the queue while holding `q_locks[0]` inside an OpenMP critical section. However, the PCB is not protected after it has been removed. For example:

```c
while (running_p->state == RUNNING)
```

reads the PCB without holding a lock. Other accesses after `execute_instr` also occur without protection.

**Why it matters.** A PCB represents one unit of work, so two workers executing the same PCB at the same time breaks the main scheduling invariant. If `executing[]` reaches 2, the dispatcher has allowed the same process to run twice.

This is a safety property because a violation produces a finite counterexample.

---

## Property 2: `no_self_wait`

Category: safety, resource ownership.

```text
#define no_self_wait_p ( \
    (waiting_for[1] == 0 || resource_owner[waiting_for[1]] != 1) && \
    (waiting_for[2] == 0 || resource_owner[waiting_for[2]] != 2) && \
    (waiting_for[3] == 0 || resource_owner[waiting_for[3]] != 3))

ltl no_self_wait { [] no_self_wait_p }
```

A process should never wait for a resource that it already owns.

**Implementation.** `request_resource` checks whether a resource is available:

```c
if (resource->allocated == NULL)
{
    resource->allocated = cur_pcb;
    ...
}
```

It does not check whether the resource is already owned by `cur_pcb`. If the resource is unavailable, the process is placed on the waiting queue:

```c
if (!resource_found)
{
    enqueue_pcb(cur_pcb, &waitingq, WAITING);
}
```

The process then waits for the resource to be released, but it is the owner itself, so it can wait forever.

**Why it matters.** This is a logic error rather than a race. It can occur with only one worker. It also causes the liveness failures in Properties 5 and 6 because a process can remain blocked permanently.

---

## Property 3: `waiting_consistent`

Category: safety, state consistency.

```text
#define waiting_consistent_p ( \
    (pcb_state[1] != WAITING || waiting_for[1] != 0) && \
    (pcb_state[2] != WAITING || waiting_for[2] != 0) && \
    (pcb_state[3] != WAITING || waiting_for[3] != 0))

ltl waiting_consistent { [] waiting_consistent_p }
```

A process marked as `WAITING` should have a resource that it is waiting for.

**Implementation.** `enqueue_pcb` changes the PCB state and adds it to a queue, while resource information is handled separately in `request_resource`. These operations can be observed by another worker while the PCB is being changed.

**Why it matters.** The property shows another effect of the scheduling race. A counterexample can leave a PCB marked `WAITING` while it still owns a resource. Since it is waiting, it is not dispatched again, so the resource is never released and other processes can become blocked.

Property 1 shows that two workers can execute the same PCB, while this property shows that the race can also corrupt the scheduler's state.

---

## Property 4: `no_dup_ready`

Category: safety, queue integrity.

This is the only property in the set that holds.

```text
#define no_dup_ready_p (in_ready[1] <= 1 && in_ready[2] <= 1 && in_ready[3] <= 1)

ltl no_dup_ready { [] no_dup_ready_p }
```

 A PCB should not appear more than once in the ready queue.

**Implementation.** The ready queue is modified by `init_system`, `load_new_processes` and `release_resource`. These operations use `enqueue_pcb` with the appropriate queue lock.

**Why it matters.** A duplicate PCB could be removed from the queue more than once and dispatched multiple times. This would provide another way of causing the `pcb_mutex` violation.

The property holds for the tested configuration of three processes, two workers, two resources and two instructions per process. The verification also reported zero unreached statements in the relevant proctypes.

The result is still bounded to this configuration. The bound is sufficient to represent the shortest blocking and wake-up scenario.

---

## Property 5: `all_terminate`

Category: liveness.

```text
#define all_done (pcb_state[1] == TERMINATED && pcb_state[2] == TERMINATED && pcb_state[3] == TERMINATED)

ltl all_terminate { <> all_done }
```

 Every process should eventually reach the `TERMINATED` state.

**Implementation.** The workers continue running until `terminate()` returns `TRUE`. A process that is stuck in the waiting queue can cause the system to reach the termination condition without the process actually completing.

**Why it matters.** The scheduler's purpose is to complete all processes. A process that remains permanently blocked means the scheduler has failed to complete its workload.

---

## Property 6: `ready_dispatched`

Category: liveness, ready queue responsiveness.

```text
ltl ready_dispatched { [] ((pcb_state[1] == READY) -> <> (pcb_state[1] == RUNNING)) }
```

If a process becomes ready, it should eventually be dispatched.

**Why process 1 only?** The three processes are symmetric in the model. They have no different priorities or other properties that would make one process special. Checking one process is therefore enough and keeps the model smaller.

**Why it matters.** A scheduler should not only terminate, it should also eventually run processes that are ready. This property checks that a ready process is not left waiting forever.

---

## Fairness

Both liveness properties were checked with and without `-f`.

Without fairness, both properties produce an acceptance cycle after 25 states. This is caused by the model allowing `init` to be repeatedly ignored by the scheduler. As a result, the ready queue is never populated and a worker can remain waiting forever.

This is not considered a real program defect because OpenMP scheduling does not indefinitely ignore a process that can make progress. Weak fairness removes this behaviour from the verification.

For this reason, the reported liveness results use `-f`. The search is much more expensive with fairness. For example, `ready_dispatched` increases from a 25-state search without fairness to more than eleven million states with fairness.

This differs from Program 2, where the termination property fails both with and without fairness. That counterexample is therefore not caused by unfair scheduling.

---

## Deadlock: invalid end states

Category: safety. This is not an LTL property.

The deadlock check uses a claim-free verifier because Spin disables invalid end state checking while an LTL claim is active.

The counterexample occurs when a worker reaches a point where neither branch of the instruction loop can execute:

```c
if
:: instr_count[pcb_id] == MAX_INSTR ->
    pcb_state[pcb_id] = TERMINATED
:: instr_count[pcb_id] < MAX_INSTR ->
    ...
```

The model reaches a state where `instr_count[2]` is 4 while `MAX_INSTR` is 3. Neither condition is true, so the worker cannot continue.

This is linked to the `pcb_mutex` violation. If two workers execute the same PCB, both can advance `next_instruction`. One worker can move it to the end of the instruction list, after which the other worker can access it again. In the C implementation this can result in accessing beyond the end of the linked list.

Therefore, the deadlock result shows the consequence of the same race detected by `pcb_mutex`.

---

## The causal chain

The verification results point to two main defects in `manager.c`.

**1. Double dispatch race.**
`schedule_fcfs` does not protect the PCB after it is removed from the ready queue. This is detected by `pcb_mutex` and `waiting_consistent`, and can eventually lead to the invalid end state.

**2. Self-wait logic error.**
`request_resource` does not check whether the requesting process already owns the resource. This is detected by `no_self_wait` and causes the liveness failures in `all_terminate` and `ready_dispatched`.

The self-wait bug can also affect `detect_deadlock`. A process waiting for a resource that it already owns forms a cycle of length one, so the function can report a deadlock even though the intended deadlock detection is meant to identify cycles between different processes.

---

## Properties considered and not verified

**`assert(instr_count[pcb_id] <= MAX_INSTR)`.**
An earlier version of the model included this assertion and it was violated. It was removed because the same behaviour is already visible in the deadlock counterexample, where `instr_count[2] = 4` while `MAX_INSTR = 3`. Keeping the assertion could also cause verification runs to stop at the assertion instead of the selected LTL property.

**Correctness of `detect_deadlock`.**
`detect_deadlock` runs after the parallel region inside `#pragma omp single`, so it is sequential code. Its false positive is a consequence of the self-wait bug rather than a concurrency property that needs to be modelled separately.

**Lock reentrancy.**
The model does not reproduce the exact nesting behaviour of `omp_nest_lock_t`. The properties only depend on which accesses are protected, not on the number of times a lock can be acquired.

**Other schedulers.**
`schedule_rr` and `schedule_priority` were not modelled. They share the main queue, dispatch and resource handling code with `schedule_fcfs`, so modelling them would increase the state space without testing a different type of concurrency problem.
