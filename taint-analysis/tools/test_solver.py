"""
Solver tests: behaviours the provided cases do not cover.
    python3 tools/test_solver.py         
    python3 tools/test_solver.py -v 
Every test taints the parameter x of main, then asks whether the sinks
argument is tainted. reaches_sink makes the same query reaches() does
"""

import sys

from fake_graph import (FakeGraph, report,
                        CALL, IDENTIFIER, LITERAL, METHOD_REF, OPERATOR)
from analysis.solver import Solver, consuming_statement


def reaches_sink(graph, source, sink):
    """Taint at the sink under any one context. Same query as reaches()"""
    solution = Solver(graph, source).run()
    statement = consuming_statement(graph, sink, solution)
    if statement is None:
        return False
    node = graph.node(sink)
    return any(solution.transfer.eval_expr(node, store).tainted
               for store in solution.stores_at(statement))


def taint_per_context(graph, source, node_id):
    """Taint at 1 node, once per calling context it was analysed under."""
    solution = Solver(graph, source).run()
    statement = consuming_statement(graph, node_id, solution)
    node = graph.node(node_id)
    return [solution.transfer.eval_expr(node, store).tainted
            for (_, n), store in solution.in_store.items() if n == statement]


def methods_entered(graph, source):
    """which METHODs the solver actually walked into."""
    solution = Solver(graph, source).run()
    owners = (graph.owning_method(n) for _, n in solution.in_store)
    return {m.id for m in owners if m is not None}


# LOOPS AND MERGES

def test_taint_enters_in_a_loop_body():
    """
    def main(x):
        y = 0
        while ...:
            y = x
        sink(y)
    """
    g = FakeGraph()
    main = g.method("main", ["x"])
    init = g.assign(main.block, "y", g.value(LITERAL, "0"))
    head = g.node(OPERATOR, "greaterThan")
    g.ast(main.block, head)
    body = g.assign(main.block, "y", g.value(IDENTIFIER, "x"))
    sink = g.call("sink", (IDENTIFIER, "y"), parent=main.block)

    g.cfg(main.entry, init, head)
    g.branch(head, body, sink)  # both branches reachable
    g.cfg(body, head)  # back edge
    g.cfg(sink, main.exit)

    graph = g.build()
    assert reaches_sink(graph, main.params[0], g.arg(sink)) is True


def test_loop_that_never_touches_the_source_stays_clean():
    """
    def main(x):
        y = 0
        while ...:
            y = 1
        sink(y)
    """
    g = FakeGraph()
    main = g.method("main", ["x"])
    init = g.assign(main.block, "y", g.value(LITERAL, "0"))
    head = g.node(OPERATOR, "greaterThan")
    g.ast(main.block, head)
    body = g.assign(main.block, "y", g.value(LITERAL, "1"))
    sink = g.call("sink", (IDENTIFIER, "y"), parent=main.block)

    g.cfg(main.entry, init, head)
    g.branch(head, body, sink)
    g.cfg(body, head)
    g.cfg(sink, main.exit)

    graph = g.build()
    assert reaches_sink(graph, main.params[0], g.arg(sink)) is False


def test_union_at_an_if_else_merge():
    """
    def main(x):
        if ...:
            y = x
        else:
            y = 0
        sink(y)

    Tainted on one path only -> union at merge keeps it tainted
    """
    g = FakeGraph()
    main = g.method("main", ["x"])
    cond = g.node(OPERATOR, "greaterThan")
    g.ast(main.block, cond)
    then = g.assign(main.block, "y", g.value(IDENTIFIER, "x"))
    otherwise = g.assign(main.block, "y", g.value(LITERAL, "0"))
    sink = g.call("sink", (IDENTIFIER, "y"), parent=main.block)

    g.cfg(main.entry, cond)
    g.branch(cond, then, otherwise)
    g.cfg(then, sink)
    g.cfg(otherwise, sink)
    g.cfg(sink, main.exit)

    graph = g.build()
    assert reaches_sink(graph, main.params[0], g.arg(sink)) is True


# CALLS

