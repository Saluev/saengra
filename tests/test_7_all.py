from dataclasses import dataclass

import pytest

from saengra import Environment
from saengra.graph import AddEdge, AddVertex, RemoveVertex
from saengra.observer import observer

VERTICES = range(8)


@pytest.fixture
def env(empty_env: Environment) -> Environment:
    # 0 -a> 1, 0 -b> 2, 0 -c> 3 -d> 4, 0 -e> 5, 0 -e> 6, 0 -s> 7
    empty_env.update_many(
        [AddVertex(i) for i in VERTICES]
        + [
            AddEdge(0, "a", 1),
            AddEdge(0, "b", 2),
            AddEdge(0, "c", 3),
            AddEdge(3, "d", 4),
            AddEdge(0, "e", 5),
            AddEdge(0, "e", 6),
            AddEdge(0, "s", 7),
        ]
    )
    empty_env.commit()
    return empty_env


def subgraphs(env: Environment, expression: str, *placeholders: object) -> set[tuple]:
    """(refs, edges) of every matched subgraph."""
    return {
        (
            tuple(sorted(sg.refs.items())),
            tuple(sorted((e.from_, e.label, e.to) for e in sg.edges)),
        )
        for sg in env.match_subgrapghs(expression, *placeholders)
    }


A = ((0, "a", 1),)
B = ((0, "b", 2),)
C = ((0, "c", 3), (3, "d", 4))


def test_all_returns_every_cell_of_refspace(env: Environment) -> None:
    # Subgraphs A {x=1}, B {y=2} and C {x=3, y=4} split the refspace into parts covered by
    # A and B, A only, B only and C only. Refs that are only excluded from some values are
    # set to every other vertex of the graph.
    assert subgraphs(env, "? (all: -a> * as x | -b> * as y | -c> * as x -d> * as y)", 0) == {
        ((("x", 1), ("y", 2)), A + B),
        ((("x", 3), ("y", 4)), C),
        *(((("x", 1), ("y", v)), A) for v in VERTICES if v != 2),
        *(((("x", v), ("y", 2)), B) for v in VERTICES if v != 1),
    }


def test_all_cells_dont_depend_on_unrelated_values(env: Environment) -> None:
    # Same as above without C: the cells of A and B are the same.
    assert subgraphs(env, "? (all: -a> * as x | -b> * as y)", 0) == {
        ((("x", 1), ("y", 2)), A + B),
        *(((("x", 1), ("y", v)), A) for v in VERTICES if v != 2),
        *(((("x", v), ("y", 2)), B) for v in VERTICES if v != 1),
    }


def test_all_merges_part_without_refs_into_every_cell(env: Environment) -> None:
    shared = ((0, "s", 7),)
    assert subgraphs(env, "? (all: -s> * | -e> * as z)", 0) == {
        ((("z", 5),), ((0, "e", 5),) + shared),
        ((("z", 6),), ((0, "e", 6),) + shared),
        *(((("z", v),), shared) for v in VERTICES if v not in (5, 6)),
    }


def test_all_without_matches_for_ref_returns_part_without_refs(env: Environment) -> None:
    assert subgraphs(env, "? (all: -s> * | -nonexistent> * as z)", 0) == {((), ((0, "s", 7),))}


def test_and_keeps_only_cells_with_all_operands(env: Environment) -> None:
    # {x=3, y=4} doesn't intersect {y=2}, so its cell has no subgraphs of the second operand.
    assert subgraphs(env, "? ((-a> * as x | -c> * as x -d> * as y) & -b> * as y)", 0) == {
        ((("x", 1), ("y", 2)), A),
        ((("x", 1), ("y", 2)), B),
    }


def test_and_with_excluded_values(env: Environment) -> None:
    # The second operand sets z to 5, 6 and 7; the first one has z=5, z=6 and z ∉ {5, 6}.
    assert subgraphs(env, "? ((all: -s> * | -e> * as z) & -e|s> * as z)", 0) == {
        ((("z", 5),), ((0, "e", 5), (0, "s", 7))),
        ((("z", 5),), ((0, "e", 5),)),
        ((("z", 6),), ((0, "e", 6), (0, "s", 7))),
        ((("z", 6),), ((0, "e", 6),)),
        ((("z", 7),), ((0, "s", 7),)),
    }


def test_setting_excluded_ref_doesnt_match(empty_env: Environment) -> None:
    # 0 -e> 1, 0 -e> 2, 0 -s> 1. The part with z ∉ {1, 2} only has the -s> edge, which ends at
    # vertex 1, so `z` can't be set there. (z=2 ends at vertex 1 too, so only z=1 matches.)
    empty_env.update_many(
        [AddVertex(i) for i in range(3)] + [AddEdge(0, "e", 1), AddEdge(0, "e", 2), AddEdge(0, "s", 1)]
    )
    empty_env.commit()
    assert subgraphs(empty_env, "? (all: -s> | -e> z) * as z", 0) == {
        ((("z", 1),), ((0, "e", 1), (0, "s", 1))),
    }


@dataclass(frozen=True, slots=True)
class Anchor:
    id: int


def test_excluded_values_are_tracked_incrementally(empty_env: Environment) -> None:
    a = Anchor(0)
    created: list[object] = []
    deleted: list[object] = []
    o = observer("Anchor as s (all: -s> * | -e> * as z)", dont_register=True)
    o.on_create(lambda s, z=None: created.append(z))
    o.on_delete(lambda s, z=None: deleted.append(z))
    empty_env.register_observers([o])

    empty_env.update_many([AddVertex(a), AddVertex(1), AddVertex(2), AddEdge(a, "e", 1), AddEdge(a, "s", 2)])
    empty_env.commit()
    # z=1 has both edges; every other vertex has the -s> edge only.
    assert sorted(created, key=repr) == sorted([1, a, 2], key=repr)

    created.clear()
    empty_env.update(AddVertex(100))
    empty_env.commit()
    assert created == [100]

    empty_env.update(RemoveVertex(100))
    empty_env.commit()
    assert deleted == [100]
