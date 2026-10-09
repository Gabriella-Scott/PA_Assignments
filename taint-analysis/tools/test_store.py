#!/usr/bin/env python3
"""
Check the abstract store behaves as the spec requires.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from analysis.store import Store, Loc, LIST, FUNC  # noqa: E402

# node id of 'def foo' in testcases/test_inter5, so the FUNC checks use a real site
FOO_SITE = 111669149697

passed = 0
failed = 0


def check(label, condition, store=None):
    global passed, failed
    if condition:
        passed += 1
        print("  pass  %s" % label)
    else:
        failed += 1
        print("  FAIL  %s" % label)
    if store is not None:
        print("        %r" % store)


def test_strong_update():
    print("strong update on a variable")
    s = Store()
    s.assign("x", True, set())  # x is the source parameter
    check("source parameter is tainted", s.is_tainted("x"))
    s.assign("x", False, set())  # x = "OVERWRITE"
    check("literal assignment clears taint", not s.is_tainted("x"), s)


def test_aliasing():
    print("aliasing through a shared location")
    lst = Loc(LIST, 42)
    s = Store()
    s.assign("lst", False, {lst})
    s.assign("alias", False, s.locs_of("lst"))  # alias = lst
    s.write_through(s.locs_of("lst"), True)  # lst[0] = tainted
    check("write through lst is visible via alias", s.is_tainted("alias"), s)


def test_weak_update_never_kills():
    print("weak update is add-only")
    lst = Loc(LIST, 42)
    s = Store()
    s.assign("lst", False, {lst})
    s.write_through(s.locs_of("lst"), True)
    s.write_through(s.locs_of("lst"), False)  # lst[1] = 0
    check("clean write leaves earlier taint alone", s.is_tainted("lst"), s)


def test_function_references():
    print("function references")
    foo = Loc(FUNC, FOO_SITE)
    s = Store()
    s.assign("f", False, {foo})  # f = foo
    s.assign("g", False, s.locs_of("f"))  # g = f
    check("g resolves to foo", s.func_targets("g") == frozenset({FOO_SITE}), s)
    check("a scalar name has no call targets",
          s.func_targets("nothing") == frozenset())


def test_rebinding_shadows():
    print("rebinding is flow sensitive")
    s = Store()
    s.assign("h", False, {Loc(FUNC, 111)})
    s.assign("h", False, {Loc(FUNC, 222)})  # h = other
    check("only the latest definition survives",
          s.func_targets("h") == frozenset({222}), s)


def test_join():
    print("join at a merge point")
    lst = Loc(LIST, 42)
    left = Store()
    left.assign("y", True, set())  # tainted on the then branch
    right = Store()
    right.assign("z", False, {lst})  # a list on the else branch

    before = left.copy()
    merged = left.join(right)

    check("taint from one side survives", merged.is_tainted("y"))
    check("points-to from the other side survives",
          merged.locs_of("z") == {lst}, merged)
    check("join does not mutate its operand", left == before)


def test_copy_is_deep():
    print("copy is deep")
    lst = Loc(LIST, 42)
    original = Store()
    original.assign("a", False, {lst})
    duplicate = original.copy()
    duplicate.points_to["a"].add(Loc(LIST, 99))
    check("editing the copy leaves the original alone",
          original.locs_of("a") == {lst}, original)


def main():
    for test in (test_strong_update, test_aliasing, test_weak_update_never_kills,
                 test_function_references, test_rebinding_shadows, test_join,
                 test_copy_is_deep):
        test()
        print()

    print("%d passed, %d failed" % (passed, failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
