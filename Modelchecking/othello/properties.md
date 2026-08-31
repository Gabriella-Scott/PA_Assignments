# Verification runs: Othello MPI player (Program 2)

This file records every verification run reported for the Othello model, in
enough detail to reproduce each result from a clean checkout.

The properties themselves, and the reasoning behind them, are in
`othello/properties.md`. This file is about how the results were obtained.

## Environment

| Item | Value |
| --- | --- |
| Model checker | Spin Version 6.5.2, 6 December 2019 |
| Compiler | gcc |
| Platform | Ubuntu Linux |
| Memory | 7 GB RAM, 1 GB swap |
| Model file | `othello/model.pml` |
| Repository | `PA_Assignments/Modelchecking` |
| Model version | commit `70b73d2` |

The memory figure matters. It is the constraint that decides which
configurations can be searched exhaustively and which cannot, and it is
referred to by name in the section on the reduced configuration below.

All results were produced from the single committed `model.pml` at the commit
above. No local edits were made between runs. Every configuration is supplied
on the command line rather than by editing the file, so each result is
reproducible from the same source.

## Model configuration

Three parameters are wrapped in include guards so they can be set at parse
time without editing the model:

    #ifndef NUM_WORKERS
    #define NUM_WORKERS 3
    #endif

    #ifndef MAX_MOVES
    #define MAX_MOVES 4
    #endif

    #ifndef MAX_ROUNDS
    #define MAX_ROUNDS 2
    #endif

`NUM_WORKERS 3` is not an arbitrary choice. `common/configs.py` in the
Othello project sets `"threads": 4` for the Othello game, and `Othello.json`
carries the same value through to the Java wrapper that invokes `mpirun`. So
`comm_sz` is 4, one master and three workers, and the model matches the
deployed system.

`MAX_MOVES` must be strictly greater than `NUM_WORKERS`. If it is not,
`send_init_moves` empties the move stack before any result comes back, so
`send_next_move` called from the collection loop can only ever send
`NO_WORK_TAG` and the dynamic work redistribution that the architecture
exists for is never exercised. This was not deduced in advance; it was caught
by pan's unreachable statement report at `NUM_WORKERS 3, MAX_MOVES 3`:

    unreached in proctype Master
        model.pml:57, state 75, "to_worker[r_rank]!MOVE_TAG,moves_left,master_alpha,master_depth"
        model.pml:58, state 76, "moves_left = (moves_left-1)"

`MAX_MOVES 4` is the smallest value that exceeds the worker count.

## Where the -D flags go

Two different preprocessor passes are involved and the definitions are not
interchangeable.

`-DNUM_WORKERS`, `-DMAX_MOVES`, `-DMAX_ROUNDS`, `-DNO_LEAK` and
`-DNO_TIME_CUTOFF` are used by `model.pml`, so they go to `spin`, which runs
the C preprocessor when it generates `pan.c`:

    spin -DMAX_MOVES=4 -a model.pml          # correct
    gcc -O2 -DMAX_MOVES=4 -o pan pan.c       # silently ignored

By the time `gcc` sees `pan.c` the constant is already a literal, so the gcc
form has no effect and produces no warning.

`-DNOCLAIM` and `-DBITSTATE` are used by `pan.c` itself, so they go to `gcc`.

The same flags must also be repeated on any replay, because `spin -t`
re-reads `model.pml` and re-runs the preprocessor. Replaying a two-worker
trail against a three-worker model produces a stream of `transition failed`
lines and a misleading trace. Treat `transition failed` during a replay the
same way as the warning that `model.pml` is newer than the trail file.

## Build steps

Two verifier binaries are built from the same generated `pan.c`.

### Standard verifier, used for all LTL properties

    spin -DNUM_WORKERS=3 -DMAX_MOVES=4 -DMAX_ROUNDS=1 -a model.pml
    gcc -O2 -o pan pan.c

`spin -a` reports the claims it found and warns that only one is used per
run:

    the model contains 4 never claims: workers_finish, no_lost_results,
    best_is_maximal, no_eval_chosen
    only one claim is used in a verification run
    choose which one with ./pan -a -N name (defaults to -N no_eval_chosen)