def test_context_keeps_two_call_sites_apart():
    """
    def leak(p):
        sink(p)

    def main(x):
        leak(0)
        leak(x)

    One sink node, two contexts. Context insensitive would taint both.
    """
    g = FakeGraph()
    leak = g.method("leak", ["p"])
    sink = g.call("sink", (IDENTIFIER, "p"), parent=leak.block)
    g.cfg(leak.entry, sink, leak.exit)

    main = g.method("main", ["x"])
    clean = g.call("leak", (LITERAL, "0"), parent=main.block)
    dirty = g.call("leak", (IDENTIFIER, "x"), parent=main.block)
    g.cfg(main.entry, clean, dirty, main.exit)

    graph = g.build()
    seen = taint_per_context(graph, main.params[0], g.arg(sink))
    assert sorted(seen) == [False, True], seen


def test_callee_returning_a_clean_parameter_stays_clean():
    """
    def keep(a, b):
        return a

    def main(x):
        y = keep(0, x)
        sink(y)

    The summary rule alone taints y, since one argument is tainted. Walking
    into keep shows it gives back a, which is clean.
    """
    g = FakeGraph()
    keep = g.method("keep", ["a", "b"])
    give_back = g.ret(keep.block, IDENTIFIER, "a")
    g.cfg(keep.entry, give_back, keep.exit)

    main = g.method("main", ["x"])
    # hoisted, so the call is its own statement and also the assignments value
    call = g.call("keep", (LITERAL, "0"), (IDENTIFIER, "x"), order=2)
    bind = g.assign(main.block, "y", call)
    sink = g.call("sink", (IDENTIFIER, "y"), parent=main.block)
    g.cfg(main.entry, call, bind, sink, main.exit)

    graph = g.build()
    assert reaches_sink(graph, main.params[0], g.arg(sink)) is False


def test_recursion_terminates():
    """
    def rec(n):
        rec(n)
        sink(n)

    def main(x):
        rec(x)

    The call string saturates at depth cap, so the recursive call lands in
    a context already in the map (loops forever if cap is removed)
    """
    g = FakeGraph()
    rec = g.method("rec", ["n"])
    again = g.call("rec", (IDENTIFIER, "n"), parent=rec.block)
    sink = g.call("sink", (IDENTIFIER, "n"), parent=rec.block)
    g.cfg(rec.entry, again, sink, rec.exit)

    main = g.method("main", ["x"])
    start = g.call("rec", (IDENTIFIER, "x"), parent=main.block)
    g.cfg(main.entry, start, main.exit)

    graph = g.build()
    assert reaches_sink(graph, main.params[0], g.arg(sink)) is True


def test_list_tainted_inside_a_callee_is_visible_to_the_caller():
    """
    def fill(lst, v):
        lst[0] = v

    def main(x):
        data = []
        fill(data, x)
        sink(data[0])

    Locals do not cross a call, but locations do -> lists are by reference
    """
    g = FakeGraph()
    fill = g.method("fill", ["lst", "v"])
    write = g.node(OPERATOR, "assignment")
    target = g.node(OPERATOR, "indexAccess", 1)
    g.ast(fill.block, write)
    g.ast(write, target, g.value(IDENTIFIER, "v"))
    g.ast(target, g.node(IDENTIFIER, "lst", 1), g.node(LITERAL, "0", 2))
    g.cfg(fill.entry, write, fill.exit)

    main = g.method("main", ["x"])
    alloc = g.assign(main.block, "data", g.value(OPERATOR, "listLiteral"))
    call = g.call("fill", (IDENTIFIER, "data"), (IDENTIFIER, "x"),
                  parent=main.block)
    # sink(data[0]): the argument is an indexAccess, not a plain name
    sink = g.node(CALL, "sink")
    g.ast(main.block, sink)
    read = g.node(OPERATOR, "indexAccess", 1)
    g.ast(sink, read)
    base = g.node(IDENTIFIER, "data", 1)
    g.ast(read, base, g.node(LITERAL, "0", 2))
    g.cfg(main.entry, alloc, call, sink, main.exit)

    graph = g.build()
    assert reaches_sink(graph, main.params[0], base) is True


