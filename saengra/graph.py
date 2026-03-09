from dataclasses import dataclass, field
from typing import Hashable, Literal, TypeAlias

from saengra.frozendict import frozendict

Primitive: TypeAlias = Hashable
Vertex: TypeAlias = Primitive
Label: TypeAlias = str


# Store reference to the function in module globals for maximum performance.
_object_setattr = object.__setattr__


@dataclass(frozen=True, slots=True)
class Edge:
    from_: Vertex
    label: Label
    to: Vertex

    @property
    def reverse(self) -> "Edge":
        return Edge(from_=self.to, label=self.label, to=self.from_)

    # Manual implementations are way faster compared to built-in _dataclass_getstate()/_dataclass_setstate().
    # And since edges can be frequently pickled (e. g. to save graph snapshot), it's better to have this here.
    def __getstate__(self) -> tuple[Vertex, Label, Vertex]:
        return self.from_, self.label, self.to

    def __setstate__(self, state: tuple[Vertex, Label, Vertex]) -> None:
        _object_setattr(self, "from_", state[0])
        _object_setattr(self, "label", state[1])
        _object_setattr(self, "to", state[2])


def reverse(edge: Edge) -> Edge:
    return Edge(from_=edge.to, label=edge.label, to=edge.from_)


@dataclass(frozen=True, slots=True)
class AddVertex:
    primitive: Primitive


@dataclass(frozen=True, slots=True)
class AddEdge:
    from_: Primitive
    label: Label
    to: Primitive


@dataclass(frozen=True, slots=True)
class RemoveVertex:
    primitive: Primitive


@dataclass(frozen=True, slots=True)
class RemoveEdge:
    from_: Primitive
    label: Label
    to: Primitive


@dataclass(frozen=True, slots=True)
class RemoveEdgesToAll:
    from_: Primitive
    label: Label


Update = AddVertex | AddEdge | RemoveVertex | RemoveEdge | RemoveEdgesToAll


@dataclass(frozen=True, slots=True)
class Position:
    vertex: Vertex
    point: Literal[".", "o"]


Refs: TypeAlias = frozendict[str, Vertex]


@dataclass(frozen=True, slots=True)
class Subgraph:
    start_position: Position
    end_positions: frozenset[Position]
    vertices: frozenset[Vertex] = field(default=frozenset())
    edges: frozenset[Edge] = field(default=frozenset())
    refs: Refs = field(default=Refs())
