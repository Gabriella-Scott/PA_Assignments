# Part II bench: solution counts, total time, per-iteration cost
# expected counts come from an independent backtracking solver, not CBMC
from solve_all import block_defines
import solve
import os
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "src"))


CASES = [("puzzles/unsolvable/hidden.txt", 0),   # puzzle, expected count
         ("puzzles/solvable/wiki.txt", 1),
         ("puzzles/multi/two.txt", 2),
         ("puzzles/multi/five.txt", 5),
         ("puzzles/multi/forty.txt", 40)]

PROFILE = "puzzles/multi/forty.txt"  # per-iteration breakdown for this one


def run(path):  # -> per-iteration CBMC times; last run finds nothing
    puzzle = solve.read_puzzle(os.path.join(REPO, path))
    found, times = [], []
    while True:
        extra = block_defines(found) if found else []
        start = time.perf_counter()
        code, out = solve.run_cbmc(puzzle, extra)
        times.append(time.perf_counter() - start)
        if code != solve.CBMC_FAILURE:
            break
        found.append(solve.parse_grid(out))
    return times


def main():
    profile = []
    print("| Puzzle | Expected | Found | Time (s) |")
    print("|--------|----------|-------|----------|")
    for path, expected in CASES:
        times = run(path)
        n = len(times) - 1
        flag = "" if n == expected else " MISMATCH"
        print(f"| {path} | {expected} | {n}{flag} | {sum(times):.2f} |")
        print(f"{path}: {n} in {sum(times):.2f}s", file=sys.stderr)
        if path == PROFILE:
            profile = times

    if profile:  # cost per run grows as blocking constraints pile up
        print()
        print(f"Per-run CBMC cost, `{PROFILE}`:")
        print()
        print("| Solutions blocked | CBMC time (s) |")
        print("|-------------------|---------------|")
        for i in range(0, len(profile), 10):
            print(f"| {i} | {profile[i]:.2f} |")


if __name__ == "__main__":
    main()
