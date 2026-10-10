from collections import deque
from typing import Dict, Optional, Tuple

from .graph import Graph, Node, PARAMETER, TestCase
from .store import Store
from .transfer import Transfer


class Solution:
    """IN and OUT stores at every node reached from the entry"""

    def __init__(self, transfer: Transfer, method: Node, in_store: Dict[int, Store], out_store: Dict[int, Store]):
        self.transfer = transfer
        self.method = method
        self.in_store = in_store
        self.out_store = out_store

    def analysed(self, node_id: int) -> bool:
        """Check if the node part of the solved function"""
        return node_id in self.in_store

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


def solve(graph: Graph, source_node: int) -> Solution:
    transfer = Transfer(graph)
    method, entry = boundary_store(graph, source_node)

    nodes = graph.cfg_nodes(method.id)
    in_store: Dict[int, Store] = {n: Store() for n in nodes}
    out_store: Dict[int, Store] = {n: Store() for n in nodes}

    in_store[method.id] = entry.copy()

    # FIFO: breadth-first order reaches a merge point after both its branches
    # more often than LIFO does, so loops settle in fewer passes. Either order
    # reaches the same fixed point
    work = deque([method.id])
    while work:
        node = work.popleft()
        out = transfer.apply(node, in_store[node])
        out_store[node] = out

        for succ in graph.successors(node):
            merged = in_store[succ].join(out)
            if merged != in_store[succ]:
                in_store[succ] = merged
                if succ not in work:
                    work.append(succ)

    return Solution(transfer, method, in_store, out_store)


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
        # the sink sits in a function this analysis never entered, which is
        # every interprocedural case until part 2 adds call and return edges
        return False

    sink = graph.node(case.sink_node)
    return solution.transfer.eval_expr(sink, solution.in_store[statement]).tainted
