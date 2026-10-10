"""
Transfer test script over the real test cases
    python3 tools/test_transfer.py                 run every case in testcases/
    python3 tools/test_transfer.py -v test3        trace one case node by node
"""


import os
import sys
import json

# tools/ sits next to analysis/, so the assignment root goes on the path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from analysis.transfer import Transfer
from analysis.store import Store
from analysis.graph import load_case, CFG_KINDS


CASES = os.path.join(os.path.dirname(
    os.path.abspath(__file__)), "..", "testcases")


def enclosing_statement(graph, node_id):
    """Walk up the ast until a node that is actually on the cfg. The sink is an identifier,
    which has no cfg edges, so its taint has to be read in the IN store of the statement that contains it"""
    curr = node_id
    while curr is not None:
        if graph.kind_of(curr) in CFG_KINDS:
            return curr
        parent = graph.parent(curr)
        curr = parent.id if parent else None
    return None


def boundary_store(graph, source_node):
    """IN store at entry of function holding the source. Every param is a known name, and source param starts tainted"""
    method = graph.owning_method(source_node)
    store = Store()
    for param in graph.parameters(method.id):
        store.assign(param.val, param.id == source_node, set())
    return method, store


def solve(graph, source_node, trace=False):
    transfer = Transfer(graph)
    method, entry = boundary_store(graph, source_node)

    in_store = {n: Store() for n in graph.cfg_nodes(method.id)}
    in_store[method.id] = entry

    worklist = [method.id]
    while worklist:
        node = worklist.pop()
        out = transfer.apply(node, in_store[node])
        if trace:
            print("  %-34s %s" % (graph.node(node), out))
        for succ in graph.successors(node):
            merged = in_store[succ].join(out)
            # only re-queue when the IN set actually grew, else this never ends
            if merged != in_store[succ]:
                in_store[succ] = merged
                worklist.append(succ)

    return transfer, in_store


def reaches(directory, trace=False):
    graph, case = load_case(directory)
    transfer, in_store = solve(graph, case.source_node, trace)

    statement = enclosing_statement(graph, case.sink_node)
    if statement is None or statement not in in_store:
        # sink lives in a function this analysis never entered: part 2's job
        return False
    return transfer.eval_expr(graph.node(case.sink_node), in_store[statement]).tainted


def needs_part2(graph):
    return len(graph.methods()) > 1


def expected_of(directory):
    """
    Only the test harness may read this field. The analyser itself must not,
    and does not: load_case drops it.
    """
    with open(os.path.join(directory, "test_case.json")) as f:
        return json.load(f)["reaches"]


def main():
    args = sys.argv[1:]
    trace = "-v" in args
    only = [a for a in args if a != "-v"]

    names = sorted(only or os.listdir(CASES))
    intra_failures = 0

    for name in names:
        directory = os.path.join(CASES, name)
        if not os.path.isdir(directory):
            continue

        graph, _ = load_case(directory)
        part2 = needs_part2(graph)

        if trace:
            print("%s:" % name)
        got = reaches(directory, trace)
        want = expected_of(directory)

        if part2:
            # a call into a defined function, so the intra analysis is allowed to be wrong
            mark = "part 2" if got != want else "ok"
        elif got == want:
            mark = "ok"
        else:
            mark = "FAIL"
            intra_failures += 1

        print("%-14s expected=%-5s got=%-5s %s" % (name, want, got, mark))

    print()
    if intra_failures:
        print("%d intraprocedural case(s) failing" % intra_failures)
    else:
        print("all intraprocedural cases passing")
    return 1 if intra_failures else 0


if __name__ == "__main__":
    sys.exit(main())
