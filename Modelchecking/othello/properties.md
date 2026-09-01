# Correctness properties: Othello MPI player (Program 2)

This file describes the correctness properties checked for the Promela model in `othello/model.pml`. It explains what each property checks, how it relates to `my_player.c`, and why it is important. The verification commands and results are given in `othello/results/run_commands.md`.

## What the model covers

The model represents the MPI master and worker logic in `my_player.c`, including `run_master`, `execute_master`, `send_init_moves`, `send_next_move`, `pop_move`, `run_worker`, and the worker call to `minimax_pruning`. The serial path is not modelled because `serial_master` only runs when `comm_sz == 1` and does not involve concurrency.

The model has three types of processes. `Master` represents rank 0, `Worker` represents the other MPI ranks, and `Referee` represents the Ingenious Framework sending messages to the player. The referee is included because Promela models must be closed and because the message handling in `run_master` can affect the behaviour of the workers.

The board and most of the Othello logic are abstracted away. This includes `legal_moves`, `make_move`, `evaluate`, the recursive parts of `minimax`, and the board itself. The model only keeps the three possible outcomes of a `minimax_pruning` call. The board broadcast is represented by the round number because the properties only depend on the broadcast occurring and reaching each worker.

MPI communication is represented using channels. `MPI_Bcast` uses one channel per worker, while move and no-work messages use `to_worker[]`. Worker results are sent through `to_master`, which represents the master's use of `MPI_Iprobe` and `MPI_Recv(MPI_ANY_SOURCE)`.

Unlike Program 1, this program has no shared memory and therefore no mutual exclusion problem. The main correctness issues are related to messages, their contents, whether they are consumed, and whether termination messages are sent.

## Model state and verification-only state

The following variables represent state that exists in the implementation:

| Variable                  | Corresponds to                                                                           |
| ------------------------- | ---------------------------------------------------------------------------------------- |
| `my_colour`               | `my_colour` in `run_master`, with a negative value representing the `end_game` broadcast |
| `round_no`                | The board broadcast, represented by the round number                                     |
| `num_moves`               | `move_stack.size` after `load_round_moves`                                               |
| `moves_left`              | Remaining moves on the move stack                                                        |
| `master_alpha`            | `alpha` in `execute_master`                                                              |
| `master_depth`            | `depth` in `execute_master`, represented as full or reduced                              |
| `best_move`, `best_score` | `max_move` and `max_score`, which determine `global_best`                                |
| `workers_done`            | Number of workers that have left `run_worker`                                            |

`NO_EVAL` represents `INT_MAX`, while `ALPHA_MIN` represents `INT_MIN`.

The model also has three variables used only for verification:

| Variable       | Meaning                                               |
| -------------- | ----------------------------------------------------- |
| `true_score[]` | Correct evaluation for each move in the current round |
| `best_true`    | Highest value in `true_score[]`                       |
| `round_done`   | Set when the collection loop for a round has finished |

`true_score[]` is important because the real program does not store the correct score separately. The model chooses these values before the round and allows `evaluate_move` to return either the correct value or `NO_EVAL`. This makes it possible to check whether the master selected the best move.

`round_done` ensures that the move selection properties are checked only after all results have been collected.

---

## Property 1: `no_eval_chosen`

Category: safety, data validity.

```text
#define chose_no_eval (round_done && best_score == NO_EVAL)
ltl no_eval_chosen { []!chose_no_eval }
```

 The master should never finish a round with `NO_EVAL` as the selected score.

**Implementation.** In the minimising branch of `minimax`, `minEval` is initially set to `INT_MAX`:

```c
minEval = INT_MAX;
legal_moves(moves, &number_of_moves, my_colour);

for (int i = 0; i < number_of_moves; i++)
{
  ...
  minEval = minimum(minEval, eval);
  beta = minimum(beta, eval);

  if (beta <= *alpha)
  {
    break;
  }
}

return minEval;
```

There is no check for `number_of_moves == 0`. In Othello, having no legal moves means the player passes, so this is a valid situation. If there are no moves, the loop does not execute and `INT_MAX` is returned as if it were a real score. The maximising branch has the same issue with `INT_MIN`.

The value is then used by `execute_master` when comparing scores:

```c
if ((process_score > max_score) || (max_move == -1))
```

Since `INT_MAX` is larger than any valid score, it can incorrectly become the best score and can also affect the shared alpha value.

**Why it matters.** A sentinel value is being used as a real evaluation. This can cause the master to make a decision using invalid information. The problem is also difficult to notice because the resulting score can still look reasonable.

The damage is limited to the current move decision because `execute_master` resets `alpha` at the start of each round.

---

## Property 2: `best_is_maximal`

Category: safety, functional correctness.

```text
#define chose_maximal (true_score[best_move] == best_true)
ltl best_is_maximal { [] ((round_done && num_moves > 0) -> chose_maximal) }
```

 If a round has at least one legal move, the master should choose the move with the highest correct score.

**Implementation.** This corresponds to `global_best.move`, which is the move eventually played by `run_master`. The condition `num_moves > 0` excludes the pass case.

**Why it matters.** This property checks the main purpose of the program. The MPI communication could work correctly while the program still chooses the wrong move.