Every run below therefore names its property with `-N`. The header line
`pan: ltl formula <name>` confirms which claim was actually used.

### Claim-free verifier, used for the deadlock check

    gcc -O2 -DNOCLAIM -o pan_noclaim pan.c

While a never claim is active, pan disables the invalid end state check, as
its header shows:

    invalid end states      - (disabled by never claim)

`-DNOCLAIM` excludes the claim from the generated verifier, re-enabling that
check without removing the `ltl` blocks from `model.pml`. One committed model
file therefore serves every result.

### Both binaries must be rebuilt after every `spin -a`

`spin -a` overwrites `pan.c`. A binary left over from a previous
configuration will run happily and report results for the wrong model. This
is easy to miss because nothing warns about it.

## Configurations

| Label | Flags | Purpose |
| --- | --- | --- |
| A | `NUM_WORKERS 3, MAX_MOVES 4, MAX_ROUNDS 1` | matches the deployed system |
| B | `NUM_WORKERS 3, MAX_MOVES 4, MAX_ROUNDS 0` | minimal witness for the termination defect |
| C | `NUM_WORKERS 3, MAX_MOVES 4, MAX_ROUNDS 2` | covers match reset and the per-round reset |
| D | `NUM_WORKERS 2, MAX_MOVES 3, MAX_ROUNDS 1` | reduced, small enough to search exhaustively |
| E | D plus fault injection switches | isolates the causes of `best_is_maximal` failing |

## Summary of results

### Configuration A, three workers, one round

| Property | Result | Depth | States | Time |
| --- | --- | --- | --- | --- |
| `no_eval_chosen` | violated | 205 | 5,336 | under 0.01 s |
| `best_is_maximal` | violated | 255 | 106,138 | 0.13 s |
| `workers_finish` | violated, acceptance cycle | 31 | 203 | under 0.01 s |
| deadlock | invalid end state | 20 | 205 | under 0.01 s |
| `no_lost_results` | exhausted memory, see bitstate below | | | |

State-vector 244 bytes.

### Configuration B, three workers, zero rounds

| Property | Result | Depth | States |
| --- | --- | --- | --- |
| deadlock | invalid end state | 20 | 205 |
| `workers_finish` | violated, acceptance cycle | 31 | 203 |

### Configuration C, three workers, two rounds

| Property | Result | Depth | States |
| --- | --- | --- | --- |
| `no_eval_chosen` | violated | 341 | 5,683 |
| `best_is_maximal` | violated | 391 | 107,862 |

### Configuration D, two workers, one round

| Property | Result | Depth | States | Time |
| --- | --- | --- | --- | --- |
| `no_eval_chosen` | violated | 172 | 1,046 | under 0.01 s |
| `best_is_maximal` | violated | 222 | 17,286 | 0.02 s |
| `workers_finish` | violated, acceptance cycle | 28 | 77 | under 0.01 s |
| deadlock | invalid end state | 17 | 79 | under 0.01 s |
| `no_lost_results` | holds, 0 errors | | 7,676,089 | 11.4 s |

State-vector 204 bytes. The `no_lost_results` run reports zero unreached
statements in all four proctypes.

### Configuration E, fault isolation on `best_is_maximal`

| Flags | Result | Depth | States |
| --- | --- | --- | --- |
| none | violated | 222 | 17,286 |
| `-DNO_TIME_CUTOFF` | violated | 218 | 10,026 |
| `-DNO_LEAK` | violated | 280 | 47,056 |
| `-DNO_TIME_CUTOFF -DNO_LEAK` | holds, 0 errors | | 626,037 |

The last run reports zero unreached statements in all four proctypes.

## The three-worker `no_lost_results` run

Exhaustive search of configuration A was killed by the kernel out-of-memory
killer. At a 244 byte state vector plus 28 bytes of overhead, 7 GB of RAM
holds roughly 2.6 x 10^7 states, and the search passed that without
terminating.

The fallback is bitstate storage, which is a compile-time option:

    spin -DNUM_WORKERS=3 -DMAX_MOVES=4 -DMAX_ROUNDS=1 -a model.pml
    gcc -O2 -DBITSTATE -o pan_bit pan.c
    ./pan_bit -a -N no_lost_results -m100000 -w32

