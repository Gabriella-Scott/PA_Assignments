"""
The abstract store: what the analysis knows at one program point.
"""

from dataclasses import dataclass, field
from typing import Dict, FrozenSet, Set, Union, Iterable

LIST = "LIST"  # a list allocation site
FUNC = "FUNC"  # a function definition site


@dataclass(frozen=True)
class Loc:
    """
    One abstract location, identified by the graph node that creates it.
    (frozen so it can live in a set) 2 locations are equal exactly when they come from the same site
    """
    kind: str
    site: int

    def __repr__(self) -> str:
        return "%s@%d" % (self.kind, self.site)


# A store key is either a var name or an abstract location
Key = Union[str, Loc]


@dataclass
class Store:

    points_to: Dict[str, Set[Loc]] = field(default_factory=dict)
    tainted: Set[Key] = field(default_factory=set)

    # LATTICE
    def copy(self) -> "Store":
        # the inner set need copying as well, otherwise both stores would share the same sets
        return Store(
            points_to={v: set(locs) for v, locs in self.points_to.items()},
            tainted=set(self.tainted),
        )

    def join(self, other: "Store") -> "Store":
        """
        Meet operator: union, because this is a may analysis.
        Used at merge pts. A var tainted on either incoming path is tainted after the merge,
        and its points-to set is the union of both.
        """
        out = self.copy()
        for var, locs in other.points_to.items():
            out.points_to.setdefault(var, set()).update(locs)
        out.tainted.update(other.tainted)
        return out

    def __eq__(self, other: object) -> bool:
        # the worklist needs this to decide whether an IN set actually changed
        if not isinstance(other, Store):
            return NotImplemented
        return self.points_to == other.points_to and self.tainted == other.tainted

    # READING

    def locs_of(self, var: str) -> Set[Loc]:
        """
        Locations var may point to. Empty if only ever held a scalar value.
        """
        return self.points_to.get(var, set())

    def is_tainted(self, var: str) -> bool:
        """
        Is a read of var tainted? 
        Either the var itself holds tainted data, or it points at a location whose contents are tainted (aliasing and lists)
        """
        if var in self.tainted:
            return True

        return any(loc in self.tainted for loc in self.locs_of(var))

    def any_tainted(self, keys: Iterable[Key]) -> bool:
        return any(self.is_tainted(k) if isinstance(k, str) else k in self.tainted for k in keys)

    def func_targets(self, var: str) -> FrozenSet[Loc]:
        """
        Definition sites var may refer to, for resolving an indirect call.
        Flow sensitive -> reads the store at this program pt, so a later rebinding shadows an earlier one
        """
        return frozenset(loc.site for loc in self.locs_of(var) if loc.kind == FUNC)

    # WRITING

    def assign(self, var: str, tainted: bool, locs: Set[Loc]) -> None:
        """
        Strong update on a var: x = <something>
        Strong (the old value is killed, not merged) because a var name denotes exactly one lot in language.
        There are no pointers to variables, so nothing else can be writing to x at the same time.
        """
        # test5 needs this: x = "OVERWRITE" must clear x's taint
        self.points_to[var] = set(locs)
        if tainted:
            self.tainted.add(var)
        else:
            self.tainted.discard(var)

    def write_through(self, locs: Iterable[Loc], tainted: bool) -> None:
        """
        weak update through locations: lst[i] = <something>
        Weak (taint is added, never removed) for 2 reasons. One location summarises every object that site, so may be
        writing to only one of several. And dont track indices, so the other elements of the same list keep whatever they had.
        Removing taint here would be unsound.
        """
        if not tainted:
            return
        for loc in locs:
            self.tainted.add(loc)

    def taint_loc(self, loc: Loc) -> None:
        """Mark a locations contents tainted"""
        self.tainted.add(loc)

    def __repr__(self) -> str:
        pts = ", ".join("%s->{%s}" % (v, ",".join(sorted(map(str, locs))))
                        for v, locs in sorted(self.points_to.items()) if locs)
        tnt = ", ".join(sorted(map(str, self.tainted)))
        return "Store(pt: %s | tainted: %s)" % (pts or "-", tnt or "-")
