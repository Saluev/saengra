import pickle
from weakref import WeakKeyDictionary

from frozendict import frozendict

from saengra import messages_pb2
from saengra.api import (
    Edge,
    Observer,
    Observation,
    ObservationType,
    CommitResponse,
    CommitStatus,
    FindResponse,
    MatchResponse,
)
from saengra.graph import (
    Primitive,
    Update,
    AddVertex,
    AddEdge,
    RemoveVertex,
    RemoveEdge,
    RemoveEdgesToAll,
    Position,
    Subgraph,
    Refs,
)

ApplyUpdates_Update_UpdateKind = messages_pb2.ApplyUpdates.Update.UpdateKind


class Converter:
    def __init__(self):
        self._pickle_cache: dict[Primitive, bytes] = {}

    def vertex_to_proto(self, vertex: Primitive, proto: messages_pb2.Vertex) -> None:
        # The eternal cache is present because it's very hard to normalize
        # Python objects in such a way that x == y is equivalent to
        # pickle.dumps(x) == pickle.dumps(y). Notable examples:
        # * integral floats should be normalized to ints
        # * frozendicts should be sorted by keys
        # * all of the above applies to nested elements in dataclasses, frozensets, frozendicts, etc.
        key = type(vertex), vertex
        if (value := self._pickle_cache.get(key)) is None:
            value = self._pickle_cache[key] = pickle.dumps(vertex)

        proto.type_name = type(vertex).__name__.encode("ascii")
        proto.value = value

    def vertex_from_proto(self, proto: messages_pb2.Vertex) -> Primitive:
        return pickle.loads(proto.value)

    def edge_from_proto(self, proto: messages_pb2.Edge) -> Edge:
        return Edge(
            from_=self.vertex_from_proto(getattr(proto, "from")),
            label=proto.label.decode("utf-8"),
            to=self.vertex_from_proto(proto.to),
        )

    def update_to_proto(
        self, update: Update, proto: messages_pb2.ApplyUpdates.Update
    ) -> None:
        match update:
            case AddVertex(primitive=v):
                proto.kind = ApplyUpdates_Update_UpdateKind.ADD_VERTEX
                self.vertex_to_proto(v, getattr(proto, "from"))
            case AddEdge(from_=from_, label=label, to=to):
                proto.kind = ApplyUpdates_Update_UpdateKind.ADD_EDGE
                self.vertex_to_proto(from_, getattr(proto, "from"))
                proto.label = label
                self.vertex_to_proto(to, proto.to)
            case RemoveVertex(primitive=v):
                proto.kind = ApplyUpdates_Update_UpdateKind.REMOVE_VERTEX
                self.vertex_to_proto(v, getattr(proto, "from"))
            case RemoveEdge(from_=from_, label=label, to=to):
                proto.kind = ApplyUpdates_Update_UpdateKind.REMOVE_EDGE
                self.vertex_to_proto(from_, getattr(proto, "from"))
                proto.label = label
                self.vertex_to_proto(to, proto.to)
            case RemoveEdgesToAll(from_=v, label=label):
                proto.kind = ApplyUpdates_Update_UpdateKind.REMOVE_EDGES_TO_ALL
                self.vertex_to_proto(v, getattr(proto, "from"))
                proto.label = label
            case _:
                raise TypeError(f"not an update: {update}")

    def observer_to_proto(
        self, observer: Observer, proto: messages_pb2.Observe.Observer
    ) -> None:
        proto.id = observer.id
        proto.expression = observer.expression
        for v in observer.placeholder_values:
            self.vertex_to_proto(v, proto.placeholder_values.add())
        proto.on_create = observer.on_create
        proto.on_change = observer.on_change
        proto.on_delete = observer.on_delete

    def observer_from_proto(self, proto: messages_pb2.Observe.Observer) -> Observer:
        return Observer(
            id=proto.id,
            expression=proto.expression,
            placeholder_values=tuple(
                self.vertex_from_proto(v) for v in proto.placeholder_values
            ),
            on_create=proto.on_create,
            on_change=proto.on_change,
            on_delete=proto.on_delete,
        )

    def observation_from_proto(
        self,
        proto: messages_pb2.CommitResponse.Observation,
    ) -> Observation:
        return Observation(
            observer_id=proto.observer_id,
            type=ObservationType(proto.type),
            variables={
                k: self.vertex_from_proto(v) for k, v in proto.variables.items()
            },
        )

    def commit_response_from_proto(
        self, proto: messages_pb2.CommitResponse
    ) -> CommitResponse:
        return CommitResponse(
            status=CommitStatus(proto.status),
            observations=[
                self.observation_from_proto(obs) for obs in proto.observation
            ],
        )

    def find_response_from_proto(
        self, proto: messages_pb2.FindResponse
    ) -> FindResponse:
        return FindResponse(
            vertices=[self.vertex_from_proto(v) for v in proto.vertices],
            edges=[self.edge_from_proto(e) for e in proto.edges],
        )

    def position_from_proto(self, proto: messages_pb2.Position) -> Position:
        point = "." if proto.kind == messages_pb2.Position.CORE else "o"
        return Position(
            vertex=self.vertex_from_proto(proto.vertex),
            point=point,
        )

    def subgraph_from_proto(self, proto: messages_pb2.Subgraph) -> Subgraph:
        return Subgraph(
            start_position=self.position_from_proto(proto.start_position),
            end_positions=frozenset(
                self.position_from_proto(p) for p in proto.end_positions
            ),
            vertices=frozenset(self.vertex_from_proto(v) for v in proto.vertices),
            edges=frozenset(self.edge_from_proto(e) for e in proto.edges),
            refs=Refs({k: self.vertex_from_proto(v) for k, v in proto.refs.items()}),
        )

    def match_response_from_proto(
        self, proto: messages_pb2.MatchResponse
    ) -> MatchResponse:
        return MatchResponse(
            subgraphs=[self.subgraph_from_proto(sg) for sg in proto.subgraphs],
        )
