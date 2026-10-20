import pytest

from saengra import Environment
from saengra.graph import AddEdge, AddVertex


@pytest.fixture
def env(empty_env: Environment) -> Environment:
    # 0 -a> 1, 0 -a> 2, 0 -b> 1, 0 -b> 3
    empty_env.update_many(
        [AddVertex(i) for i in range(4)]
        + [AddEdge(0, "a", 1), AddEdge(0, "a", 2), AddEdge(0, "b", 1), AddEdge(0, "b", 3)]
    )
    empty_env.commit()
    return empty_env


def refs_set(env: Environment, expression: str) -> set[tuple]:
    return {tuple(sorted(refs.items())) for refs in env.match(expression, 0)}


def edges_of(env: Environment, expression: str) -> list[set[tuple]]:
    return [{(e.from_, e.label, e.to) for e in sg.edges} for sg in env.match_subgrapghs(expression, 0)]


def test_and_joins_on_shared_ref(env: Environment) -> None:
    assert refs_set(env, "? ((-a> * as x) & (-b> * as x))") == {(("x", 1),)}


def test_and_of_lookaheads_joins_on_shared_ref(env: Environment) -> None:
    # Both operands produce identical (empty) subgraphs for x = 1.
    assert refs_set(env, "? ((?= -a> * as x) & (?= -b> * as x))") == {(("x", 1),)}


def test_and_with_disjoint_refs_is_cartesian_product(env: Environment) -> None:
    assert refs_set(env, "? ((-a> * as x) & (-b> * as y))") == {
        (("x", x), ("y", y)) for x in (1, 2) for y in (1, 3)
    }


def test_and_fails_if_any_operand_fails(env: Environment) -> None:
    assert refs_set(env, "? ((?= -a> *) & (?= -c> *))") == set()
    assert refs_set(env, "? ((?= -a> * as x) & (?= -b> * as x) & (?= -a> * as x -c> *))") == set()


def test_all_of_and_without_refs(env: Environment) -> None:
    assert edges_of(env, "? (all: -a> * & -b> *)") == [
        {(0, "a", 1), (0, "a", 2), (0, "b", 1), (0, "b", 3)}
    ]


def test_all_of_and_with_shared_ref(env: Environment) -> None:
    assert edges_of(env, "? (all: (-a> * as x) & (-b> * as x))") == [{(0, "a", 1), (0, "b", 1)}]
