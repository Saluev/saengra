import pytest

from saengra import Environment
from saengra.graph import (
    AddVertex,
    AddEdge,
    Edge,
    RemoveVertex,
    RemoveEdge,
    RemoveEdgesToAll,
)
from tests.scenarios.game import Biome


def test_add_vertex_unhashable(empty_env: Environment) -> None:
    with pytest.raises(TypeError):
        empty_env.update(AddVertex([]))


def test_add_vertex_unpicklable(empty_env: Environment) -> None:
    class Unpicklable:
        def __reduce__(self):
            raise TypeError("unpicklable")

    with pytest.raises(TypeError):
        empty_env.update(AddVertex(Unpicklable()))


def test_add_vertex(empty_env: Environment) -> None:
    empty_env.update(AddVertex(primitive="foo"))
    assert empty_env.find_all_of_type(str) == {"foo"}

    empty_env.commit()
    assert empty_env.find_all_of_type(str) == {"foo"}


def test_add_vertex_deduplication(empty_env: Environment) -> None:
    empty_env.update(AddVertex(primitive="foo"), AddVertex(primitive="bar"))
    vertices, _ = empty_env.find_all()
    assert {*vertices} == {"foo", "bar"}

    empty_env.update(AddVertex(primitive="foo"))
    vertices, _ = empty_env.find_all()
    assert len(vertices) == 2


def test_add_vertex_distinguishes_int_from_bool(empty_env: Environment) -> None:
    empty_env.update(AddVertex(primitive=0), AddVertex(primitive=False))
    vertices, _ = empty_env.find_all()
    assert len(vertices) == 2


def test_add_vertex_distinguishes_int_from_float(empty_env: Environment) -> None:
    empty_env.update(AddVertex(primitive=0), AddVertex(primitive=0.0))
    vertices, _ = empty_env.find_all()
    assert len(vertices) == 2


def test_add_vertex_distinguishes_str_from_str_enum(empty_env: Environment) -> None:
    empty_env.update(
        AddVertex(primitive=Biome.LAKE.value), AddVertex(primitive=Biome.LAKE)
    )
    vertices, _ = empty_env.find_all()
    assert len(vertices) == 2


def test_add_edge_without_both_vertices(empty_env: Environment) -> None:
    empty_env.update(AddEdge("from", "label", "to"))
    vertices, edges = empty_env.find_all()
    assert not vertices and not edges, "shouldn't add edge"


def test_add_edge_without_from_vertex(empty_env: Environment) -> None:
    empty_env.update(AddVertex("to"), AddEdge("from", "label", "to"))
    vertices, edges = empty_env.find_all()
    assert {*vertices} == {"to"} and not edges, "shouldn't add edge"


def test_add_edge_without_to_vertex(empty_env: Environment) -> None:
    empty_env.update(AddVertex("from"), AddEdge("from", "label", "to"))
    vertices, edges = empty_env.find_all()
    assert {*vertices} == {"from"} and not edges, "shouldn't add edge"


def test_add_edge_non_string_label(empty_env: Environment) -> None:
    empty_env.update(AddVertex("from"), AddVertex("to"))
    with pytest.raises(TypeError):
        empty_env.update(AddEdge("from", 1, "to"))


def test_add_edge(empty_env: Environment) -> None:
    empty_env.update(AddVertex("from"), AddVertex("to"), AddEdge("from", "label", "to"))
    vertices, edges = empty_env.find_all()
    assert {*vertices} == {"from", "to"} and edges == [Edge("from", "label", "to")]


def test_add_edge_deduplication(empty_env: Environment) -> None:
    empty_env.update(AddVertex("from"), AddVertex("to"), AddEdge("from", "label", "to"))
    empty_env.update(AddEdge("from", "label", "to"))
    _, edges = empty_env.find_all()
    assert edges == [Edge("from", "label", "to")]


def test_remove_nonexisting_vertex(empty_env: Environment) -> None:
    try:
        empty_env.update(RemoveVertex("foo"))
        empty_env.commit()
    except Exception as e:
        assert not e, "should not raise exception"


def test_remove_specific_vertex(empty_env: Environment) -> None:
    empty_env.update(AddVertex("foo"), AddVertex("bar"))
    empty_env.update(RemoveVertex("foo"))
    vertices, _ = empty_env.find_all()
    assert vertices == ["bar"]


