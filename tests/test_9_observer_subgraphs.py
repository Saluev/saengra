from dataclasses import dataclass

from saengra import Environment, messages_pb2
from saengra.api import ObservationType
from saengra.api import Observer as APIObserver
from saengra.conversions import Converter
from saengra.graph import AddEdge, AddVertex, Edge, Position, Refs, RemoveVertex, Subgraph
from saengra.observer import observer


@dataclass(frozen=True, slots=True)
class Anchor:
    id: int


def edges(sg: Subgraph) -> set[tuple]:
    return {(e.from_, e.label, e.to) for e in sg.edges}


def test_handler_receives_subgraph(empty_env: Environment) -> None:
    a = Anchor(0)
    received: list[tuple[str, Subgraph]] = []
    o = observer("Anchor as s (all: -e> *)", dont_register=True)

    @o.on_create
    def on_create(s: Anchor, sg: Subgraph) -> None:
        received.append(("create", sg))

    @o.on_change
    def on_change(s: Anchor, sg: Subgraph) -> None:
        received.append(("change", sg))

    @o.on_delete
    def on_delete(s: Anchor, sg: Subgraph) -> None:
        received.append(("delete", sg))

    empty_env.register_observers([o])

    empty_env.update_many([AddVertex(a), AddVertex(1), AddEdge(a, "e", 1)])
    empty_env.commit()
    empty_env.update_many([AddVertex(2), AddEdge(a, "e", 2)])
    empty_env.commit()
    empty_env.update(RemoveVertex(a))
    empty_env.commit()

    assert [(kind, edges(sg)) for kind, sg in received] == [
        ("create", {(a, "e", 1)}),
        ("change", {(a, "e", 1), (a, "e", 2)}),
        ("delete", {(a, "e", 1), (a, "e", 2)}),
    ]
    assert all(sg.refs == Refs({"s": a}) for _, sg in received)


def test_subgraph_is_passed_to_argument_declared_as_subgraph(empty_env: Environment) -> None:
    a = Anchor(0)
    received: list[tuple[Anchor, Subgraph]] = []
    o = observer("Anchor as s -e> *", dont_register=True)

    @o.on_create
    def on_create(matched: Subgraph, s: Anchor) -> None:
        received.append((s, matched))

    empty_env.register_observers([o])
    empty_env.update_many([AddVertex(a), AddVertex(1), AddEdge(a, "e", 1)])
    empty_env.commit()

    ((s, sg),) = received
    assert s == a
    assert edges(sg) == {(a, "e", 1)}


def test_subgraphs_are_requested_only_if_some_handler_expects_one() -> None:
    o = observer("Anchor as s", dont_register=True)

    @o.on_create
    def on_create(s: Anchor) -> None:
        pass

    assert not o.to_api().with_subgraphs

    @o.on_delete
    def on_delete(s: Anchor, sg: Subgraph) -> None:
        pass

    assert o.to_api().with_subgraphs


def test_subgraph_conversions() -> None:
    converter = Converter()
    observer_proto = messages_pb2.Observe.Observer()
    converter.observer_to_proto(APIObserver(id="o", expression="Anchor as s", with_subgraphs=True), observer_proto)
    assert converter.observer_from_proto(observer_proto).with_subgraphs

    a = Anchor(0)
    proto = messages_pb2.CommitResponse.Observation(observer_id="o", type=messages_pb2.CommitResponse.Observation.ON_CREATE)
    converter.vertex_to_proto(a, proto.variables["s"])
    assert converter.observation_from_proto(proto).subgraph is None

    start = proto.subgraph.start_position
    converter.vertex_to_proto(a, start.vertex)
    start.kind = messages_pb2.Position.CORE
    end = proto.subgraph.end_positions.add()
    converter.vertex_to_proto(1, end.vertex)
    end.kind = messages_pb2.Position.CORE
    converter.vertex_to_proto(a, proto.subgraph.vertices.add())
    edge = proto.subgraph.edges.add()
    converter.vertex_to_proto(a, getattr(edge, "from"))
    edge.label = b"e"
    converter.vertex_to_proto(1, edge.to)
    converter.vertex_to_proto(a, proto.subgraph.refs["s"])

    observation = converter.observation_from_proto(proto)
    assert observation.type == ObservationType.ON_CREATE
    assert observation.subgraph == Subgraph(
        start_position=Position(a, "."),
        end_positions=frozenset({Position(1, ".")}),
        vertices=frozenset({a}),
        edges=frozenset({Edge(a, "e", 1)}),
        refs=Refs({"s": a}),
    )
