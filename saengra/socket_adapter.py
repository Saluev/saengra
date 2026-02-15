from typing import Iterable

from saengra.adapter import ShouldCommitAgain
from saengra.api import Observation, CommitStatus, Observer
from saengra.client import SaengraClient
from saengra.graph import Update, Primitive, Edge, Subgraph


class SocketAdapter:
    """
    SocketAdapter is the layer of abstraction between Python code and saengra-server commands.

    It communicates with saengra-server via a socket connection.
    It also maintains cache of incoming updates and flushes them when necessary.
    """

    def __init__(self, client: SaengraClient) -> None:
        self._client = client
        self._pending_updates: list[Update] = []

    def update(
        self, update_or_updates: Update | list[Update] | tuple[Update, ...]
    ) -> None:
        if isinstance(update_or_updates, (list, tuple)):
            self._pending_updates.extend(update_or_updates)
        else:
            self._pending_updates.append(update_or_updates)

    def flush(self) -> None:
        if not self._pending_updates:
            return
        self._client.apply_updates(self._pending_updates)
        self._pending_updates.clear()

    def commit(self) -> tuple[ShouldCommitAgain, list[Observation]]:
        self.flush()
        result = self._client.commit()
        return (
            result.status == CommitStatus.REQUIRES_REPEATED_COMMIT,
            result.observations,
        )

    def rollback(self) -> None:
        self._client.rollback()
        self._pending_updates.clear()

    def find_vertices(self, *, with_type_name: str | None = None) -> list[Primitive]:
        # Frequently needed in application-level code.
        self.flush()
        result = self._client.find(vertices=True, with_type_name=with_type_name)
        return result.vertices

    def find_edges(
        self,
        *,
        with_label: str | None = None,
        from_vertices: list[Primitive] | tuple[Primitive, ...] | None = None,
        to_vertices: list[Primitive] | tuple[Primitive, ...] | None = None,
    ) -> list[Edge]:
        self.flush()
        result = self._client.find(
            edges=True,
            with_label=with_label,
            from_vertices=from_vertices,
            to_vertices=to_vertices,
        )
        return result.edges

    def find_all(self) -> tuple[list[Primitive], list[Edge]]:
        # Needed to dump entire graph state.
        self.flush()
        result = self._client.find(vertices=True, edges=True)
        return result.vertices, result.edges

    def observe(self, observers: Iterable[Observer]) -> None:
        self.flush()
        self._client.observe(observers)

    def match(self, expression: str, *placeholder_values: Primitive) -> list[Subgraph]:
        self.flush()
        result = self._client.match(expression, placeholder_values)
        return result.subgraphs
