from dataclasses import dataclass, field
from typing import TypeAlias, Hashable, Literal

from saengra.frozendict import frozendict

Primitive: TypeAlias = Hashable
Vertex: TypeAlias = Primitive
Label: TypeAlias = str


@dataclass(frozen=True, slots=True)
class Edge:
    from_: Vertex
    label: Label
    to: Vertex

    @property
    def reverse(self) -> "Edge":
        return Edge(from_=self.to, label=self.label, to=self.from_)


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
