from dataclasses import dataclass, field
from enum import Enum

from saengra import messages_pb2
from saengra.graph import Primitive, Edge, Subgraph


@dataclass(frozen=True)
class Observer:
    id: str
    expression: str
    placeholder_values: tuple[Primitive, ...] = field(default_factory=tuple)
    on_create: bool = False
    on_change: bool = False
    on_delete: bool = False


class ObservationType(Enum):
    ON_CREATE = messages_pb2.CommitResponse.Observation.ON_CREATE
    ON_CHANGE = messages_pb2.CommitResponse.Observation.ON_CHANGE
    ON_DELETE = messages_pb2.CommitResponse.Observation.ON_DELETE


@dataclass(frozen=True)
class Observation:
    observer_id: str
    type: ObservationType
    variables: dict[str, Primitive]


class CommitStatus(Enum):
    OK = messages_pb2.CommitResponse.OK
    REQUIRES_REPEATED_COMMIT = messages_pb2.CommitResponse.REQUIRES_REPEATED_COMMIT


@dataclass(frozen=True)
class CommitResponse:
    status: CommitStatus
    observations: list[Observation]


@dataclass(frozen=True)
class FindResponse:
    vertices: list[Primitive]
    edges: list[Edge]


@dataclass(frozen=True)
class MatchResponse:
    subgraphs: list[Subgraph]
