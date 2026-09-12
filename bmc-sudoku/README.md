# BMC Sudoku Assignment

This project uses the CBMC model checker to verify whether a given Sudoku puzzle is solvable and, if so, to extract a valid solution.

## Overview

- The model is implemented in [src/sudoku.c](src/sudoku.c).
- A Python wrapper in [src/solve.py](src/solve.py) invokes CBMC, parses the counterexample trace, and prints the solved grid.
- Puzzle input files live in [puzzles/](puzzles/).
- Output and generated artifacts are stored in [results/](results/).

## Puzzle format

Each puzzle file contains 81 digits, with `0` representing an empty cell.

Example:

```
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

## Run

Make sure CBMC is installed, then run:

```bash
python3 src/solve.py puzzles/your_puzzle.txt
```

The script prints either:

- `UNSOLVABLE` if no valid solution exists, or
- the solved 9x9 grid if a valid solution is found.

## Files

- [src/sudoku.c](src/sudoku.c): Sudoku constraints and CBMC assertion model
- [src/solve.py](src/solve.py): wrapper that runs CBMC and parses results
- [puzzles/](puzzles/): puzzle inputs
- [results/](results/): generated logs and traces
- [tools/](tools/): supporting scripts or utilities

Shrinking the formula is not the same as going faster. Every winner here reduced the number of C statements, not the variable count.


# Puzzle sources

Test set for the CBMC Sudoku solver. Every puzzle below was checked with an
independent brute-force solver to confirm its givens are consistent and to
count its solutions.

## Solvable

| File | Source |
|---|---|
| `wiki.txt` | Example puzzle from the Wikipedia Sudoku article. <https://en.wikipedia.org/wiki/Sudoku> |
| `inkala_2012.txt` | Arto Inkala's 2012 puzzle, widely reported as the world's hardest. Grid taken from SudokuWiki's article on it. <https://www.sudokuwiki.org/Arto_Inkala_Sudoku> |
| `ai_escargot.txt` | "AI Escargot", Arto Inkala, 2006. Grid taken from SudokuWiki. <https://www.sudokuwiki.org/Escargot> |
| `norvig_a.txt` | Line 1 of `hardest.txt` from Peter Norvig's essay "Solving Every Sudoku Puzzle". <https://norvig.com/sudoku.html>, file at <https://norvig.com/hardest.txt> |
| `norvig_b.txt` | Line 2 of the same file. |
| `norvig_c.txt` | Line 3 of the same file. |
| `clue17.txt` | First puzzle in the 49,151-puzzle collection of 17-clue Sudokus (Gordon Royle's collection, distributed by Mladen Dobrichev). 17 is the minimum clue count for a unique solution. <https://sites.google.com/site/dobrichev/sudoku-puzzle-collections>, identified in <http://forum.enjoysudoku.com/17-clue-puzzle-difficulty-t38179.html> |
| `empty.txt` | Own construction: no givens at all. Any valid grid is a correct answer. |

The Inkala 2012 grid has 21 givens. Several 2012 news reports said 23, but the
grid published on Inkala's own site and reproduced on SudokuWiki has 21.

## Unsolvable

All four are own constructions. No published source needed, since the point is
only to check that the solver prints `UNSOLVABLE`.

| File | How it was built |
|---|---|
| `row.txt` | `wiki.txt` with a second 5 added to row 1. Clash visible on the grid. |
| `hidden.txt` | Row 1 has eight givens, so cell (1,3) must be 3, but column 3 already has a 3. No clash between any two givens. |
| `inkala_plus.txt` | `inkala_2012.txt` with cell (2,1) set to 2, where the unique solution has 9. |
| `escargot_plus.txt` | `ai_escargot.txt` with cell (3,8) set to 3, where the unique solution has 2. |

The `_plus` puzzles are the useful cases. A puzzle with a unique solution plus
one extra digit that differs from that solution has no solution at all, and the
added digit clashes with no given, so a full search is needed to prove it.

## Verification

Solution counts, from an independent backtracking solver:

- `wiki`, `inkala_2012`, `ai_escargot`, `norvig_a`, `norvig_b`, `norvig_c`,
  `clue17`: exactly 1 solution each
- `empty`: many solutions (6,670,903,752,021,072,936,960 grids exist)
- all four unsolvable puzzles: 0 solutions