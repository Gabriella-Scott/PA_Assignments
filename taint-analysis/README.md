# Taint Analysis (Data-Flow Assignment)
A static taint analyser for the restricted Python subset described in the assignment spec. It reads the `graph.json` and `test_case.json` of a test case directory and reports whether taint from the designated source may reach the designated sink.

## Build and run
From the assignment root directory:
```sh
./build/sh
./main -i testcases/test1
```
`build.sh` only marks `main` executable, since Python needs no compilation step.

Output is a single JSON object on stdout:
```json
{"reaches": true}
```

## Assumptions
- **Explicit flows only.** The analyser tracks data dependence, not control dependence, so a
  branch whose condition is tainted does not taint what is assigned inside it. `test4` requires
  this: the expected answer is `false` even though the sink is control-dependent on a tainted
  comparison.
- **The `reaches` field is never read.** Only `sourceNode` and `sinkNode` are taken from
  `test_case.json`.
- **All branches are reachable** (path-insensitive). Conditions are never evaluated.
- **Field-insensitive.** Per the spec, values are flat and have no members.
- **Lists are summarised per allocation site.** All elements of one list share a single abstract
  location, so an index is never resolved to a specific slot and `y[i]` cannot be tainted by `i`.
- **Boolean and comparison operators do not propagate taint** to their result; all other
  operators propagate from every operand.
- **Calls to functions that are not defined in the file** (such as `sink` and `print`) have no
  body to analyse and are treated as having no effect on the store.
- **Module-level and captured scope are out of scope**, as stated in the specification.