There are two independent reasons why this property fails:

1. **Invalid minimax results.** The `INT_MAX` value described in Property 1 can be treated as a real score.
2. **Different search conditions.** `execute_master` can reduce `depth` to 4 after the cutoff time. This means some moves may be searched to depth 11 or more while later moves are searched to depth 4. Their scores are then compared as if they were produced under the same conditions. The shared alpha value can also cause different amounts of pruning.

The model uses two verification switches, `NO_LEAK` and `NO_TIME_CUTOFF`, to test these causes separately. `-DNO_LEAK` removes the `NO_EVAL` fault, while `-DNO_TIME_CUTOFF` removes the depth reduction. The results in `run_commands.md` show that removing either fault on its own still leaves the property violated, while removing both makes it hold. This shows that both problems contribute independently.

---

## Property 3: `no_lost_results`

Category: safety, message accounting.

```text
ltl no_lost_results { [] (round_done -> len(to_master) == 0) }
```

This is the only property that holds.

 When a round finishes, there should be no worker results still waiting in the master's queue.

**Implementation.** The collection loop ends when:

```c
while (results < num_moves)
```

The master counts received results rather than checking whether the channel is empty. If a worker never sends a result, the `MPI_Iprobe` loop can wait forever.

**Why it matters.** A result left in the queue could be read during the next round and incorrectly used for a different move. This could affect that round's alpha and best score.

The property holds for the reduced configuration used in the verification. It does not prove the same result for the full deployed worker count, where the exhaustive state space did not fit in memory and bitstate search was required. Therefore, the approximate result is not treated as equivalent to an exhaustive proof.

---

## Property 4: `workers_finish`

Category: liveness.

```text
#define all_home (workers_done == NUM_WORKERS)
ltl workers_finish { <> all_home }
```

 Every worker should eventually leave its evaluation loop and be able to reach `MPI_Finalize`.

**Implementation.** A worker waits for the next `MPI_Bcast` at the start of each round. It only stops when the master broadcasts a negative `end_game` value.

`run_master` handles `GENERATE_MOVE`, `PLAY_MOVE`, `GAME_TERMINATION`, `MATCH_RESET`, and `UNKNOWN`. Only the last two result in the termination broadcast. However, `comms.h` also defines `RECV_FAILED` and `CLIENT_DISCONNECTED`, and `run_master` has no cases for them and no final `else`.

If either message is received, `running` remains true and no termination broadcast is sent. The workers can therefore remain blocked waiting for the next broadcast.

The Promela model represents this using an explicit `skip`, matching what the C code effectively does for these unhandled messages.

**Why it matters.** This problem did not occur in the recorded matches because the framework did not send these messages. Model checking found that the situation is still reachable. This shows why model checking is useful for finding inputs and execution paths that normal testing may not encounter.

The property fails both with and without `-f`. The problem is caused by a missing branch, not by unfair scheduling.

---

## Deadlock: invalid end states

Category: safety. This is not an LTL property.

The invalid end state check looks for reachable states where the system has stopped at a point that is not a valid termination state.

The same missing message handling from Property 4 can cause a deadlock. The workers can be blocked waiting for a broadcast while the master continues through its loop without sending one. The smallest counterexample does not require a move to be played, showing that the problem is in the referee message handling rather than move evaluation.

`workers_finish` describes the problem in terms of worker termination, while the deadlock check shows that the entire system can become stuck.

---

## What the results show

The four properties point to three main problems in `my_player.c`.

**1. Invalid minimax initialisation.**
The minimising branch can return `INT_MAX` when there are no legal moves. This is detected directly by `no_eval_chosen` and also contributes to the failure of `best_is_maximal`.

**2. Comparing scores from different search conditions.**
`execute_master` can compare scores produced at different search depths and with different pruning conditions. The fault injection tests show that this is independent of the minimax problem.

**3. Missing message handling.**
`run_master` does not handle `RECV_FAILED` or `CLIENT_DISCONNECTED`. This causes `workers_finish` to fail and can also lead to a deadlock.

`no_lost_results` holds for the verified reduced configuration. Including a property that passes provides useful evidence that the model is checking more than just known defects.

---

## Properties considered and not verified

**Mutual exclusion.**
This does not apply to the MPI program because MPI ranks use separate address spaces and do not share memory.

**Alpha monotonicity.**
The master's alpha only increases because it is updated when a received score is greater than the current alpha. This can be seen directly from the implementation and does not provide a useful concurrency property.

**Collective ordering.**
The model sends broadcasts to each worker in a fixed order, so the required ordering is built into the abstraction. Verifying this would mainly test the model rather than the implementation.

**Move-halving in the maximising branch.**
`minimax` halves `number_of_moves` when searching the opponent's replies. This affects search quality rather than concurrency, and the board is not represented in the model.

**Memory management.**
`minimax` allocates `moves` and `copy` before the depth check but does not free them when returning from the `depth == 0 || game_over()` case. This causes memory leaks, but Promela does not model heap allocation. A memory analysis tool would be more appropriate for this issue.

**Socket layer.**
`comms.c` and the framing of referee messages are treated as framework code. The relevant defect is how `run_master` responds to message types, rather than how those messages are transmitted.

