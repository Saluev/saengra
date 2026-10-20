from dataclasses import dataclass
from typing import Callable

from saengra import Environment
from saengra.graph import AddEdge, AddVertex, RemoveEdge, RemoveEdgesToAll, Update
from saengra.observer import Observer, observer

from ..workload import Op


@dataclass(frozen=True, slots=True)
class Segment:
    id: int


@dataclass(frozen=True, slots=True)
class Switch:
    id: int


@dataclass(frozen=True, slots=True)
class Sensor:
    id: int


@dataclass(frozen=True, slots=True)
class Route:
    id: int


@dataclass(frozen=True, slots=True)
class SwitchPosition:
    id: int


@dataclass(frozen=True, slots=True)
class Semaphore:
    id: int


TYPES = {
    t.__name__: t for t in (Segment, Switch, Sensor, Route, SwitchPosition, Semaphore)
}

# Attribute values are vertices too (ints, strs and bools), connected with attribute-labeled edges.
#
# A Saengra observer re-matches the whole pattern from every start position whose dependencies
# changed, so the choice of the first vertex (anchor) defines the granularity of invalidation.
# These patterns follow the Cypher originals, anchored at e.g. Semaphore or Route, which makes
# every start position cover an entire route.
QUERIES_CYPHER_ORDER: dict[str, tuple[str, tuple]] = {
    "PosLength": ("Segment as s -nonPositiveLength> int as l", ()),
    "SwitchMonitored": ("Switch as sw (?! -monitoredBy>)", ()),
    "RouteSensor": (
        "Route as r -follows> SwitchPosition as swP -target> Switch as sw"
        " -monitoredBy> Sensor as sen (?! <requires- r)",
        (),
    ),
    "SwitchSet": (
        "Semaphore as sem (?= -signal> ?) <entry- Route as r (?= -active> ?)"
        " -follows> SwitchPosition as swP -target> Switch as sw"
        " -currentPosition> str as cp (?! <position- swP)",
        ("GO", True),
    ),
    "ConnectedSegments": (
        "Sensor as sen <monitoredBy- Segment as s1"
        " -connectsTo> Segment as s2 (?= -monitoredBy> sen)"
        " -connectsTo> Segment as s3 (?= -monitoredBy> sen)"
        " -connectsTo> Segment as s4 (?= -monitoredBy> sen)"
        " -connectsTo> Segment as s5 (?= -monitoredBy> sen)"
        " -connectsTo> Segment as s6 (?= -monitoredBy> sen)",
        (),
    ),
    "SemaphoreNeighbor": (
        "Semaphore as sem <exit- Route as r1 -requires> Sensor as sen1"
        " <monitoredBy- * as te1 -connectsTo> * as te2 -monitoredBy> Sensor as sen2"
        " <requires- (?! r1) Route as r2 (?! -entry> sem)",
        (),
    ),
}

# Same queries anchored at their most local element, reaching the rest via lookaheads.
# This also avoids walking into shared value vertices (e.g. all `position` edges to "STRAIGHT").
QUERIES: dict[str, tuple[str, tuple]] = {
    **QUERIES_CYPHER_ORDER,
    "RouteSensor": (
        "Switch as sw (?= <target- SwitchPosition as swP <follows- Route as r)"
        " -monitoredBy> Sensor as sen (?! <requires- r)",
        (),
    ),
    "SwitchSet": (
        "SwitchPosition as swP (?= -position> str as p)"
        " (?= <follows- Route as r (?= -active> ?) -entry> Semaphore as sem (?= -signal> ?))"
        " -target> Switch as sw (?! -currentPosition> p)",
        (True, "GO"),
    ),
    "SemaphoreNeighbor": (
        "* as te1 (?= -monitoredBy> Sensor as sen1 <requires- Route as r1 -exit> Semaphore as sem)"
        " -connectsTo> * as te2 -monitoredBy> Sensor as sen2 <requires- (?! r1) Route as r2"
        " (?! -entry> sem)",
        (),
    ),
}

# Saengra patterns have no value predicates, so `length <= 0` is maintained by a helper
# observer as a `nonPositiveLength` edge. Its cost is a part of the measured time.
NON_POSITIVE_LENGTH_HELPER = "Segment as s -length> int as l"


class SaengraEngine:
    name = "Saengra"
    algorithm = "dependency-tracked re-matching"
    language = "C++ core, Python callbacks"

    def __init__(self, queries: dict[str, tuple[str, tuple]] = QUERIES) -> None:
        self.env = Environment()
        self.primitives: dict[int, object] = {}
        self.matches: dict[str, set[tuple]] = {q: set() for q in queries}

        observers: list[Observer] = []
        for query, (expression, placeholders) in queries.items():
            o = observer(expression, *placeholders, dont_register=True)
            o.on_create(self._make_handler(self.matches[query].add))
            o.on_delete(self._make_handler(self.matches[query].discard))
            observers.append(o)

        helper = observer(NON_POSITIVE_LENGTH_HELPER, dont_register=True)

        @helper.on_create
        def on_length_created(s: Segment, l: int) -> None:
            if l <= 0:
                self.env.update(AddEdge(s, "nonPositiveLength", l))

        @helper.on_delete
        def on_length_deleted(s: Segment, l: int) -> None:
            if l <= 0:
                self.env.update(RemoveEdge(s, "nonPositiveLength", l))

        observers.append(helper)
        self.env.register_observers(observers)

    @staticmethod
    def _make_handler(callback: Callable[[tuple], None]) -> Callable[..., None]:
        def handler(**refs: object) -> None:
            callback(tuple(sorted(refs.items())))

        return handler

    def _to_updates(self, ops: list[Op]) -> list[Update]:
        updates: list[Update] = []
        primitives = self.primitives
        for op in ops:
            match op:
                case ("N", type_, id_):
                    p = primitives[id_] = TYPES[type_](id_)
                    updates.append(AddVertex(p))
                case ("E", label, a, b):
                    updates.append(AddEdge(primitives[a], label, primitives[b]))
                case ("R", label, a, b):
                    updates.append(RemoveEdge(primitives[a], label, primitives[b]))
                case ("A", attr, id_, value):
                    p = primitives[id_]
                    updates.append(RemoveEdgesToAll(p, attr))
                    updates.append(AddVertex(value))
                    updates.append(AddEdge(p, attr, value))
        return updates

    def load(self, ops: list[Op]) -> None:
        self.env.update_many(self._to_updates(ops))
        self.env.commit()

    def transform(self, ops: list[Op]) -> None:
        self.env.update_many(self._to_updates(ops))
        self.env.commit()

    def check(self) -> dict[str, int]:
        return {q: len(ms) for q, ms in self.matches.items()}
