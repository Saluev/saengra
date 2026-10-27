from dataclasses import dataclass

import pytest

from saengra import Environment
from saengra.graph import AddEdge, AddVertex, RemoveEdge
from saengra.observer import observer


@pytest.fixture
def env(empty_env: Environment) -> Environment:
    # 0 -a> 1, 0 -a> 2, 0 -b> 1, 0 -b> 2, 0 -b> 3, 0 -c> 3, 1 -c> 2
    empty_env.update_many(
        [AddVertex(i) for i in range(4)]
        + [
            AddEdge(0, "a", 1),
            AddEdge(0, "a", 2),
            AddEdge(0, "b", 1),
            AddEdge(0, "b", 2),
            AddEdge(0, "b", 3),
            AddEdge(0, "c", 3),
            AddEdge(1, "c", 2),
        ]
    )
    empty_env.commit()
    return empty_env


def refs_set(env: Environment, expression: str, *placeholders: object) -> set[tuple]:
    return {tuple(sorted(refs.items())) for refs in env.match(expression, *placeholders)}


def test_negation_excludes_values_of_shared_ref(env: Environment) -> None:
    assert refs_set(env, "? (?! -a> * as x) -b> * as x", 0) == {(("x", 3),)}


@pytest.mark.parametrize(("expression", "expected"), [("? (?! -a> * as y)", set()), ("? (?! -z> * as y)", {()})])
def test_negation_quantifies_local_ref_away(env: Environment, expression: str, expected: set[tuple]) -> None:
    # `y` occurs only inside the negation, so it means "there's no such y".
    assert refs_set(env, expression, 0) == expected


@pytest.mark.parametrize(("expression", "expected"), [("? (?! -a> * as y & -b> y)", set()), ("? (?! -a> * as y & -c> y)", {()})])
def test_negation_joins_branches_by_local_ref(env: Environment, expression: str, expected: set[tuple]) -> None:
    # Vertices 1 and 2 are reachable by both -a> and -b>, but no vertex by both -a> and -c>.
    assert refs_set(env, expression, 0) == expected


def test_negation_with_several_shared_refs(env: Environment) -> None:
    # Only the pair x=1, y=2 is excluded.
    assert refs_set(env, "? (?! -a> * as x -c> * as y) (?= -b> * as x) (?= -b> * as y)", 0) == {
        (("x", x), ("y", y)) for x in (1, 2, 3) for y in (1, 2, 3) if (x, y) != (1, 2)
    }


def test_double_negation(env: Environment) -> None:
    assert refs_set(env, "? (?! (?! -a> * as x)) -b> x", 0) == {(("x", 1),), (("x", 2),)}


def test_exclusion_reaching_result_is_expanded(env: Environment) -> None:
    # `x` is shared with the optional branch, which doesn't match, so `x` is anything but 1 and 2.
    assert refs_set(env, "? (?! -a> * as x) (maybe: -z> x)", 0) == {(("x", 0),), (("x", 3),)}


@dataclass(frozen=True, slots=True)
class Anchor:
    id: int


def test_exclusions_are_tracked_incrementally(empty_env: Environment) -> None:
    a = Anchor(0)
    created: list[object] = []
    deleted: list[object] = []
    o = observer("Anchor as s (?! -a> * as x) -b> * as x", dont_register=True)
    o.on_create(lambda s, x: created.append(x))
    o.on_delete(lambda s, x: deleted.append(x))
    empty_env.register_observers([o])

    empty_env.update_many([AddVertex(a), AddVertex(1), AddVertex(2), AddEdge(a, "a", 1), AddEdge(a, "b", 1), AddEdge(a, "b", 2)])
    empty_env.commit()
    assert created == [2]

    empty_env.update(AddEdge(a, "a", 2))
    empty_env.commit()
    assert deleted == [2]

    created.clear()
    empty_env.update_many([RemoveEdge(a, "a", 1), RemoveEdge(a, "a", 2)])
    empty_env.commit()
    assert sorted(created) == [1, 2]