def test_remove_vertex_before_commit(empty_env: Environment) -> None:
    empty_env.update(AddVertex("foo"))
    empty_env.update(RemoveVertex("foo"))

    assert_no_vertices(empty_env)

    empty_env.commit()
    assert_no_vertices(empty_env)


def test_remove_vertex_after_commit(empty_env: Environment) -> None:
    empty_env.update(AddVertex("foo"))
    empty_env.commit()
    empty_env.update(RemoveVertex("foo"))

    assert_no_vertices(empty_env)

    empty_env.commit()
    assert_no_vertices(empty_env)


def test_remove_nonexisting_edge_without_both_vertices(empty_env: Environment) -> None:
    try:
        empty_env.update(RemoveEdge("from", "label", "to"))
        empty_env.commit()
    except Exception as e:
        assert not e, "should not raise exception"


def test_remove_nonexisting_edge_without_from_vertex(empty_env: Environment) -> None:
    empty_env.update(AddVertex("to"))
    try:
        empty_env.update(RemoveEdge("from", "label", "to"))
        empty_env.commit()
    except Exception as e:
        assert not e, "should not raise exception"


def test_remove_nonexisting_edge_without_to_vertex(empty_env: Environment) -> None:
    empty_env.update(AddVertex("from"))
    try:
        empty_env.update(RemoveEdge("from", "label", "to"))
        empty_env.commit()
    except Exception as e:
        assert not e, "should not raise exception"


def test_remove_nonexisting_edge(empty_env: Environment) -> None:
    empty_env.update(AddVertex("from"), AddVertex("to"))
    try:
        empty_env.update(RemoveEdge("from", "label", "to"))
        empty_env.commit()
    except Exception as e:
        assert not e, "should not raise exception"


def test_remove_nonexisting_edge_before_commit(empty_env: Environment) -> None:
    empty_env.update(AddVertex("from"), AddVertex("to"), AddEdge("from", "label", "to"))
    empty_env.update(RemoveEdge("from", "label", "to"))
    assert_no_edges(empty_env)
    empty_env.commit()
    assert_no_edges(empty_env)


def test_remove_nonexisting_edge_after_commit(empty_env: Environment) -> None:
    empty_env.update(AddVertex("from"), AddVertex("to"), AddEdge("from", "label", "to"))
    empty_env.commit()
    empty_env.update(RemoveEdge("from", "label", "to"))
    assert_no_edges(empty_env)
    empty_env.commit()
    assert_no_edges(empty_env)


def test_remove_edges_to_all_from_nonexisting_vertex(empty_env: Environment) -> None:
    try:
        empty_env.update(RemoveEdgesToAll("from", "label"))
        empty_env.commit()
    except Exception as e:
        assert not e, "should not raise exception"


def test_remove_edges_to_all_with_nonexisting_label(empty_env: Environment) -> None:
    empty_env.update(
        AddVertex("from"), AddVertex("to"), AddEdge("from", "other_label", "to")
    )
    try:
        empty_env.update(RemoveEdgesToAll("from", "label"))
        empty_env.commit()
    except Exception as e:
        assert not e, "should not raise exception"
    _, edges = empty_env.find_all()
    assert edges == [Edge("from", "other_label", "to")]


@pytest.mark.parametrize("commit_before_removal", [False, True])
def test_remove_edges_to_all(
    empty_env: Environment, commit_before_removal: bool
) -> None:
    empty_env.update(
        AddVertex("from"),
        AddVertex("to1"),
        AddVertex("to2"),
        AddEdge("from", "label", "to1"),
        AddEdge("from", "label", "to2"),
        AddEdge("from", "other_label", "to2"),
    )
    if commit_before_removal:
        empty_env.commit()
    empty_env.update(RemoveEdgesToAll("from", "label"))
    _, edges = empty_env.find_all()
    assert edges == [Edge("from", "other_label", "to2")]
    empty_env.commit()
    _, edges = empty_env.find_all()
    assert edges == [Edge("from", "other_label", "to2")]


def assert_no_vertices(env: Environment) -> None:
    vertices, _ = env.find_all()
    assert not vertices


def assert_no_edges(env: Environment) -> None:
    _, edges = env.find_all()
    assert not edges
