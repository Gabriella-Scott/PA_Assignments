"""
Worklist solver over the interprocedural cfg
"""

from collections import deque
from typing import Dict, List, Optional, Set, Tuple

from .callgraph import CallGraph
from .graph import CALL, EXIT, Graph, Node, PARAMETER, TestCase
from .store import Loc, Store
from .transfer import Transfer, call_temp, return_slot

CONTEXT_DEPTH = 2  # call string depth

Context = Tuple[int, ...]  # the call sites that led here, oldest first
Point = Tuple[Context, int]  # one (context, cfg node) pair


def extend(ctx: Context, call_id: int, depth: int = CONTEXT_DEPTH) -> Context:
    """Push a call site onto the call string, dropping anything past the cap."""
    if depth <= 0:
        return ()
    return (ctx + (call_id,))[-depth:]


class Solution:
    """IN and OUT stores at every node reached from the entry"""

    def __init__(self, transfer: Transfer, callgraph: CallGraph, method: Node,
                 in_store: Dict[Point, Store], out_store: Dict[Point, Store]):
        self.transfer = transfer
        self.callgraph = callgraph
        self.method = method
        self.in_store = in_store
        self.out_store = out_store
        # node ids reached in at least one context
        self.statements: Set[int] = {node for _, node in in_store}

    def analysed(self, node_id: int) -> bool:
        """Was this node solved, in any context?"""
        return node_id in self.statements

    def stores_at(self, node_id: int) -> List[Store]:
        """IN store of this node, once per context it was analysed under."""
        return [store for (_, node), store in self.in_store.items()
                if node == node_id]


def boundary_store(graph: Graph, source_node: int) -> Tuple[Node, Store]:
    """IN store at the entry of the function holding the source."""
    source = graph.node(source_node)
    method = graph.owning_method(source_node)

    store = Store()
    for param in graph.parameters(method.id):
        store.assign(param.val, param.id == source_node, set())

    if source.kind != PARAMETER:
        # the generator always makes source a parameter -> this only fires
        # on a hand-built case. Tainting the name at entry is conservative:
        # it may report taint slightly before the real source
        store.assign(source.val, True, set())

    return method, store


class Solver:
    """One run of the analysis, from the function that owns the source."""

    def __init__(self, graph: Graph, source_node: int,
                 depth: int = CONTEXT_DEPTH):
        self.g = graph
        self.cg = CallGraph(graph)
        self.transfer = Transfer(graph, self.cg)
        self.depth = depth

        self.in_store: Dict[Point, Store] = {}
        self.out_store: Dict[Point, Store] = {}
        # (context, callee EXIT) -> call sites waiting on that summary
        self.callers: Dict[Point, Set[Point]] = {}
        self.work: deque = deque()

        self.method, entry = boundary_store(graph, source_node)
        self.push((), self.method.id, entry.copy())

    # WORKLIST

    def push(self, ctx: Context, node: int, store: Store) -> None:
        """Join a store into IN(ctx,node) and queue the node if that grew it."""
        key = (ctx, node)
        old = self.in_store.get(key)
        merged = store.copy() if old is None else old.join(store)

        if old is None or merged != old:
            self.in_store[key] = merged
            if key not in self.work:
                self.work.append(key)

    def run(self) -> Solution:
        # FIFO: breadth first reaches a merge point after both its branches more
        # often than LIFO, so loops settle in fewer passes. Same fixed point
        while self.work:
            ctx, node = self.work.popleft()
            store_in = self.in_store[(ctx, node)]

            if self.g.kind_of(node) == CALL:
                out = self.call(ctx, node, store_in)
            else:
                out = self.transfer.apply(node, store_in)

            grew = self.out_store.get((ctx, node)) != out
            self.out_store[(ctx, node)] = out

            if grew and self.g.kind_of(node) == EXIT:
                # this functions summary changed, so every call site that
                # already read it has to read it again
                for caller in self.callers.get((ctx, node), ()):
                    if caller not in self.work:
                        self.work.append(caller)

            for succ in self.g.successors(node):
                self.push(ctx, succ, out)

        return Solution(self.transfer, self.cg, self.method,
                        self.in_store, self.out_store)

    # CALLS

    def call(self, ctx: Context, call_id: int, store_in: Store) -> Store:
        """
        OUT of a CALL node: the caller's own state, plus whatever the callee
        did to the heap, plus the returned value parked in the call's temp
        """
        targets = self.cg.targets(call_id, store_in)
        if not targets:
            # sink(), print(), anything with no definition in this file:
            # pt 1s summary rule is all we have
            return self.transfer.apply(call_id, store_in)

        out = store_in.copy()  # locals carry on unchanged across the call
        ret_tainted = False
        ret_locs: Set[Loc] = set()

        for method_id in sorted(targets):
            ctx2 = extend(ctx, call_id, self.depth)
            self.push(ctx2, method_id,
                      self.entry_store(call_id, method_id, store_in))

            exit_node = self.g.exit_of(method_id)
            if exit_node is None:
                continue
            self.callers.setdefault(
                (ctx2, exit_node.id), set()).add((ctx, call_id))

            summary = self.out_store.get((ctx2, exit_node.id))
            if summary is None:
                continue    # callee not solved yet, its EXIT will requeue us

            # the heap is shared, so a list the callee tainted is tainted here
            out.tainted.update(
                k for k in summary.tainted if isinstance(k, Loc))

            # union over targets, so an indirect call to two functions takes
            # taint if either of them returns it
            slot = return_slot(method_id)
            ret_tainted = ret_tainted or summary.is_tainted(slot)
            ret_locs |= summary.locs_of(slot)

        out.assign(call_temp(call_id), ret_tainted, ret_locs)
        return out

    def entry_store(self, call_id: int, method_id: int, store: Store) -> Store:
        """IN store at a callee's entry: formals bound to actuals."""
        entry = Store()

        for name, arg in self.cg.bind(call_id, method_id):
            value = self.transfer.eval_expr(arg, store)
            entry.assign(name, value.tainted, value.locs)

        # a formal with no matching argument is still a known, clean name
        for param in self.g.parameters(method_id):
            if param.val not in entry.points_to:
                entry.assign(param.val, False, set())

        return entry


def solve(graph: Graph, source_node: int) -> Solution:
    return Solver(graph, source_node).run()


def consuming_statement(graph: Graph, node_id: int,
                        solution: Solution) -> Optional[int]:
    """The CFG statement whose IN store holds the value of this expression"""
    parent = graph.parent(node_id)
    current = parent.id if parent else None

    while current is not None:
        if solution.analysed(current):
            return current
        above = graph.parent(current)
        current = above.id if above else None

    return None


def reaches(graph: Graph, case: TestCase) -> bool:
    """Does taint from the source reach the sink?"""
    solution = solve(graph, case.source_node)

    statement = consuming_statement(graph, case.sink_node, solution)
    if statement is None:
        # nothing that could run ever reaches sinks statement
        return False

    sink = graph.node(case.sink_node)
    # tainted under any one calling context is enough: this is a may analysis
    return any(solution.transfer.eval_expr(sink, store).tainted
               for store in solution.stores_at(statement))
