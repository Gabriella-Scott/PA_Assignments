# wrapper for sudoku model
# stdout: solutioin or UNSOLVABLE only; err -> stderr

import os
import re
import subprocess
import sys

# model lives next to script
MODEL_PATH = os.path.join(os.path.dirname(
    os.path.abspath(__file__)), "sudoku.c")

CBMC_SUCCESS = 0  # assert unreachable -> unsolvable
CBMC_FAILURE = 10  # assert reachable -> solution exists

# need vals in output; checks not needed here, only add formula size
CBMC_FLAGS = ["--trace", "--no-bounds-check", "--no-signed-overflow-check"]


def read_puzzle(path):  # read puzzle file -> lst of 81 ints (0=empty)
    with open(path) as f:
        text = f.read()
    tokens = text.split()  # digits separated by whitespace
    if len(tokens) != 81:  # Fall back
        tokens = [c for c in text if c in "0123456789"]
    if len(tokens) != 81 or not all(t in "0123456789" and len(t) == 1 for t in tokens):
        sys.exit("error: puzzle must be 81 digits (0-9)")
    return [int(t) for t in tokens]


def run_cbmc(puzzle, extra=()):  # run CBMC on model -> (exit code, stdout)
    define = "-DPUZZLE={" + ",".join(map(str, puzzle)) + "}"
    result = subprocess.run(["cbmc", MODEL_PATH, define] + list(extra) + CBMC_FLAGS,
                            capture_output=True, text=True)
    return result.returncode, result.stdout


# Find 'g={..}' block in trace -> list of 81 ints
def parse_grid(trace):
    lines = trace.splitlines()  # split trace into individual lines
    for i, line in enumerate(lines):  # iterate over each line in trace
        if line.startswith("  g={"):  # found start
            text = ""
            for part in lines[i:]:  # value may wrap over 9 lines
                text += part.split(" ({")[0]  # drop binary dump
                if " ({" in part:
                    break
            # extract all numbers from text
            masks = [int(x) for x in re.findall(r'\d+', text)]
            if len(masks) == 81:
                return [m.bit_length() for m in masks]  # 1<<(d-1) -> d
    return None


def format_grid(grid, one_line=False):  # 81 ints -> 9 lines, or 81 digits on 1 line
    if one_line:
        return "".join(map(str, grid))
    return "\n".join(" ".join(map(str, grid[r * 9: r * 9 + 9])) for r in range(9))


def solve_one(puzzle, one_line=False):
    code, trace = run_cbmc(puzzle)
    if code == CBMC_SUCCESS:
        print("UNSOLVABLE")
    elif code == CBMC_FAILURE:
        grid = parse_grid(trace)
        if grid is not None:
            print(format_grid(grid, one_line))
        else:
            # wrapper bug unsolvable
            sys.exit("error: no grid found in cbmc trace")
    else:
        sys.exit("error: CBMC failed")


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: solve.py <puzzle_file>")
    solve_one(read_puzzle(sys.argv[1]))


if __name__ == "__main__":
    main()
