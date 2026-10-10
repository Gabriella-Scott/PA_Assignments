"""
Call graph: which method definitions can one call site invoke?
"""

from typing import Dict, FrozenSet, List, Optional, Set

from .graph import CALL, Graph, Node
from .store import Store


class CallSite:
    """One CALL node: the name + its argument expressions."""

    def __init__(self, graph: Graph, call_id: int):
        node = graph.node(call_id)
        self.id = call_id
        self.name = node.val
        # operands of a CALL are its arguments, L -> R
        self.args: List[Node] = graph.operands(call_id)

    def __repr__(self) -> str:
        return "CallSite(%s/%d, id=%d)" % (self.name, len(self.args), self.id)


class CallGraph:
    """Definition lookup and call site inventory for one graph."""

    def __init__(self, graph: Graph):
        self.g = graph

        # enclosing method id (None at module level) -> name -> definition sites.
        # A set, not one id: a name redefined twice in the same scope is kept as
        # both, which over-approximates rather than guessing which one runs
        self.scopes: Dict[Optional[int], Dict[str, Set[int]]] = {}
        for m in graph.methods():
            enclosing = graph.enclosing_method(m.id)
            key = enclosing.id if enclosing else None
            self.scopes.setdefault(key, {}).setdefault(m.val, set()).add(m.id)

        self.sites: Dict[int, CallSite] = {
            n.id: CallSite(graph, n.id)
            for n in graph.nodes.values() if n.kind == CALL
        }

    # SCOPE

    def scope_chain(self, node_id: int) -> List[Optional[int]]:
        """
        Scopes visible from a node, innermost first, ending with None (module).
        """
        chain: List[Optional[int]] = []
        method = self.g.owning_method(node_id)
        while method is not None:
            chain.append(method.id)
            method = self.g.enclosing_method(method.id)
        chain.append(None)
        return chain

    def direct_targets(self, call_id: int) -> FrozenSet[int]:
        """Definitions a bare name resolves to, taking the innermost scope that has it."""
        name = self.sites[call_id].name
        for scope in self.scope_chain(call_id):
            found = self.scopes.get(scope, {}).get(name)
            if found:
                return frozenset(found)
        return frozenset()

    # RESOLUTION

    def targets(self, call_id: int, store: Store) -> FrozenSet[int]:
        """
        Target methods of this call site, given the store at that point.
        """
        name = self.sites[call_id].name

        if name in store.points_to or name in store.tainted:
            return frozenset(store.func_targets(name))

        return self.direct_targets(call_id)

    # QUERIES

    def sites_in(self, method_id: int) -> List[CallSite]:
        """Call sites on the CFG of one method, in a stable order."""
        owned = [
            site for node_id, site in self.sites.items()
            if self.g.owning_method(node_id) is not None
            and self.g.owning_method(node_id).id == method_id
        ]
        return sorted(owned, key=lambda s: s.id)

    def bind(self, call_id: int, method_id: int) -> List[tuple]:
        """
        Pair arguments with the callee's parameter names, positionally
        """
        args = self.sites[call_id].args
        params = self.g.parameters(method_id)
        return [(params[i].val, args[i]) for i in range(min(len(params), len(args)))]
