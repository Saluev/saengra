from typing import Iterable

from saengra.environment import Environment
from saengra.graph import (
    AddVertex,
    AddEdge,
    RemoveVertex,
    RemoveEdge,
    RemoveEdgesToAll,
    Update,
    Primitive,
    Label,
    Subgraph,
    Position,
    Refs,
)
from saengra.api import Edge


def add_vertex(value: Primitive) -> Update:
    return AddVertex(primitive=value)


def remove_vertex(value: Primitive) -> Update:
    return RemoveVertex(primitive=value)


def add_edge(from_: Primitive, label: str, to: Primitive) -> Update:
    return AddEdge(from_=from_, label=label, to=to)


def remove_edge(from_: Primitive, label: str, to: Primitive) -> Update:
    return RemoveEdge(from_=from_, label=label, to=to)


def remove_edges_to_all(from_: Primitive, label: str) -> Update:
    return RemoveEdgesToAll(from_=from_, label=label)


def edge(from_: Primitive, label: str, to: Primitive) -> Edge:
    return Edge(from_=from_, label=label, to=to)


def add_vertices(e: Environment, *vertices: Primitive) -> None:
    e.update(*[AddVertex(v) for v in vertices])


def add_edges(e: Environment, *edges: Primitive | Label) -> None:
    it = iter(edges)
    e.update(
        *[
            AddEdge(from_, label, to)
            for from_, label, to in zip(it, it, it, strict=True)
        ]
    )


def subgraph(
    starts_inside: Primitive,
    *,
    ends_inside: Iterable[Primitive] = (),
    ends_outside: Iterable[Primitive] = (),
    vertices: Iterable[Primitive] = (),
    edges: Iterable[Edge | tuple[Primitive, Label, Primitive]] = (),
    **refs: Primitive,
) -> Subgraph:
    return Subgraph(
        start_position=Position(starts_inside, "."),
        end_positions=frozenset(Position(v, ".") for v in ends_inside)
        | frozenset(Position(v, "o") for v in ends_outside),
        vertices=frozenset(vertices),
        edges=frozenset(e if isinstance(e, Edge) else Edge(*e) for e in edges),
        refs=Refs(refs),
    )
