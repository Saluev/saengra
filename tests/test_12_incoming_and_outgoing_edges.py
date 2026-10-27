from dataclasses import dataclass

import pytest

from saengra import Environment
from saengra.graph import AddEdge, AddVertex, RemoveEdge, Subgraph
from saengra.observer import observer


@dataclass(frozen=True, slots=True)
class Anchor:
    id: int


A = Anchor(0)


def edges_of(sg: Subgraph) -> set[tuple]:
    return {(e.from_, e.label, e.to) for e in sg.edges}


def watch(env: Environment, expression: str) -> list[tuple[str, dict, set[tuple]]]:
    events: list[tuple[str, dict, set[tuple]]] = []
    o = observer(expression, dont_register=True)

    @o.on_create
    def on_create(sg: Subgraph, **refs: object) -> None:
        events.append(("create", refs, edges_of(sg)))

    @o.on_delete
    def on_delete(sg: Subgraph, **refs: object) -> None:
        events.append(("delete", refs, edges_of(sg)))

    env.register_observers([o])
    return events


def commit(env: Environment, *updates: object) -> None:
    env.update_many(list(updates))
    env.commit()


def test_incoming_and_outgoing_edges_to_the_same_vertex_are_matched(
    empty_env: Environment,
) -> None:
    commit(
        empty_env,
        *[AddVertex(v) for v in (A, 1, 2, 3, 4)],
        AddEdge(1, "x", A),
        AddEdge(A, "y", 1),  # A <x&y> 1
        AddEdge(2, "x", A),
        AddEdge(A, "y", 3),  # incoming from 2, outgoing to 3
        AddEdge(A, "x", 4),
        AddEdge(4, "y", A),  # the other way around: 4 <x&y> A
    )
    matched = {
        (sg.refs["w"], frozenset(edges_of(sg)))
        for sg in empty_env.match_subgrapghs("Anchor as a <x&y> * as w")
    }
    assert matched == {(1, frozenset({(1, "x", A), (A, "y", 1)}))}


@pytest.mark.parametrize(
    "expression",
    [
        "Anchor as a <x&y> * as w",
        "<x&y> * as w",  # starting with the edge
        "Anchor as a -e> * as w (?= <y&x> a)",  # to a known vertex
    ],
)
@pytest.mark.parametrize("incoming_first", [True, False])
def test_incoming_and_outgoing_edges_are_tracked(
    empty_env: Environment, expression: str, incoming_first: bool
) -> None:
    events = watch(empty_env, expression)
    commit(empty_env, AddVertex(A), AddVertex(1), AddEdge(A, "e", 1))
    incoming, outgoing = AddEdge(1, "x", A), AddEdge(A, "y", 1)
    if expression.endswith("(?= <y&x> a)"):
        # From 1: incoming `y` from A, outgoing `x` to A.
        incoming, outgoing = outgoing, incoming
    commit(empty_env, incoming if incoming_first else outgoing)
    assert events == []

    commit(empty_env, outgoing if incoming_first else incoming)
    assert [(kind, refs["w"]) for kind, refs, _ in events] == [("create", 1)]
    if not expression.endswith("(?= <y&x> a)"):
        assert events[0][2] == {(1, "x", A), (A, "y", 1)}

    # Edges with the same labels to other vertices don't affect the match.
    commit(
        empty_env,
        AddVertex(2),
        AddEdge(2, "x", A),
        AddEdge(A, "y", 2),
        RemoveEdge(A, "y", 2),
    )
    assert len(events) == 1

    commit(empty_env, RemoveEdge(incoming.from_, incoming.label, incoming.to))
    assert [(kind, refs["w"]) for kind, refs, _ in events] == [
        ("create", 1),
        ("delete", 1),
    ]


@pytest.mark.parametrize(
    ("expression", "matches"),
    [
        ("Anchor <x> *", 0),  # the same label in both directions
        ("Anchor <x&y> *", 1),
        ("Anchor <x|y> *", 1),  # any of the labels in each direction
        ("Anchor <x|z&y> *", 1),
        ("Anchor <y|z&y> *", 0),
        ("Anchor <*&y> *", 1),
        ("Anchor <x&*> *", 1),
        ("Anchor <*> *", 1),
        ("Anchor <y&x> *", 0),
    ],
)
def test_labels_of_both_edges(
    empty_env: Environment, expression: str, matches: int
) -> None:
    commit(
        empty_env, AddVertex(A), AddVertex(1), AddEdge(1, "x", A), AddEdge(A, "y", 1)
    )
    assert len(empty_env.match_subgrapghs(expression)) == matches


@pytest.mark.parametrize(
    "expression", ["Anchor -x&y> *", "Anchor <x&y- *", "Anchor -x&y- *"]
)
def test_incoming_and_outgoing_labels_need_both_arrows(
    empty_env: Environment, expression: str
) -> None:
    with pytest.raises(RuntimeError, match="parse error"):
        empty_env.match_subgrapghs(expression)
