# CBMC Sudoku Solver

This repository contains a Sudoku solver built around the CBMC model checker. The core idea is to encode Sudoku constraints as a C model and ask CBMC whether a valid grid exists for the given puzzle. If the assertion is reachable, CBMC produces a counterexample trace that the python wrapper converts back into a solved 9x9 grid.

## Contents:

- `src/sudoku.c`: the Sudoku constraint model expressed as a CBMC program
- `src/solve.py`: solves one puzzle and prints either a solution or "UNSOLVABLE"
- `src/solve_all.py`: enumerates all solutiuons by repeatedly blocking previously found ones
- `tools/check.py`: validates solver output against Sudoku rules and puzzle givens
- `tools/bench.py`: runs solver checks across the bundled test sets and reports timings
- `tools/bench_all.py`: runs solve_all.py over the multi-solution puzzles and reports counts, total and per-run costs
- `puzzles/`: sample inputs split into solvable, unsolvable and multi solution cases
- `results/timings.md`: full benchmark tables, SAT instance sizes and environment details

## Modelling approach

Each cell is a 9-bit mask rather than a digit. Bit `i` set means the cell holds digit `i+1`, and exactly one bit is set per cell. This one-hot encoding lets contraints be
expressead as bitwise operations instead of comparisons over 1-9, which produces smaller, more uniform clauses for CBMC's SAT backend.

**Cell validity**: `__CPROVER_assume(m != 0 && m <= 256 && (m & (m - 1)) == 0)` restricts the mask to exactly one bit in range. `m & (m -1)` clears the lowest set bit, so the result is zero only if m started with exactly one bit set.

**Givens**: each pre-filled cell gets an extra assume pinning its mask to that digit's bit, e.g. digit 5 becomes `1 << 4`.

**Row, column and box rules**: the nine masks in a group are OR-ed together. Since each cell contributes exactly one bit, the OR can only equal `0x1FF` (all 9 bits set) if the 9 cells are all different digits. One assume per group replaces the usual pairwise "cell A != cell B" checks.

**Feeding in the puzzle**: the wrapper never edits `sudoku.c`. It passes the puzzle as `-DPUZZLE={...}` macro on the CBMC command line, so the model source stays fixed across runs.

**Finding a solution**: the model ends in `assert(0)`, unreachable only if a valid grid exists. If CBMC finds a way to reach it (VERIFICATION FAILED, exit code 10), the counterexample trace contains that grid. If no valid grid exists, the assumes can never all hold and the assert stays unreachable (VERIFICATION SUCCESSFUL, exit code 0). The wrapper reads the exit code, not the printed message, to decide between UNSOLVABLE and parsing a trace.

**Part II**: reuses the Part I model unchanged. An `NBLOCK`/`BLOCKED` macro pair (default `NBLOCK=0`, no effect) lets the wrapper pass in previously found grids as flat mask arrays. For each one, an assume requires at least one cell to differ (XOR across all 81 cells is nonzero), forcing CBMC to find a genuinely new solution or report none remain.

**Why one-hot over pairwise inequality**: both were benchmarked and perform identically within noise (see `results/timings.md`). One-hot is kept for the smaller clause count (58446 vs 91098 clauses on the hardest test puzzle) and because it reads closer to the rules as stated, one assume per row, column and box.

## Puzzle format
Each puzzle file contains 81 digits with 0 used as empty. The digits are read as a flat sequence, row-major.
Example puzzle:

```text
5 3 0 0 7 0 0 0 0
6 0 0 1 9 5 0 0 0
0 9 8 0 0 0 0 6 0
8 0 0 0 6 0 0 0 3
4 0 0 8 0 3 0 0 1
7 0 0 0 2 0 0 0 6
0 6 0 0 0 0 2 8 0
0 0 0 4 1 9 0 0 5
0 0 0 0 8 0 0 7 9
```
Equivalent flat representation:

```text
530070000
600195000
098000060
800060003
400803001
700020006
060000280
000419005
000080079
```


The input parser expects exactly 81 single-digit tokens. The solver accepts a file path like the examples under `puzzles/`.

## Requirements

- CBMC v6.11.0
- python 3

To check CBMC is available:
```bash
cbmc --version
```

## Quick start

Solve a single puzzle:

```bash
python3 src/solve.py puzzles/solvable/wiki.txt
```

Expected output is either:

- `UNSOLVABLE`
- or a 9x9 grid in the following format:

```text
5 3 4 6 7 8 9 1 2
6 7 2 1 9 5 3 4 8
1 9 8 3 4 2 5 6 7
8 5 9 7 6 1 4 2 3
4 2 6 8 5 3 7 9 1
7 1 3 9 2 4 8 5 6
9 6 1 5 3 7 2 8 4
2 8 7 4 1 9 6 3 5
3 4 5 2 8 6 1 7 9
```
## Enumerating all solutions
To find every valid solution for a puzzle, use `solve_all.py`:

```bash
python3 src/solve_all.py puzzles/multi/two.txt
```
This repeatedly solves the puzzle, blocks each solution found and then continues until no more solutions remains. It 
prints each discovered grid and ends with:


