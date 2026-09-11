# wrapper for sudoku model
# stdout: solutioin or UNSOLVABLE only; err -> stderr

import os
import re
import subprocess
import sys

# model lives next to script
MODEL_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "sudoku.c")

CBMC_SUCCESS = 0 # assert unreachable -> unsolvable
CBMC_FAILURE = 10 # assert reachable -> solution exists

CBMC_FLAGS = ["--trace", "--no-bounds-check", "--no-signed-overflow-check"] # need vals in output; checks not needed here, only add formula size

def read_puzzle(path): # read puzzle file -> lst of 81 ints (0=empty)
    with open(path) as f:
        tokens = f.read().split()
    if len(tokens) != 81 or not all(t in "0123456789" and len(t) == 1 for t in tokens):
        sys.exit("error: puzzle must be 81 digits (0-9)")
    return [int(t) for t in tokens]

def run_cbmc(puzzle): # Run CBMC on model with given puzzle. Returns (exit code, stdout)
    define = "-DPUZZLE={" + ",".join(map(str, puzzle)) + "}"
    result = subprocess.run(["cbmc", MODEL_PATH, define] + CBMC_FLAGS, capture_output=True, text=True)
    return result.returncode, result.stdout

def parse_grid(trace): # Find 'g={{...}, ...}' line in trace -> list of 81 ints
    for line in trace.splitlines():
        if line.startswith("g={{"):
            vals = line.split(" (")[0] # drop bin part
            nums = [int(x) for x in re.findall(r'\d+', vals)]
            if len(nums) == 81:
                return nums
    return None

def format_grid(grid): # 81 ints -> 9 lines, space separated
    return "\n".join(" ".join(map(str, grid[r * 9: r * 9 + 9])) for r in range(9))

def main():
    if len(sys.argv) != 2:
        sys.exit("usage: solve.py <puzzle_file>")
    puzzle = read_puzzle(sys.argv[1])
    code, trace = run_cbmc(puzzle)
    if code == CBMC_SUCCESS:
        print("UNSOLVABLE")
    elif code == CBMC_FAILURE:
        grid = parse_grid(trace)
        if grid:
            print(format_grid(grid))
        else:
            print("UNSOLVABLE")
    else:
        sys.exit("error: CBMC failed")

if __name__ == "__main__":
    main()