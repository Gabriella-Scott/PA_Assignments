"""
Transfer functions (how one CFG node turns its IN store into it OUT store)
2 layers:
- read AST expression, gives back an abstract val
- run one CFG statement, give back a new store

CFG -> only containes statement roots, so apply dispatches on those and eval_expr walks down the AST under them
"""

from typing import Dict, NamedTuple, Set

from .graph import (Graph, Node, CALL, IDENTIFIER, LITERAL, OPERATOR,
                    PARAMETER, RETURN)
from .store import FUNC, LIST, Loc, Store


ASSIGN = "assignment"

AUG_ASSIGN = {"assignmentPlus", "assignmentMinus", "assignmentMultiplication",
              "assignmentDivision", "assignmentModulo"}

# comparisons and boolean connectives return a bool, not the operand data
BOOL_OPS = {"greaterThan", "greaterEqualsThan", "lessThan", "lessEqualsThan",
            "equals", "notEquals", "logicalAnd", "logicalOr", "logicalNot",
            "is", "isNot", "in", "notIn"}

# a list literal allocates a fresh object
ALLOC_OPS = {"listLiteral", "arrayInitializer", "tupleLiteral"}

# lst[i], both as a read and as an assignment target
INDEX_OPS = {"indexAccess", "indirectIndexAccess"}


class Value(NamedTuple):
    """
    Abstract val of an expression.
    tainted -> may hold data derived from source.
    locs -> objects it may point to
    """
    tainted: bool
    locs: Set[Loc]


def call_temp(call_id: int) -> str:
    """Store key holding results of one call site"""
    return "@call%d" % call_id


def return_slot(method_id: int) -> str:
    """Store key holding what one function gives back."""
    return "@ret%d" % method_id


class Transfer:
    """Transfer functions for one graph"""

    def __init__(self, graph: Graph):
        self.g = graph
        # name -> definition sites, for reading a bare function name
        self.funcs_by_name: Dict[str, Set[int]] = {}
        for m in graph.methods():
            self.funcs_by_name.setdefault(m.val, set()).add(m.id)

# STATEMENTS

    def apply(self, node_id: int, store_in: Store) -> Store:
        """
        OUT = f_n(IN) for the CFG node n. Works on a copy, so the IN store of a
        node is never damged by its own transfer
        """
        out = store_in.copy()
        node = self.g.node(node_id)

        if node.kind == OPERATOR:
            self._operator(node, out)
        elif node.kind == CALL:
            self._call(node, out)
        elif node.kind == RETURN:
            self._return(node, out)
        # METHOD (the entry node) and EXIT are not statements: identity

        return out

    def _operator(self, node: Node, store: Store) -> None:
        ops = self.g.operands(node.id)

        if node.val == ASSIGN and len(ops) == 2:
            self._assign(ops[0], self.eval_expr(ops[1], store), store)

        elif node.val in AUG_ASSIGN and len(ops) == 2:
            # x += y is x = x + y, so the old value survives into the new one
            old = self.eval_expr(ops[0], store)
            rhs = self.eval_expr(ops[1], store)
            self._assign(ops[0], Value(
                old.tainted or rhs.tainted, old.locs | rhs.locs), store)

        # every other operator is a pure expression (a condition, an arithmetic term, pass)

    def _assign(self, target: Node, value: Value, store: Store) -> None:
        """The two shapes of assignment target that the scope allows."""
        if target.kind == IDENTIFIER:
            # STRONG update: a name is exactly one slot, so the old value is killed.
            store.assign(target.val, value.tainted, value.locs)

        elif target.kind == OPERATOR and target.val in INDEX_OPS:
            # WEAK update: one loc summarises every list made at that site and
            # not tracking indices, so taint is added and never removed
            base = self.g.operands(target.id)[0]
            store.write_through(self.eval_expr(
                base, store).locs, value.tainted)

    def _call(self, node: Node, store: Store) -> None:
        """
        summary rule for a call, used by the intraprocedural analysis
        """
        args = [self.eval_expr(a, store) for a in self.g.operands(node.id)]
        locs: Set[Loc] = set()
        for a in args:
            locs |= a.locs
        store.assign(call_temp(node.id), any(a.tainted for a in args), locs)

    def _return(self, node: Node, store: Store) -> None:
        """Park the returned value under the methods return slot."""
        method = self.g.owning_method(node.id)
        if method is None:
            return
        ops = self.g.operands(node.id)
        value = self.eval_expr(ops[0], store) if ops else Value(False, set())
        store.assign(return_slot(method.id), value.tainted, value.locs)

 # EXPRESSIONS

    def eval_expr(self, node: Node, store: Store) -> Value:
        """Abstract value of an expression subtree, read-only apart from allocations."""
        if node.kind == LITERAL:
            return Value(False, set())

        if node.kind in (IDENTIFIER, PARAMETER):
            return self._read_name(node.val, store)

        if node.kind == CALL:
            # hoisted, so the result is already sitting in its temp
            return self._read_name(call_temp(node.id), store)

        if node.kind == OPERATOR:
            return self._eval_operator(node, store)

        return Value(False, set())

    def _read_name(self, name: str, store: Store) -> Value:
        """A name is a variable if the store knows it, otherwise a bare function name."""
        if name in store.points_to or name in store.tainted:
            return Value(store.is_tainted(name), set(store.locs_of(name)))

        # f = foo: foo is not a variable, it refers to the definition itself
        return Value(False, {Loc(FUNC, site) for site in self.funcs_by_name.get(name, set())})

    def _eval_operator(self, node: Node, store: Store) -> Value:
        ops = self.g.operands(node.id)

        if node.val in BOOL_OPS:
            return Value(False, set())

        if node.val in ALLOC_OPS:
            # fresh list, named after the node that allocates it. One loc per site,
            # so a list built in a loop is summarised by a single location
            loc = Loc(LIST, node.id)
            if any(self.eval_expr(e, store).tainted for e in ops):
                store.taint_loc(loc)
            return Value(False, {loc})

        if node.val in INDEX_OPS and ops:
            # y[i]: only the contents of y can taint the result, never the index i.
            # field-insensitive, so the element keeps the location of the whole list
            base = self.eval_expr(ops[0], store)
            tainted = base.tainted or any(
                loc in store.tainted for loc in base.locs)
            return Value(tainted, set(base.locs))

        # default: any operand may define the result
        tainted = False
        locs: Set[Loc] = set()
        for e in ops:
            v = self.eval_expr(e, store)
            tainted = tainted or v.tainted
            locs |= v.locs
        return Value(tainted, locs)
