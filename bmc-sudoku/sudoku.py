# main entry point
# usage: python3 sudoku.py [--exhaustive] [--silent] [--single-line-output] <puzzle_file>

import os
import sys

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "src")
FLAGS = ["--exhaustive", "--silent", "--single-line-output"]
USAGE = "usage: sudoku.py [--exhaustive] [--silent] [--single-line-output] <puzzle_file>"


def main():
    # imports here: src/ on path first, formatters won't move them
    sys.path.insert(0, SRC)
    from solve import read_puzzle, solve_one
    from solve_all import solve_all

    args = sys.argv[1:]
    for a in args:
        if a.startswith("--") and a not in FLAGS:
            sys.exit("error: unknown option " + a + "\n" + USAGE)
    files = [a for a in args if a not in FLAGS]
    if len(files) != 1:
        sys.exit(USAGE)

    puzzle = read_puzzle(files[0])
    one_line = "--single-line-output" in args
    if "--exhaustive" in args:
        solve_all(puzzle, "--silent" in args, one_line)
    else:
        solve_one(puzzle, one_line)


if __name__ == "__main__":
    main()
