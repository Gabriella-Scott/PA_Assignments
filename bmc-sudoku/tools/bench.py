# Runs solver over all test puzzles, checks + times each
# time = best of REPEATS runs -> noise adds time
import os
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOLVER = os.path.join(REPO, "src", "solve.py")
CHECKER = os.path.join(REPO, "tools", "check.py")
PUZZLES = os.path.join(REPO, "puzzles")

REPEATS = 3 # runs per puzzle
TIMEOUT = 300 # secs per run, stops a hang blocking everything

def run_solver(puzzle): # -> (exit code, stdout, seconds); code None = timeout
    start = time.perf_counter()
    try:
        result = subprocess.run([sys.executable, SOLVER, puzzle], capture_output=True, text=True, timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        return (None, "", TIMEOUT)
    return result.returncode, result.stdout, time.perf_counter() - start

def is_correct(folder, puzzle, output): # judge output by folder rule
    if folder == "unsolvable":
        return output.strip() == "UNSOLVABLE"
    check = subprocess.run([sys.executable, CHECKER, puzzle], input=output, capture_output=True, text=True)
    return check.returncode == 0

def main():
    rows = []
    failures = 0
    for folder in ["solvable", "unsolvable"]:
        folder_path = os.path.join(PUZZLES, folder)
        for name in sorted(os.listdir(folder_path)):
            puzzle = os.path.join(folder_path, name)
            times, status = [], "PASS"
            for _ in range(REPEATS):
                code, output, secs = run_solver(puzzle)
                times.append(secs)
                if code is None:
                    status = "TIMEOUT"
                elif code != 0:
                    status = "ERROR" # solver crashed/cbmc err
                elif not is_correct(folder, puzzle, output):
                    status = "FAIL"
                if status != "PASS":
                    break # no point repeating a failure
            if status != "PASS":
                failures += 1
            rows.append((f"{folder}/{name}", status, min(times)))
            print(f"{folder}/{name}: {status}, time: {min(times):.2f}s", file=sys.stderr)

    total = sum(t for _, _, t in rows)
    print("| Puzzle | Result | Time (s) |")
    print("|--------|--------|-----------|")
    for name, status, secs in rows:
        print(f"| {name} | {status} | {secs:.2f} |")
    print(f"| **Total** | | {len(rows) - failures}/{len(rows)} passed | {total:.2f}|")
    sys.exit(1 if failures else 0)
    
if __name__ == "__main__":
    main()