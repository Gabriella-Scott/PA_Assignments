"""
Call graph tests: name resolution and argument binding.
    python3 tools/test_callgraph.py         
    python3 tools/test_callgraph.py -v 

These are unit tests on CallGraph alone, no solver involved. Store is
built by hand, so each test says exactly what is known at call site.
"""

import sys

from fake_graph import FakeGraph, report, IDENTIFIER, LITERAL, METHOD_REF
from analysis.callgraph import CallGraph
from analysis.store import FUNC, Loc, Store


def test_direct_call_resolves_to_a_module_level_def():
    """
    def foo(z):
        pass

    def main(x):
        foo(x)
    """
    g = FakeGraph()
    foo = g.method("foo", ["z"])
    main = g.method("main", ["x"])
    call = g.call("foo", (IDENTIFIER, "x"), parent=main.block)

    cg = CallGraph(g.build())
    assert cg.direct_targets(call) == frozenset({foo.entry})


def test_undefined_callee_has_no_targets():
    """
    def main(x):
        sink(x)

    sink has no def in the file, so the target set is empty. That is normal,
    not an error: it is what makes the solver use the summary rule instead.
    """
    g = FakeGraph()
    main = g.method("main", ["x"])
    call = g.call("sink", (IDENTIFIER, "x"), parent=main.block)

    cg = CallGraph(g.build())
    assert cg.direct_targets(call) == frozenset()


def test_nested_def_shadows_an_outer_def_of_the_same_name():
    """
    def helper(z):
        pass

    def main(x):
        def helper(z):
            pass
        helper(x)

    Innermost scope wins, so only the nested definition is a target.
    """
    g = FakeGraph()
    outer = g.method("helper", ["z"])
    main = g.method("main", ["x"])
    inner = g.method("helper", ["z"], inside=main.entry)
    call = g.call("helper", (IDENTIFIER, "x"), parent=main.block)

    cg = CallGraph(g.build())
    assert cg.direct_targets(call) == frozenset({inner.entry})
    assert outer.entry not in cg.direct_targets(call)


def test_scope_chain_runs_innermost_first_and_ends_at_module_level():
    """
    def main(x):
        def inner(z):
            pass

    None stands for module level, so it is always the last scope searched.
    """
    g = FakeGraph()
    main = g.method("main", ["x"])
    inner = g.method("inner", ["z"], inside=main.entry)

    cg = CallGraph(g.build())
    assert cg.scope_chain(inner.block) == [inner.entry, main.entry, None]


def test_lookup_resolves_a_name_at_a_node_that_is_not_a_call():
    """
    def helper(z):
        pass

    def main(x):
        f = helper

    helper is being read, not called. transfer._func_locs needs this, and it
    has to agree with what a call site would resolve to.
    """
    g = FakeGraph()
    helper = g.method("helper", ["z"])
    main = g.method("main", ["x"])
    name = g.value(IDENTIFIER, "helper")
    g.assign(main.block, "f", name)

    cg = CallGraph(g.build())
    assert cg.lookup("helper", name) == frozenset({helper.entry})


# RESOLVING THROUGH THE STORE

def test_indirect_call_resolves_from_the_store():
    """
    def foo(z):
        pass

    def main(x):
        f = foo
        f(x)

    f is a variable, so its targets come from the store, not from the name.
    """
    g = FakeGraph()
    foo = g.method("foo", ["z"])
    main = g.method("main", ["x"])
    call = g.call("f", (IDENTIFIER, "x"), parent=main.block)

    cg = CallGraph(g.build())
    store = Store()
    store.assign("f", False, {Loc(FUNC, foo.entry)})
    assert cg.targets(call, store) == frozenset({foo.entry})


def test_store_wins_over_the_name_at_a_call_site():
    """
    def f(z):
        pass

    def main(x):
        f = <some other function>
        f(x)

    A variable named f shadows the def named f. The store is checked first,
    so the def is not a target.
    """
    g = FakeGraph()
    defined = g.method("f", ["z"])
    other = g.method("other", ["z"])
    main = g.method("main", ["x"])
    call = g.call("f", (IDENTIFIER, "x"), parent=main.block)

    cg = CallGraph(g.build())
    store = Store()
    store.assign("f", False, {Loc(FUNC, other.entry)})
    assert cg.targets(call, store) == frozenset({other.entry})
    assert defined.entry not in cg.targets(call, store)


