# Taint Analysis (Data-Flow Assignment)

A static taint analyser for the restricted Python subset described in the assignment spec. It reads
the `graph.json` and `test_case.json` of a test case directory and reports whether taint from the
designated source may reach the designated sink. Both the intraprocedural and the interprocedural
analysis are in this one analyser.

## Prerequisites

- Linux x86_64
- Python 3, standard library only. Developed and tested on Python 3.10.12.

## Build and run

From the assignment root directory:

```sh
./build.sh
./main -i testcases/test1
```

`build.sh` only marks `main` executable, since Python needs no compilation step.

Output is a single JSON object on stdout.

```json
{"reaches": true}
```

## Results

The 10 provided cases, plus two generated with `df-assignment-graph-builder` for this submission.
Every case produces the expected answer.

| Case | What it tests | Expected | Result |
|---|---|---|---|
| `test1` | parameter straight into the sink | true | true |
| `test2` | unrelated parameter | false | false |
| `test3` | sink inside a branch | true | true |
| `test4` | implicit flow must be ignored | false | false |
| `test5` | strong update overwrites taint | false | false |
| `test_inter1` | call, argument bound to parameter | true | true |
| `test_inter2` | second argument position | true | true |
| `test_inter3` | constant argument | false | false |
| `test_inter4` | callee returns its untainted parameter | false | false |
| `test_inter5` | function reference passed through variables | true | true |
| `test_list1` | list allocation, aliasing, weak update, index read | true | true |
| `test_nested1` | nested definition, called after its `def` | true | true |

`test_list1` and `test_nested1` were written because none of the provided cases exercise lists,
aliasing or nested function definitions

## Assumptions

- **Explicit flows only.** The analyser tracks data dependence, not control dependence, so a
  branch whose condition is tainted does not taint what is assigned inside it. `test4` requires
  this: the expected answer is `false` even though the sink is control-dependent on a tainted
  comparison.
- **The `reaches` field is never read.** Only `sourceNode` and `sinkNode` are taken from
  `test_case.json`.
- **All branches are reachable** (path-insensitive). Conditions are never evaluated.
- **Field-insensitive.** Values are flat and have no members.
- **Lists are summarised per allocation site.** All elements of one list share a single abstract
  location, so an index is never resolved to a specific slot and `y[i]` cannot be tainted by `i`.
- **Boolean and comparison operators do not propagate taint** to their result; all other
  operators propagate from every operand.
- **Calls to functions with no definition in the file** (such as `sink` and `print`) cannot be
  entered, so they are summarised: the call's own result value may be tainted by any of its
  arguments, and nothing else in the store changes.
- **Module-level and captured scope are out of scope**
- **Taint under any one calling context is enough.** This is a may-analysis, so the sink is
  reported as reached if it is tainted on any analysed path in any context.

## Known limitations

- **Context depth is capped at 2** (`CONTEXT_DEPTH` in `analysis/solver.py`). Call paths longer
  than the cap share a context, so their facts merge. Less precise, but still conservative. The
  cap is also why recursion terminates.
- **A call before a nested `def` of the same name** resolves to the nested one, where Python would
  use the outer one. Keping both targets would fix it but report a false positive instead.
- **Weak updates only add taint.** Writing clean data over a tianted list element leaves the list
  tainted, since one location stands for the whole list.
- **Indices are not tracked**, so `a[0] = x` followed by `sink(a[1])` gives `true`.
- **Some operator names are guesses.** `assignment`, `greaterThan`, `listLiteral`
  and `indexAccess` are confirmed from generated graphs; the boolean connective
  names are not. An unknown operator falls through to the default rule where
  every operand taints the result, so a wrong guess loses precision but stays
  safe. Augmented assignments are matched on the `assignment` prefix rather than
  by name, so they do not depend on the guess.

## Extras

- **The points-to analysis is flow-sensitive.** A call target is read from the store at that
  program point, so `f = clean` then `f = dirty` resolves `f()` to `dirty` only, and a nested
  `def` shadowing an outer one of the same name resolves to the nested one only.
- **Context-sensitive by call string**, so `f(0)` and `f(x)` do not merge their facts.
- **Aliasing, lists and function references use one mechanism.** A variable maps to a set of
  abstract locations, each named by the graph node that creates it.
- **`tools/`** holds development scripts, not part of the analyser: `dump_case.py` prints the AST
  and CFG per function, `test_store.py` asserts the store operations, and `test_transfer.py` runs
  the suite and traces one case with `-v`.
  - **Augmented assignments are matched by prefix**, so an `assignment*` operator
  the tables do not list still updates the store rather than being skipped.