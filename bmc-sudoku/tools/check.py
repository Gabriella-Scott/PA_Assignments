# Format: exactly 9 lines, 9 digits per line, single spaces, no zeros
# Givens: every digit in puzzle is unchanged in soln
# Rules: every row, col and box contains exactly the digits 1 to 9

# P = puzzle file, slover output read from stdin
# exit code: 0 = valid, 1 = invalid, 2 = solver said UNSOLVABLE

import sys

DIGITS = set("123456789")  # allowed output tokens (no 0)
FULL = set(range(1, 10))  # what every group must contain


def read_puzzle(path):  # puzzle file -> 9x9 list of ints (0=empty)
    with open(path) as f:
        nums = [int(t) for t in f.read().split()]
    return [nums[r * 9: r * 9 + 9] for r in range(9)]


def parse_output(text):  # solver output -> 9x9 grid or None, format errors
    lines = text.strip("\n").split("\n")
    if len(lines) != 9:
        return None, [f"expected 9 lines, got {len(lines)}"]
    grid, errs = [], []
    for i, line in enumerate(lines):
        tokens = line.split(" ")  # catches double spaces
        if len(tokens) != 9 or not all(t in DIGITS for t in tokens):
            errs.append(f"line {i + 1}: badly formatted: '{line}'")
        else:
            grid.append([int(t) for t in tokens])
    return (grid if not errs else None), errs


def check_givens(puzzle, grid):  # givens -> unchanged
    errs = []
    for r in range(9):
        for c in range(9):
            if puzzle[r][c] != 0 and grid[r][c] != puzzle[r][c]:
                errs.append(f"given changed at row {r + 1}, col {c + 1}: "
                            f"{puzzle[r][c]} -> {grid[r][c]}")

    return errs

def check_groups(grid):  # every row, col, box contains 1..9
    errs = []
    # check rows
    for i in range(9):
        row = {grid[i][c] for c in range(9)}
        col = {grid[r][i] for r in range(9)}
        box = {grid[3 * (i // 3)+ k // 3][3 * (i % 3) + k % 3] for k in range(9)}
        if row != FULL:
            errs.append(f"row {i + 1} missing {sorted(FULL - row)}")
        if col != FULL:
            errs.append(f"col {i + 1} missing {sorted(FULL - col)}")
        if box != FULL:
            errs.append(f"box {i + 1} missing {sorted(FULL - box)}")

    return errs

def main():
    if len(sys.argv) != 2:
        sys.exit("usage: python3 src/solve.py P | python3 tools/check.py P")
    puzzle = read_puzzle(sys.argv[1])
    output = sys.stdin.read()
    
    if output.strip() == "UNSOLVABLE":
        print("UNSOLVABLE (no grid to check)")
        sys.exit(2)
    grid, errs = parse_output(output)
    if grid is not None:
        errs = check_givens(puzzle, grid) + check_groups(grid)
        
    if errs:
        print("INVALID")
        for e in errs:
            print(" " + e)
        sys.exit(1)
    print("OK")

if __name__ == "__main__":
    main()