def test_list_tainted_before_a_call_is_visible_inside_the_callee():
    """
    def leak(lst):
        sink(lst[0])

    def main(x):
        a = []
        a[0] = x
        leak(a)

    the other direction: the caller taints the list, the callee reads it
    """
    g = FakeGraph()
    leak = g.method("leak", ["lst"])
    sink = g.node(CALL, "sink")
    g.ast(leak.block, sink)
    read = g.node(OPERATOR, "indexAccess", 1)
    g.ast(sink, read)
    base = g.node(IDENTIFIER, "lst", 1)
    g.ast(read, base, g.node(LITERAL, "0", 2))
    g.cfg(leak.entry, sink, leak.exit)

    main = g.method("main", ["x"])
    alloc = g.assign(main.block, "a", g.value(OPERATOR, "listLiteral"))
    write = g.node(OPERATOR, "assignment")
    target = g.node(OPERATOR, "indexAccess", 1)
    g.ast(main.block, write)
    g.ast(write, target, g.value(IDENTIFIER, "x"))
    g.ast(target, g.node(IDENTIFIER, "a", 1), g.node(LITERAL, "0", 2))
    call = g.call("leak", (IDENTIFIER, "a"), parent=main.block)
    g.cfg(main.entry, alloc, write, call, main.exit)

    graph = g.build()
    assert reaches_sink(graph, main.params[0], base) is True


def test_indirect_call_uses_the_latest_binding():
    """
    def clean(z):
        sink(0)

    def dirty(z):
        sink(z)

    def main(x):
        f = clean
        f = dirty
        f(x)

    Flow sensitive, so f() goes to dirty only and clean is never entered.
    """
    g = FakeGraph()
    clean = g.method("clean", ["z"])
    quiet = g.call("sink", (LITERAL, "0"), parent=clean.block)
    g.cfg(clean.entry, quiet, clean.exit)

    dirty = g.method("dirty", ["z"])
    leak = g.call("sink", (IDENTIFIER, "z"), parent=dirty.block)
    g.cfg(dirty.entry, leak, dirty.exit)

    main = g.method("main", ["x"])
    first = g.assign(main.block, "f", g.value(IDENTIFIER, "clean"))
    second = g.assign(main.block, "f", g.value(IDENTIFIER, "dirty"))
    call = g.call("f", (IDENTIFIER, "x"), parent=main.block)
    g.cfg(main.entry, first, second, call, main.exit)

    graph = g.build()
    assert reaches_sink(graph, main.params[0], g.arg(leak)) is True
    assert clean.entry not in methods_entered(graph, main.params[0])


def test_nested_def_shadows_the_outer_one():
    """
    def helper(z):
        sink(0)

    def main(x):
        def helper(z):
            sink(z)
        helper(x)

    A nested def is a METHOD_REF plus an assignment binding the name, so the
    call resolves to the nested definition only.
    """
    g = FakeGraph()
    outer = g.method("helper", ["z"])
    quiet = g.call("sink", (LITERAL, "0"), parent=outer.block)
    g.cfg(outer.entry, quiet, outer.exit)

    main = g.method("main", ["x"])
    inner = g.method("helper", ["z"], inside=main.entry)
    leak = g.call("sink", (IDENTIFIER, "z"), parent=inner.block)
    g.cfg(inner.entry, leak, inner.exit)

    ref = g.value(METHOD_REF, "helper")
    bind = g.assign(main.block, "helper", ref)
    call = g.call("helper", (IDENTIFIER, "x"), parent=main.block)
    g.cfg(main.entry, ref, bind, call, main.exit)

    graph = g.build()
    walked = methods_entered(graph, main.params[0])
    assert inner.entry in walked
    assert outer.entry not in walked
    assert reaches_sink(graph, main.params[0], g.arg(leak)) is True


TESTS = [
    test_taint_enters_in_a_loop_body,
    test_loop_that_never_touches_the_source_stays_clean,
    test_union_at_an_if_else_merge,
    test_context_keeps_two_call_sites_apart,
    test_callee_returning_a_clean_parameter_stays_clean,
    test_recursion_terminates,
    test_list_tainted_inside_a_callee_is_visible_to_the_caller,
    test_list_tainted_before_a_call_is_visible_inside_the_callee,
    test_indirect_call_uses_the_latest_binding,
    test_nested_def_shadows_the_outer_one,
]


if __name__ == "__main__":
    sys.exit(report(TESTS, verbose="-v" in sys.argv[1:]))
