from dataclasses import dataclass

import pytest

from saengra import Environment
from saengra.graph import AddEdge, AddVertex, RemoveEdge, RemoveVertex
from saengra.observer import observer


@dataclass(frozen=True, slots=True)
class Anchor:
    id: int


A = Anchor(0)


def watch(env: Environment, expression: str, *placeholders: object) -> list[tuple[str, object]]:
    events: list[tuple[str, object]] = []
    o = observer(expression, *placeholders, dont_register=True)
    o.on_create(lambda a, x: events.append(("create", x)))
    o.on_delete(lambda a, x: events.append(("delete", x)))
    env.register_observers([o])
    return events


def commit(env: Environment, *updates: object) -> None:
    env.update_many(list(updates))
    env.commit()


@pytest.mark.parametrize(
    ("lookahead", "edge"),
    [
        ("(?= -f> a)", (1, "f", A)),
        ("(?= <f- a)", (A, "f", 1)),
        ("(?= <f> a)", None),  # both directions
    ],
)
def test_edge_to_bound_ref_is_tracked(empty_env: Environment, lookahead: str, edge: tuple | None) -> None:
    events = watch(empty_env, f"Anchor as a -e> * as x {lookahead}")
    commit(empty_env, AddVertex(A), AddVertex(1), AddVertex(2), AddEdge(A, "e", 1))
    assert events == []

    edges = [AddEdge(*edge)] if edge else [AddEdge(1, "f", A), AddEdge(A, "f", 1)]
    commit(empty_env, *edges)
    assert events == [("create", 1)]

    # Other edges with the same label don't affect the match.
    commit(empty_env, AddEdge(1, "f", 2), AddEdge(2, "f", A), AddEdge(A, "f", 2), AddEdge(2, "f", 1))
    commit(empty_env, RemoveEdge(1, "f", 2), RemoveEdge(2, "f", A))
    assert events == [("create", 1)]

    removed = edges[0]
    commit(empty_env, RemoveEdge(removed.from_, removed.label, removed.to))
    assert events == [("create", 1), ("delete", 1)]


def test_edge_to_removed_vertex_is_tracked(empty_env: Environment) -> None:
    events = watch(empty_env, "Anchor as a -e> * as x (?= -f> a)")
    commit(empty_env, AddVertex(A), AddVertex(1), AddEdge(A, "e", 1), AddEdge(1, "f", A))
    commit(empty_env, RemoveVertex(1))
    assert events == [("create", 1), ("delete", 1)]


def test_edge_to_placeholder_added_later_is_tracked(empty_env: Environment) -> None:
    target = Anchor(9)
    events = watch(empty_env, "Anchor as a -e> * as x (?= -f> ?)", target)
    commit(empty_env, AddVertex(A), AddVertex(1), AddEdge(A, "e", 1))
    commit(empty_env, AddVertex(target))
    assert events == []

    commit(empty_env, AddEdge(1, "f", target))
    assert events == [("create", 1)]

    commit(empty_env, RemoveEdge(1, "f", target))
    assert events == [("create", 1), ("delete", 1)]
