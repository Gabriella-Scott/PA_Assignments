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