Note that `-bitstate` and `-collapse` are compile-time directives for
`pan.c`. Passing them as runtime flags to `./pan` is silently ignored.

Result:

    errors: 0
    depth reached 436
    655,692,780 states stored
    425,727,270 states matched
    1.08142e+09 transitions
    hash factor: 6.55027 (best if > 100.)
    bits set per state: 3 (-k3)
    512 MB memory used for hash array (-w32)
    518 MB total actual memory usage
    approximately 1,370 seconds

Zero unreached statements in all four proctypes.

**How to read this result.** Bitstate storage replaces exact state storage
with a hash-indexed bit array, so distinct states can collide and be treated
as already visited. The search is therefore approximate and possibly lossy.
The `hash factor` of 6.55 is well below the 100 that indicates near-exhaustive
coverage, so an appreciable fraction of the state space may have been skipped.

The correct claim is that no counterexample was found in an approximate
search covering roughly 6.6 x 10^8 states, not that the property holds at
three workers. The exhaustive configuration D result is the stronger
evidence. Reporting both, and being explicit about which is which, is the
point.

## The reduced configuration

Properties that fail are cheap, because the search stops at the first
counterexample. The property that holds must exhaust the reachable state
space, and that is where the 7 GB limit binds.

| Configuration | `no_lost_results` |
| --- | --- |
| D, two workers, three moves | 7,676,089 states, exhaustive, 11.4 s |
| A, three workers, four moves | exceeds 7 GB, bitstate only |

Justification for the reduction, specific to this property: a lost result
requires the master to finish a round while a message it should have consumed
is still queued. The shortest such witness needs one worker to send a result
that the master's counter does not account for, which requires two workers at
most, one to be accounted for and one not. Adding a third worker admits only
longer instances of the same shape.

This is a bounded argument and not a proof. The exhaustive result establishes
that no result is stranded within two workers, three moves and one round. It
does not establish absence for larger configurations, and the bitstate run at
three workers raises but does not settle the confidence.

## Search order and the referee process

The four terminal message branches of `proctype Referee` are listed before
the `budget > 0` branch. This is deliberate. Depth-first search explores the
branches of a nondeterministic `do` in the order written, so with
`GENERATE_MOVE` first the search explored an entire move round before ever
reaching `RECV_FAILED`, and the termination counterexamples cost tens of
millions of states at three workers. With the terminal branches first they
are found in a few hundred.

The branches of a nondeterministic selection are unordered by definition, so
this changes only the order in which the search visits the state space, not
the state space itself. That is confirmed empirically: after the reordering,
`no_lost_results` at configuration D still reports exactly 7,676,089 states,
while the failing runs changed slightly because a different counterexample is
now reached first.

## Saving and replaying error trails

Each run overwrites `model.pml.trail`, so `verify.sh` moves each one into
`results/` immediately. To replay a saved trail, repeat the flags of the run
that produced it:

    spin -DNUM_WORKERS=3 -DMAX_MOVES=4 -DMAX_ROUNDS=1 \
         -t -p -k results/no_eval_chosen_w3r1.trail model.pml

Adding `-c` prints one column per process, which makes the interleaving
between the master and the three workers easier to follow.

Trails are named `<property>_w<workers>r<rounds>.trail`.

## Reproducing every result

All of the above is produced by a single script, `othello/verify.sh`, from a
clean checkout at the commit recorded at the top of this file:

    cd Modelchecking/othello
    ./verify.sh

Or, to skip the two exhaustive runs:

    ./verify.sh quick

The script writes the full pan output of every run to `results/logs/`, the
error trails to `results/`, and a summary table to `results/summary.txt`. It
rebuilds both binaries after every `spin -a` and passes each definition to
the correct preprocessor pass.

The three-worker bitstate run is not part of the script, because it takes
roughly 23 minutes and is a deliberate fallback rather than a routine result.
Its commands are given in the section above.

iSpin was not used for any of the results reported here. All runs were
performed from the command line.