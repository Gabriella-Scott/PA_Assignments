# Regenerates every verification result reported for the Othello model.
# Run from the directory containing model.pml.
# ./verify.sh - run everything
# ./verify.sh quick - skip the two exhaustive runs (much faster)
#
# Outputs:
# results/<run>.trail   error trail for each failing run
# results/logs/<run>.txt   pan output for each run
# results/summary.txt   one line per run, the table for the report
set -u

MODEL=model.pml
OUT=results
LOGS=$OUT/logs
SUMMARY=$OUT/summary.txt

# per-run wall clock limit; exhaustive runs may exceed on slow machines
LIMIT=${LIMIT:-3600}

QUICK=0
if [ "${1:-}" = "quick" ]; then QUICK=1; fi

mkdir -p "$LOGS"
: > "$SUMMARY"

if [ ! -f "$MODEL" ]; then
  echo "error: $MODEL not found. Run this from the othello directory." >&2
  exit 1
fi

# generate <spin flags...>
#   rebuild pan.c and both binaries; stale binary silently reports wrong model
generate() {
  CFG="$*"
  echo "==> spin $CFG -a $MODEL"
  spin $CFG -a "$MODEL" > /dev/null || exit 1
  gcc -O2 -o pan pan.c 2> /dev/null
  gcc -O2 -DNOCLAIM -o pan_noclaim pan.c 2> /dev/null
}

# record <run name> <log file>
#   pull key numbers from pan log, append one line to summary
record() {
  name=$1; log=$2

  if grep -q "errors: 0" "$log"; then
    verdict="HOLDS"
  elif grep -q "errors:" "$log"; then
    verdict="VIOLATED"
  else
    verdict="INCOMPLETE"
  fi

  depth=$(grep -oE "at depth [0-9]+" "$log" | head -1 | grep -oE "[0-9]+")
  states=$(grep -E "states, stored" "$log" | head -1 | awk '{print $1}')
  secs=$(grep -oE "elapsed time [0-9.]+" "$log" | head -1 | awk '{print $3}')

  # stopped at first error; unreached report doesn't prove coverage
    if [ "$verdict" = "INCOMPLETE" ]; then
    # killed or timed out; no coverage info
    coverage="unknown"
  elif grep -q "Search not completed" "$log"; then
    coverage="partial"
  elif grep -qE "\(0 of [0-9]+ states\)" "$log"; then
    coverage="full, 0 unreached"
  else
    coverage="full"
  fi

  printf '%-34s %-11s depth=%-6s states=%-12s %-7s %s\n' \
    "$name" "$verdict" "${depth:--}" "${states:--}" "${secs:--}s" "$coverage" \
    >> "$SUMMARY"
}

# claim <run name> <property name> <pan flags...>
claim() {
  name=$1; prop=$2; shift 2
  echo "--> $name"
  timeout "$LIMIT" ./pan -a -N "$prop" "$@" > "$LOGS/$name.txt" 2>&1
  if [ -f "$MODEL.trail" ]; then mv "$MODEL.trail" "$OUT/$name.trail"; fi
  record "$name" "$LOGS/$name.txt"
}

# deadlock <run name> <pan flags...>
#   uses pan_noclaim: active ltl claim disables invalid end state check
deadlock() {
  name=$1; shift
  echo "--> $name"
  timeout "$LIMIT" ./pan_noclaim "$@" > "$LOGS/$name.txt" 2>&1
  if [ -f "$MODEL.trail" ]; then mv "$MODEL.trail" "$OUT/$name.trail"; fi
  record "$name" "$LOGS/$name.txt"
}

banner() { echo; echo "### $1"; echo "--- $1" >> "$SUMMARY"; }

banner "A: three workers, one round (matches comm_sz 4 in configs.py)"
generate -DNUM_WORKERS=3 -DMAX_MOVES=4 -DMAX_ROUNDS=1
claim    no_eval_chosen_w3r1   no_eval_chosen   -m100000
claim    best_is_maximal_w3r1  best_is_maximal  -m100000
claim    workers_finish_w3r1   workers_finish   -m100000
deadlock deadlock_w3r1                          -m100000
if [ "$QUICK" = "0" ]; then
  claim  no_lost_results_w3r1  no_lost_results  -m100000
fi

banner "B: three workers, zero rounds (minimal termination witness)"
generate -DNUM_WORKERS=3 -DMAX_MOVES=4 -DMAX_ROUNDS=0
deadlock deadlock_w3r0                          -m100000
claim    workers_finish_w3r0   workers_finish   -m100000

banner "C: three workers, two rounds (match reset and per-round reset)"
generate -DNUM_WORKERS=3 -DMAX_MOVES=4 -DMAX_ROUNDS=2
claim    no_eval_chosen_w3r2   no_eval_chosen   -m100000
claim    best_is_maximal_w3r2  best_is_maximal  -m100000

banner "D: two workers, one round (reduced, exhaustive)"
generate -DNUM_WORKERS=2 -DMAX_MOVES=3 -DMAX_ROUNDS=1
claim    no_eval_chosen_w2r1   no_eval_chosen   -m60000
claim    best_is_maximal_w2r1  best_is_maximal  -m60000
claim    workers_finish_w2r1   workers_finish   -m60000
deadlock deadlock_w2r1                          -m60000
if [ "$QUICK" = "0" ]; then
  claim  no_lost_results_w2r1  no_lost_results  -m60000
fi

banner "E: fault isolation on best_is_maximal, two workers"
generate -DNUM_WORKERS=2 -DMAX_MOVES=3 -DMAX_ROUNDS=1
claim    fault_none            best_is_maximal  -m60000

generate -DNUM_WORKERS=2 -DMAX_MOVES=3 -DMAX_ROUNDS=1 -DNO_TIME_CUTOFF
claim    fault_no_cutoff       best_is_maximal  -m60000

generate -DNUM_WORKERS=2 -DMAX_MOVES=3 -DMAX_ROUNDS=1 -DNO_LEAK
claim    fault_no_leak         best_is_maximal  -m60000

generate -DNUM_WORKERS=2 -DMAX_MOVES=3 -DMAX_ROUNDS=1 -DNO_TIME_CUTOFF -DNO_LEAK
claim    fault_both            best_is_maximal  -m60000

# restore block A config; manual replays use the same model as those trails
generate -DNUM_WORKERS=3 -DMAX_MOVES=4 -DMAX_ROUNDS=1

echo
echo "======================================================================"
cat "$SUMMARY"
echo "======================================================================"
echo "Full pan output: $LOGS/"
echo "Error trails:    $OUT/*.trail"
echo
echo "To replay a trail, repeat the same -D flags the run used, e.g."
echo "  spin -DNUM_WORKERS=3 -DMAX_MOVES=4 -DMAX_ROUNDS=1 \\"
echo "       -t -p -k $OUT/no_eval_chosen_w3r1.trail $MODEL"
