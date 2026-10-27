import pytest

from saengra import Environment
from saengra.graph import AddEdge, AddVertex


@pytest.fixture
def env(empty_env: Environment) -> Environment:
    # 0 -a> 1, 0 -a> 2, 0 -b> 1, 0 -b> 3, 1 -c> 1, 1 -c> 2
    empty_env.update_many(
        [AddVertex(i) for i in range(4)]
        + [
            AddEdge(0, "a", 1),
            AddEdge(0, "a", 2),
            AddEdge(0, "b", 1),
            AddEdge(0, "b", 3),
            AddEdge(1, "c", 1),
            AddEdge(1, "c", 2),
        ]
    )
    empty_env.commit()
    return empty_env


def refs_set(env: Environment, expression: str, *placeholders: object) -> set[tuple]:
    return {tuple(sorted(refs.items())) for refs in env.match(expression, *placeholders)}


def test_reference_before_binding_in_and(env: Environment) -> None:
    # `x` in the first operand is a reference, even though `as x` comes later.
    assert refs_set(env, "? ((-a> x) & (-b> * as x))", 0) == {(("x", 1),)}


def test_reference_before_binding_in_lookahead(env: Environment) -> None:
    assert refs_set(env, "? (?= -a> x) -b> * as x", 0) == {(("x", 1),)}


def test_reference_binds_if_optional_binding_did_not_match(env: Environment) -> None:
    assert refs_set(env, "? (maybe: -z> * as x) -a> x", 0) == {(("x", 1),), (("x", 2),)}


def test_reference_checks_if_optional_binding_matched(env: Environment) -> None:
    # The lookahead keeps the position at the start, so that `-a>` continues from it.
    assert refs_set(env, "? (maybe: (?= -b> * as x)) -a> x", 0) == {(("x", 1),)}


def test_binding_bound_ref_requires_same_vertex(env: Environment) -> None:
    # The second `as x` doesn't override x, it requires the vertex to be the same.
    assert refs_set(env, "? -a> * as x -c> * as x", 0) == {(("x", 1),)}


def test_leading_reference(env: Environment) -> None:
    # Without `as x` anywhere, `x` would be a type name.
    assert refs_set(env, "x -c> * as x") == {(("x", 1),)}


@pytest.mark.parametrize("expression", ["int as x -a> x as y", "int as int -a> *"])
def test_name_used_as_type_and_ref(env: Environment, expression: str) -> None:
    with pytest.raises(Exception, match="used both as a type name and as a ref name"):
        env.match(expression)
