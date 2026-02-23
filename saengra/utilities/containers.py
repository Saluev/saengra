from typing import Iterator, Iterable, Any, TYPE_CHECKING

from saengra.utilities.typing import is_entity

if TYPE_CHECKING:
    from saengra.entity import EnvProtocol, Entity

from saengra.graph import (
    Primitive,
    Label,
    AddVertex,
    AddEdge,
    RemoveEdge,
    Update,
    RemoveEdgesToAll,
)


class RelatedEntitiesContainer:
    __slots__ = (
        "_env",
        "_e",
        "_from",
        "_label",
        "_cached_values",
    )

    def __init__(
        self,
        env: "EnvProtocol",
        e: "Entity",
        from_: Primitive,
        label: Label,
    ) -> None:
        self._env = env
        self._e = e
        self._from = from_
        self._label = label
        self._cached_values: set["Primitive | Entity"] = set()

    def assign(self, value: list["Primitive | Entity"]):
        self._clear()
        self._add_many(value)

    def _add_inverse_one(self, item: "Primitive | Entity"):
        if is_entity(item):
            item._inverse.add((self._e, self._label))

    def _add_inverse_many(self, items: list["Primitive | Entity"]):
        for item in items:
            if is_entity(item):
                item._inverse.add((self._e, self._label))

    def _remove_inverse(
        self, items: list["Primitive | Entity"] | set["Primitive | Entity"]
    ):
        for value in items:
            if is_entity(value):
                value._inverse.discard((self._e, self._label))

    def _add(self, item: "Primitive | Entity") -> None:
        if item is None:
            raise ValueError("expected primitive or entity, got None")

        if is_entity(item):
            to = item.primitive
            self._env.add(item)
        else:
            to = item
            self._env.update(AddVertex(to))

        self._env.update(AddEdge(self._from, self._label, to))
        self._add_to_cache(item)

    def _add_to_cache(self, item: "Primitive | Entity") -> None:
        self._cached_values.add(item)
        self._add_inverse_one(item)

    def _add_many(self, items: list["Primitive | Entity"]) -> None:
        tos = [item.primitive if is_entity(item) else item for item in items]
        add_vertex_updates = [AddVertex(to) for to in tos]
        add_edge_updates = [AddEdge(self._from, self._label, to) for to in tos]
        self._env.update(*add_vertex_updates, *add_edge_updates)
        self._cached_values.update(items)
        self._add_inverse_many(items)

    def _discard(self, item: "Primitive | Entity") -> None:
        to = item.primitive if is_entity(item) else item
        self._env.update(RemoveEdge(self._from, self._label, to))
        self._cached_values.discard(item)
        self._remove_inverse([item])

    def _discard_many(self, items: list["Primitive | Entity"]) -> None:
        tos = [item.primitive if is_entity(item) else item for item in items]
        remove_edge_updates: list[Update] = [
            RemoveEdge(self._from, self._label, to) for to in tos
        ]
        self._env.update(*remove_edge_updates)
        self._cached_values.difference_update(items)
        self._remove_inverse(items)

    def _clear(self) -> None:
        self._env.update(RemoveEdgesToAll(self._from, self._label))
        self._remove_inverse(self._cached_values)
        self._cached_values.clear()

    def __len__(self) -> int:
        return len(self._cached_values)

    def __bool__(self) -> bool:
        return bool(self._cached_values)

    def __contains__(self, o: object) -> bool:
        return o in self._cached_values

    def __iter__(self) -> Iterator[Primitive]:
        return iter(self._cached_values)


class RelatedEntitiesSet(RelatedEntitiesContainer):
    def __repr__(self) -> str:
        return repr(self._cached_values)

    def add(self, item: "Primitive | Entity") -> None:
        self._add(item)

    def discard(self, item: "Primitive | Entity") -> None:
        self._discard(item)

    def clear(self) -> None:
        self._clear()

    def update(self, *s: Iterable["Primitive | Entity"]) -> None:
        items = [item for iterable in s for item in iterable]
        self._add_many(items)

    def difference_update(self, *s: Iterable["Primitive | Entity"]) -> None:
        items = [item for iterable in s for item in iterable]
        self._discard_many(items)

    def __eq__(self, other: Any) -> bool:
        match other:
            case set():
                return self._cached_values == other
            case RelatedEntitiesSet():
                return self._cached_values == other._cached_values
        return NotImplemented

    def __and__(self, other):
        return self._cached_values & other

    def __rand__(self, other):
        return other & self._cached_values

    def __or__(self, other):
        return self._cached_values | other

    def __ror__(self, other):
        return other | self._cached_values

    def __sub__(self, other):
        return self._cached_values - other

    def __rsub__(self, other):
        return other - self._cached_values

    def __ge__(self, other):
        return self._cached_values >= other

    def __gt__(self, other):
        return self._cached_values > other

    def __le__(self, other):
        return self._cached_values <= other

    def __lt__(self, other):
        return self._cached_values < other