```text
NUMBER OF SOLUTIONS: N
```

The `--silent` option suppresses the individual grid output:

```bash
python3 src/solve_all.py --silent puzzles/multi/forty.txt
```

## Checking and benchmarking
Validate a solver result against the puzzle and Sudoku constraints:
```bash
python3 src/solve.py puzzles/solvable/wiki.txt | python3 tools/check.py puzzles/solvable/wiki.txt
```

Run the bundled benchmark suite over the sample puzzles:

```bash
python3 tools/bench.py
```
This script runs each puzzle multiple times, checks correctness and prints timings.

Run the Part II benchmark over the multi-solution puzzles:

```bash
python3 tools/bench_all.py
```
This runs `solve_all.py` on each puzzle in `puzzles/multi/`, checks the solution count against the expected value, and reports totals and per-run timings.

## Performance notes

Full benchmark tables, SAT instance sizes and the test environment are in `results/timings.md`. Part I solves all 13 benchmark puzzles in under 1.1s each, 7.00s for the full suite.

Part II's iterative blocking re-runs CBMC once per solution found, and each run gets slightly slower as more blocking constraints accumulate, so total time grows roughly quadratically with the number of solutions. On `puzzles/multi/forty.txt` (40 solutions), per-run CBMC cost rises from 0.29s with none blocked to 3.01s with 40 blocked, for a total of 68s. Heavily underconstrained puzzles with hundreds of solutions or more would not finish in reasonable time with this approach.

## Puzzle sets

The repository includes several curated examples:

### Solvable puzzles

- `puzzles/solvable/wiki.txt`
- `puzzles/solvable/inkala_2012.txt`
- `puzzles/solvable/ai_escargot.txt`
- `puzzles/solvable/norvig_a.txt`
- `puzzles/solvable/norvig_b.txt`
- `puzzles/solvable/norvig_c.txt`
- `puzzles/solvable/clue17.txt`
- `puzzles/solvable/empty.txt`

### Unsolvable puzzles

- `puzzles/unsolvable/row.txt`
- `puzzles/unsolvable/hidden.txt`
- `puzzles/unsolvable/inkala_plus.txt`
- `puzzles/unsolvable/escargot_plus.txt`

### Multi-solution puzzles

- `puzzles/multi/two.txt`
- `puzzles/multi/five.txt`
- `puzzles/multi/forty.txt`

The included test set was checked against an independent brute-force solver to confirm the expected number of solutions and the unsolvable cases.

### Test set

Solution counts were verified with an independent backtracking solver, so the
expected values below do not depend on CBMC.

**Solvable.** Chosen to be hard for constraint solvers rather than for humans.

| File | Clues | Solutions | Source |
|------|-------|-----------|--------|
| `wiki.txt` | 30 | 1 | the worked example in the assignment brief, from the Wikipedia Sudoku article |
| `ai_escargot.txt` | 23 | 1 | AI Escargot, Arto Inkala, 2006 |
| `inkala_2012.txt` | 21 | 1 | Arto Inkala, June 2012, via sudokuwiki.org |
| `norvig_a.txt` | 22 | 1 | line 1 of norvig.com/hardest.txt |
| `norvig_b.txt` | 23 | 1 | line 2 of norvig.com/hardest.txt |
| `norvig_c.txt` | 26 | 1 | line 3 of norvig.com/hardest.txt |
| `clue17.txt` | 17 | 1 | a 17-clue puzzle; Final Sudoku |
| `empty.txt` | 0 | 6.67 x 10^21 | no givens, the worst case for the givens constraints |

**Unsolvable.** Three of the four add a single conflicting given to a solvable
puzzle; in two of those the conflict is invisible in any one row, column or box.

| File | Clues | Built from | Contradiction |
|------|-------|------------|---------------|
| `row.txt` | 31 | `wiki.txt`, 5 added at r1c3 | duplicate digit in a row, visible at once |
| `hidden.txt` | 9 | constructed directly | row 1 forces r1c3 = 3, clashing with the 3 in column 3 |
| `escargot_plus.txt` | 24 | `ai_escargot.txt`, 3 added at r3c8 | no duplicate among the givens, only reachable after propagation |
| `inkala_plus.txt` | 22 | `inkala_2012.txt`, 2 added at r2c1 | as above |

**Multi-solution.** All three are clue-strippings of the solved `wiki.txt` grid.

| File | Clues | Solutions |
|------|-------|-----------|
| `two.txt` | 43 | 2 |
| `five.txt` | 37 | 5 |
| `forty.txt` | 30 | 40 |

Sources:

Norvig, P. (n.d.). *Hardest sudoku puzzles.* https://norvig.com/hardest.txt
Stuart, A. (2012). *Arto Inkala sudoku.* https://www.sudokuwiki.org/Arto_Inkala_Sudoku
Stuart, A. (2008). *Escargot.* https://www.sudokuwiki.org/Escargot
Final Sudoku. (2026). *17-Clue Sudoku - The Mathematical Minimum.* https://finalsudoku.com/sudoku-17-clue