def test_name_not_in_the_store_falls_back_to_the_scope():
    """
    def foo(z):
        pass

    def main(x):
        foo(x)

    Nothing has bound foo as a variable -> resolution falls back to the
    scope chain.
    """
    g = FakeGraph()
    foo = g.method("foo", ["z"])
    main = g.method("main", ["x"])
    call = g.call("foo", (IDENTIFIER, "x"), parent=main.block)

    cg = CallGraph(g.build())
    assert cg.targets(call, Store()) == frozenset({foo.entry})


def test_a_variable_holding_no_function_has_no_targets():
    """
    def main(x):
        f = 0
        f(x)

    f is known but points at no definition. Empty, not a fall back to a def
    of the same name, because the store is what f means here.
    """
    g = FakeGraph()
    main = g.method("main", ["x"])
    call = g.call("f", (IDENTIFIER, "x"), parent=main.block)

    cg = CallGraph(g.build())
    store = Store()
    store.assign("f", False, set())
    assert cg.targets(call, store) == frozenset()


# ARGUMENTS AND INVENTORY

def test_bind_pairs_arguments_with_parameters_by_position():
    """
    def two(a, b):
        pass

    def main(x):
        two(0, x)
    """
    g = FakeGraph()
    two = g.method("two", ["a", "b"])
    main = g.method("main", ["x"])
    call = g.call("two", (LITERAL, "0"), (IDENTIFIER, "x"), parent=main.block)

    cg = CallGraph(g.build())
    pairs = cg.bind(call, two.entry)
    assert [name for name, _ in pairs] == ["a", "b"]
    assert [node.id for _, node in pairs] == [g.arg(call, 0), g.arg(call, 1)]


def test_bind_stops_at_the_shorter_of_the_two():
    """
    def one(a):
        pass

    def main(x):
        one(x, 0)

    Takes min(params, args), so a mismatched call binds what it can instead
    of raising
    """
    g = FakeGraph()
    one = g.method("one", ["a"])
    main = g.method("main", ["x"])
    call = g.call("one", (IDENTIFIER, "x"), (LITERAL, "0"), parent=main.block)

    cg = CallGraph(g.build())
    pairs = cg.bind(call, one.entry)
    assert len(pairs) == 1
    assert pairs[0][0] == "a"


def test_bind_handles_too_few_arguments():
    """
    def two(a, b):
        pass

    def main(x):
        two(x)         # one argument short

    Binds a and leaves b alone. Without the min this raises IndexError.
    """
    g = FakeGraph()
    two = g.method("two", ["a", "b"])
    main = g.method("main", ["x"])
    call = g.call("two", (IDENTIFIER, "x"), parent=main.block)

    cg = CallGraph(g.build())
    pairs = cg.bind(call, two.entry)
    assert len(pairs) == 1
    assert pairs[0][0] == "a"


def test_sites_in_lists_only_the_calls_of_one_method():
    """
    def foo(z):
        sink(z)

    def main(x):
        foo(x)
        foo(0)
    """
    g = FakeGraph()
    foo = g.method("foo", ["z"])
    inner_sink = g.call("sink", (IDENTIFIER, "z"), parent=foo.block)

    main = g.method("main", ["x"])
    first = g.call("foo", (IDENTIFIER, "x"), parent=main.block)
    second = g.call("foo", (LITERAL, "0"), parent=main.block)

    cg = CallGraph(g.build())
    assert [s.id for s in cg.sites_in(main.entry)] == [first, second]
    assert [s.id for s in cg.sites_in(foo.entry)] == [inner_sink]


TESTS = [
    test_direct_call_resolves_to_a_module_level_def,
    test_undefined_callee_has_no_targets,
    test_nested_def_shadows_an_outer_def_of_the_same_name,
    test_scope_chain_runs_innermost_first_and_ends_at_module_level,
    test_lookup_resolves_a_name_at_a_node_that_is_not_a_call,
    test_indirect_call_resolves_from_the_store,
    test_store_wins_over_the_name_at_a_call_site,
    test_name_not_in_the_store_falls_back_to_the_scope,
    test_a_variable_holding_no_function_has_no_targets,
    test_bind_pairs_arguments_with_parameters_by_position,
    test_bind_stops_at_the_shorter_of_the_two,
    test_bind_handles_too_few_arguments,
    test_sites_in_lists_only_the_calls_of_one_method,
]


if __name__ == "__main__":
    sys.exit(report(TESTS, verbose="-v" in sys.argv[1:]))