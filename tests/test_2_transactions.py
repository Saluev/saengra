from saengra import Environment
from saengra.graph import AddVertex, RemoveVertex, AddEdge, Edge, RemoveEdge


def test_rollback_adding_vertex(empty_env: Environment) -> None:
    empty_env.update(AddVertex(1), AddVertex(2))
    empty_env.commit()

    empty_env.update(AddVertex(3))
    empty_env.rollback()
    vertices, _ = empty_env.find_all()
    assert {*vertices} == {1, 2}


def test_rollback_removing_vertex(empty_env: Environment) -> None:
    empty_env.update(AddVertex(1), AddVertex(2), AddVertex(3))
    empty_env.commit()

    empty_env.update(RemoveVertex(2))
    empty_env.rollback()
    vertices, _ = empty_env.find_all()
    assert {*vertices} == {1, 2, 3}


def test_rollback_adding_edge(empty_env: Environment) -> None:
    empty_env.update(AddVertex(1), AddVertex(2), AddVertex(3), AddEdge(1, "label", 2))
    empty_env.commit()

    empty_env.update(AddEdge(1, "label", 3))
    empty_env.rollback()
    _, edges = empty_env.find_all()
    assert edges == [Edge(1, "label", 2)]


def test_rollback_removing_edge(empty_env: Environment) -> None:
    empty_env.update(AddVertex(1), AddVertex(2), AddVertex(3), AddEdge(1, "label", 2))
    empty_env.commit()

    empty_env.update(RemoveEdge(1, "label", 2))
    empty_env.rollback()
    _, edges = empty_env.find_all()
    assert edges == [Edge(1, "label", 2)]
