#!/usr/bin/env python3
"""
Development helper: print what the loader made of a test case.
Usage: python3 tools/dump_case.py testcases/test5
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from analysis.graph import load_case  # noqa: E402


def dump_ast(graph, node_id, depth=0):
    node = graph.node(node_id)
    print("%s%s" % ("  " * depth, node))
    for child in graph.children(node_id):
        dump_ast(graph, child.id, depth + 1)


def main():
    graph, case = load_case(sys.argv[1])

    print("source: %s" % graph.node(case.source_node))
    print("sink:   %s" % graph.node(case.sink_node))

    for method in graph.methods():
        params = ", ".join(p.val for p in graph.parameters(method.id))
        print("\n=== def %s(%s)" % (method.val, params))

        print("-- AST")
        dump_ast(graph, method.id, 1)

        print("-- CFG")
        for node_id in sorted(graph.cfg_nodes(method.id)):
            succs = [str(s) for s in graph.successors(node_id)]
            print("  %s -> [%s]" % (graph.node(node_id), ", ".join(succs)))


if __name__ == "__main__":
    main()
