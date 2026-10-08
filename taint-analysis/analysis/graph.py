"""
Loader for test case directory: graph.json & test_cases.json

AST edges: parent -> child, structure of program
CFG edges: statement -> successor, execution order inside one function
"""


import json
import os
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Set


# Node kinds produced by graph builder
METHOD = "METHOD"
PARAMETER = "PARAMETER"
BLOCK = "BLOCK"
OPERATOR = "OPERATOR"
CALL = "CALL"
IDENTIFIER = "IDENTIFIER"
LITERAL = "LITERAL"
RETURN = "RETURN"
EXIT = "EXIT"
CONTROL_STRUCTURE = "CONTROL_STRUCTURE"

# Nodes that actually take part in control flow
CFG_KINDS = {METHOD, OPERATOR, CALL, RETURN, EXIT}


@dataclass(frozen=True)
class Node:
    """One node in the graph"""
    id: int
    kind: str
    val: str
    ast_order: int

    def __repr__(self) -> str:
        return "%s(%s, id=%d)" % (self.kind, self.val, self.id)


@dataclass
class TestCase:
    """Two fields of test_case.json that are allowed to read"""
    source_node: int
    sink_node: int
    directory: str


@dataclass
class Graph:
    """AST amd CFG over a shared node set, with lookups in both directions"""
    nodes: Dict[int, Node] = field(default_factory=dict)
    _ast_children: Dict[int, List[int]] = field(default_factory=dict)
    _ast_parent: Dict[int, int] = field(default_factory=dict)
    _cfg_succ: Dict[int, List[int]] = field(default_factory=dict)
    _cfg_pred: Dict[int, List[int]] = field(default_factory=dict)

    # CONSTRUCTION

    @staticmethod
    def from_json(data: dict) -> "Graph":
        g = Graph()
        for raw in data["nodes"]:
            n = Node(
                id=int(raw["id"]),
                kind=raw["kind"],
                val=raw.get("value", ""),
                # 0 keeps sorting total (astOrder is missing on a few node kinds)
                ast_order=int(raw.get("astOrder", 0)),
            )
            g.nodes[n.id] = n
            g._ast_children[n.id] = []
            g._cfg_succ[n.id] = []
            g._cfg_pred[n.id] = []

        for raw in data["edges"]:
            src, dst = int(raw["source"]), int(raw["destination"])
            if raw["kind"] == "AST":
                g._ast_children[src].append(dst)
                # the builder gives every node at most one AST parent
                g._ast_parent[dst] = src
            elif raw["kind"] == "CFG":
                g._cfg_succ[src].append(dst)
                g._cfg_pred[dst].append(src)
        return g

    # NODE ACCESS
    def node(self, node_id: int) -> Node:
        return self.nodes[node_id]

    def kind_of(self, node_id: int) -> str:
        return self.nodes[node_id].kind

    # AST
    def children(self, node_id: int, kind: Optional[str] = None) -> List[Node]:
        """
        AST children in source order. Note: astOrder is only unique inside one kind group under a parent.
        A METHOD has its BLOCK at order 1 and its 1st PARAM also at order 1.
        So filtering by kind first, then sort, when a specific kind is wanted.
        """
        kids = [self.nodes[c] for c in self._ast_children.get(node_id, [])]
        if kind is not None:
            kids = [n for n in kids if n.kind == kind]
        return sorted(kids, key=lambda n: n.ast_order)

    def operands(self, node_id: int) -> List[Node]:
        """
        children of an expression node, L -> R.
        Safe without a kind filter (under an OPERATOR, CALL or RETURN the
        orders number the operand posistions ans are unique across all children)
        """
        return self.children(node_id)

    def parent(self, node_id: int) -> Optional[Node]:
        pid = self._ast_parent.get(node_id)
        return self.nodes[pid] if pid is not None else None

    def owning_method(self, node_id: int) -> Optional[Node]:
        """ walk up AST to the METHOD this node belongs to (itself if it is one)."""
        current: Optional[int] = node_id
        while current is not None:
            if self.nodes[current].kind == "METHOD":
                return self.nodes[current]
            current = self._ast_parent.get(current)
        return None

    def enclosing_method(self, method_id: int) -> Optional[Node]:
        """The METHOD a nested definition sits inside, or None at module level."""
        parent_id = self._ast_parent.get(method_id)
        return self.owning_method(parent_id) if parent_id is not None else None

    # CFG
    def successors(self, node_id: int) -> List[int]:
        return self._cfg_succ.get(node_id, [])

    def predecessors(self, node_id: int) -> List[int]:
        return self._cfg_pred.get(node_id, [])

    # METHODS
    def methods(self) -> List[Node]:
        return [n for n in self.nodes.values() if n.kind == METHOD]

    def parameters(self, method_id: int) -> List[Node]:
        """Formal params in declaration order"""
        return self.children(method_id, PARAMETER)

    def exit_of(self, method_id: int) -> Optional[Node]:
        exits = self.children(method_id, EXIT)
        return exits[0] if exits else None

    def entry_of(self, method_id: int) -> int:
        """ The METHOD node is its own CFG entry."""
        return method_id

    def cfg_nodes(self, method_id: int) -> Set[int]:
        """
        Every CFG node of one function, found by following CFG edges from its entry

        the CFG is per function and disconnected, so this never leaks into another function
        """
        seen: Set[int] = set()
        stack = [method_id]
        while stack:
            current = stack.pop()
            if current in seen:
                continue
            seen.add(current)
            stack.extend(self._cfg_succ[current])
        return seen


def load_case(directory: str):
    """
    Read the 2 input files of a test case directory

    Only sourceNode and sinkNode are taken from test_case.json. The reaches field
    is the expected answer and the spec forbids reading it.
    """

    with open(os.path.join(directory, "graph.json"), "r") as f:
        graph = Graph.from_json(json.load(f))

    with open(os.path.join(directory, "test_case.json"), "r") as f:
        meta = json.load(f)

    case = TestCase(
        source_node=int(meta["sourceNode"]),
        sink_node=int(meta["sinkNode"]),
        directory=directory,
    )
    return graph, case
