#!/usr/bin/env bash
#
# verify.sh
#
# Regenerates every verification result reported for the Othello model.
# Run from the directory containing model.pml.
#
#   ./verify.sh            run everything
#   ./verify.sh quick      skip the two exhaustive runs (much faster)
#
# Outputs:
#   results/<run>.trail    error trail for each failing run
#   results/logs/<run>.txt full pan output for each run
#   results/summary.txt    one line per run, the table for the report
#
# Notes on flag placement, both of which are easy to get wrong:
#   -DNUM_WORKERS etc. go to spin, because model.pml uses them and spin
#     runs the preprocessor when it generates pan.c. Passing them to gcc
#     is silently ignored.
#   -DNOCLAIM and -DBITSTATE go to gcc, because pan.c uses them.

set -u

MODEL=model.pml
OUT=results
LOGS=$OUT/logs
SUMMARY=$OUT/summary.txt

# Wall clock limit for a single pan run. The exhaustive properties can
# exceed this on a small machine; a timeout is recorded, not fatal.
LIMIT=${LIMIT:-3600}

QUICK=0
if [ "${1:-}" = "quick" ]; then QUICK=1; fi

mkdir -p "$LOGS"
: > "$SUMMARY"

if [ ! -f "$MODEL" ]; then
  echo "error: $MODEL not found. Run this from the othello directory." >&2
  exit 1
fi

# ---------------------------------------------------------------------
# generate <spin flags...>
#   Regenerates pan.c and rebuilds both binaries. Must be called before
#   every group of runs, and both binaries must be rebuilt every time,
#   because a stale binary silently reports results for the wrong model.
# ---------------------------------------------------------------------
generate() {
  CFG="$*"
  echo "==> spin $CFG -a $MODEL"
  spin $CFG -a "$MODEL" > /dev/null || exit 1
  gcc -O2 -o pan pan.c 2> /dev/null
  gcc -O2 -DNOCLAIM -o pan_noclaim pan.c 2> /dev/null
}

# ---------------------------------------------------------------------
# record <run name> <log file>
#   Pulls the reportable numbers out of a pan log and appends one line
#   to the summary.
# ---------------------------------------------------------------------
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

  # A run that stops at the first error explores only part of the state
  # space, so its unreached report proves nothing about coverage.
    if [ "$verdict" = "INCOMPLETE" ]; then
    # Killed or timed out. The log has no coverage report at all, so
    # nothing can be claimed about it either way.
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

# ---------------------------------------------------------------------
# claim <run name> <property name> <pan flags...>
# ---------------------------------------------------------------------
claim() {
  name=$1; prop=$2; shift 2
  echo "--> $name"
  timeout "$LIMIT" ./pan -a -N "$prop" "$@" > "$LOGS/$name.txt" 2>&1
  if [ -f "$MODEL.trail" ]; then mv "$MODEL.trail" "$OUT/$name.trail"; fi
  record "$name" "$LOGS/$name.txt"
}

# ---------------------------------------------------------------------
# deadlock <run name> <pan flags...>
#   Uses pan_noclaim, because while any ltl block is present pan treats
#   it as a never claim and disables the invalid end state check.
# ---------------------------------------------------------------------
deadlock() {
  name=$1; shift
  echo "--> $name"
  timeout "$LIMIT" ./pan_noclaim "$@" > "$LOGS/$name.txt" 2>&1
  if [ -f "$MODEL.trail" ]; then mv "$MODEL.trail" "$OUT/$name.trail"; fi
  record "$name" "$LOGS/$name.txt"
}

banner() { echo; echo "### $1"; echo "--- $1" >> "$SUMMARY"; }

# =====================================================================
banner "A: three workers, one round (matches comm_sz 4 in configs.py)"
generate -DNUM_WORKERS=3 -DMAX_MOVES=4 -DMAX_ROUNDS=1
claim    no_eval_chosen_w3r1   no_eval_chosen   -m100000
claim    best_is_maximal_w3r1  best_is_maximal  -m100000
claim    workers_finish_w3r1   workers_finish   -m100000
deadlock deadlock_w3r1                          -m100000
if [ "$QUICK" = "0" ]; then
  claim  no_lost_results_w3r1  no_lost_results  -m100000
fi

# =====================================================================
banner "B: three workers, zero rounds (minimal termination witness)"
generate -DNUM_WORKERS=3 -DMAX_MOVES=4 -DMAX_ROUNDS=0
deadlock deadlock_w3r0                          -m100000
claim    workers_finish_w3r0   workers_finish   -m100000

# =====================================================================
banner "C: three workers, two rounds (match reset and per-round reset)"
generate -DNUM_WORKERS=3 -DMAX_MOVES=4 -DMAX_ROUNDS=2
claim    no_eval_chosen_w3r2   no_eval_chosen   -m100000
claim    best_is_maximal_w3r2  best_is_maximal  -m100000

# =====================================================================
banner "D: two workers, one round (reduced, exhaustive)"
generate -DNUM_WORKERS=2 -DMAX_MOVES=3 -DMAX_ROUNDS=1
claim    no_eval_chosen_w2r1   no_eval_chosen   -m60000
claim    best_is_maximal_w2r1  best_is_maximal  -m60000
claim    workers_finish_w2r1   workers_finish   -m60000
deadlock deadlock_w2r1                          -m60000
if [ "$QUICK" = "0" ]; then
  claim  no_lost_results_w2r1  no_lost_results  -m60000
fi

# =====================================================================
banner "E: fault isolation on best_is_maximal, two workers"
generate -DNUM_WORKERS=2 -DMAX_MOVES=3 -DMAX_ROUNDS=1
claim    fault_none            best_is_maximal  -m60000

generate -DNUM_WORKERS=2 -DMAX_MOVES=3 -DMAX_ROUNDS=1 -DNO_TIME_CUTOFF
claim    fault_no_cutoff       best_is_maximal  -m60000

generate -DNUM_WORKERS=2 -DMAX_MOVES=3 -DMAX_ROUNDS=1 -DNO_LEAK
claim    fault_no_leak         best_is_maximal  -m60000

generate -DNUM_WORKERS=2 -DMAX_MOVES=3 -DMAX_ROUNDS=1 -DNO_TIME_CUTOFF -DNO_LEAK
claim    fault_both            best_is_maximal  -m60000

# =====================================================================
# Leave the tree in the main configuration so that a manual replay after
# the script finishes uses the same model the block A trails came from.
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
