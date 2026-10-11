import os
import sys
from collections import namedtuple

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from analysis.graph import (Graph, BLOCK, CALL, EXIT, IDENTIFIER,  # noqa: E402
                            LITERAL, METHOD, METHOD_REF, OPERATOR,
                            PARAMETER, RETURN)

Func = namedtuple("Func", "entry block exit params")


class FakeGraph:
    """Collects nodes and edges, then hands back a real graph"""

    def __init__(self):
        self.nodes = []
        self.edges = []
        self.last_id = 0
        self.call_args = {}  # call id -> its arg node ids

    # NODES AND EDGES
    def node(self, kind, val="", order=0):
        """One node. Gives back its id, so tests can wire it up"""
        self.last_id += 1
        self.nodes.append({"id": self.last_id, "kind": kind,
                          "value": val, "astOrder": order})
        return self.last_id

    def ast(self, parent, *children):
        for c in children:
            self.edges.append(
                {"source": parent, "destination": c, "kind": "AST"})

    def cfg(self, *chain):
        """CFG edges along a straight line: a -> b -> c."""
        for a, b in zip(chain, chain[1:]):
            self.edges.append({"source": a, "destination": b, "kind": "CFG"})

    def branch(self, head, *targets):
        """CFG edges from one node to several. for an if, or a loop head."""
        for t in targets:
            self.edges.append({"source": head, "destination": t,
                               "kind": "CFG"})

    # SHAPES

    def method(self, name, params=(), inside=None):
        """
        A function: its METHOD entry, BLOCK, EXIT and PARAMETERs.
        inside is the enclosing METHOD id, for a nested def.
        """
        m = self.node(METHOD, name, 2)
        block = self.node(BLOCK, "[ ... ]", 1)
        ex = self.node(EXIT, "EXIT", 3)
        pids = [self.node(PARAMETER, p, i + 1) for i, p in enumerate(params)]
        self.ast(m, block, ex, *pids)
        if inside is not None:
            self.ast(inside, m)
        return Func(m, block, ex, pids)

    def value(self, kind, val=""):
        """A node maent to sit on the right of an assignment (astOrder 2)"""
        return self.node(kind, val, 2)

    def assign(self, block, name, rhs):
        """name = rhs. rhs must be at astOrder 2"""
        op = self.node(OPERATOR, "assignment")
        target = self.node(IDENTIFIER, name, 1)
        self.ast(block, op)
        self.ast(op, target, rhs)
        return op

    def call(self, name, *args, parent=None, order=0):
        """name(args), each arg a (kind, value) pair numbered L to R.
        parent is the block this call sits in, order is its astOrder."""
        c = self.node(CALL, name, order)
        if parent is not None:
            self.ast(parent, c)

        ids = []
        for i, (kind, val) in enumerate(args):
            a = self.node(kind, val, i + 1)
            self.ast(c, a)
            ids.append(a)
        self.call_args[c] = ids
        return c

    def arg(self, call_id, i=0):
        """The i-th argument node of a call. Tests use it to name the sink"""
        return self.call_args[call_id][i]

    def ret(self, block, kind, val=""):
        """return <something>."""
        r = self.node(RETURN, "return")
        self.ast(block, r)
        self.ast(r, self.node(kind, val, 1))
        return r

# RESULTS

    def build(self):
        return Graph.from_json({"nodes": self.nodes, "edges": self.edges})


def report(tests, verbose=False):
    """
    Run each test, keep going after a failure, give back an exit code.
    verbose prints each tests docstring, which is the program it models.
    """
    failed = 0
    for t in tests:
        if verbose:
            print("--- %s%s" % (t.__name__, t.__doc__ or ""))
        try:
            t()
            print("ok    %s" % t.__name__)
        except Exception as e:
            # a crash, not a failed check
            failed += 1
            print("ERROR %s: %s: %s" % (t.__name__, type(e).__name__, e))

    print()
    print("%d of %d passing" % (len(tests) - failed, len(tests)))
    return 1 if failed else 0
