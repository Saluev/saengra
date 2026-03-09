from typing import Iterable, Protocol, TypeAlias

from saengra.api import Observation, Observer
from saengra.graph import Edge, Primitive, Subgraph, Update

ShouldCommitAgain: TypeAlias = bool


class Adapter(Protocol):
    """Protocol for adapters that communicate with the saengra graph engine."""

    def update(
        self, update_or_updates: Update | list[Update] | tuple[Update, ...]
    ) -> None: ...

    def flush(self) -> None: ...

    def commit(self) -> tuple[ShouldCommitAgain, list[Observation]]: ...

    def rollback(self) -> None: ...

    def find_vertices(
        self, *, with_type_name: str | None = None
    ) -> list[Primitive]: ...

    def find_edges(
        self,
        *,
        with_label: str | None = None,
        from_vertices: list[Primitive] | tuple[Primitive, ...] | None = None,
        to_vertices: list[Primitive] | tuple[Primitive, ...] | None = None,
    ) -> list[Edge]: ...

    def find_all(self) -> tuple[list[Primitive], list[Edge]]: ...

    def observe(self, observers: Iterable[Observer]) -> None: ...

    def match(
        self, expression: str, *placeholder_values: Primitive
    ) -> list[Subgraph]: ...
