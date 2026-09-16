# Part 2: finds every solution to a puzzle
# loop: solve, feed the solution back as a blocking constraint so the next run must differ somewhere
# Stop when CBMC says SUCCESSFUL (no grid left). Reuses Part 1 model + wrapper

import sys

from solve import (CBMC_FAILURE, CBMC_SUCCESS, read_puzzle,
                   run_cbmc, parse_grid, format_grid)


def block_defines(found):  # previous solutions -> cbmc -D flags
    masks = [1 << (d-1) for grid in found for d in grid]  # digit -> bit
    return ["-DNBLOCK=" + str(len(found)), "-DBLOCKED={" + ",".join(map(str, masks)) + "}"]


def main():
    args = sys.argv[1:]
    silent = "--silent" in args  # only print the count
    args = [a for a in args if a != "--silent"]
    if len(args) != 1:
        sys.exit("Usage: solve_all.py [--silent] <puzzle_file>")
    puzzle = read_puzzle(args[0])
    found = []
    while True:
        extra = block_defines(found) if found else []
        code, trace = run_cbmc(puzzle, extra)

        if code == CBMC_SUCCESS:  # nothing left to find
            break
        if code != CBMC_FAILURE:
            sys.exit("error: CBMC failed")
        grid = parse_grid(trace)
        if grid is None:
            sys.exit("error: no grid found in cbmc trace")
        found.append(grid)
        if not silent:
            print(format_grid(grid))
            print()  # blank line between boards

    print("NUMBER OF SOLUTIONS: " + str(len(found)))


if __name__ == "__main__":
    main()
