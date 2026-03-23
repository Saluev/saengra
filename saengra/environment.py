from functools import cache
from time import perf_counter
from typing import Iterable, Type, TypeVar
from weakref import WeakValueDictionary

from saengra.adapter import Adapter
from saengra.api import Observation
from saengra.c_extension import DirectAdapter
from saengra.client import SaengraClient
from saengra.entity import Entity, EnvProtocol
from saengra.graph import AddVertex, Edge, Primitive, RemoveVertex, Update
from saengra.observer import Observer, RefsHandler
from saengra.socket_adapter import SocketAdapter
from saengra.utilities.colors import light_green
from saengra.utilities.itertools import group_by
from saengra.utilities.loggers import LazyRefsFormatter, logger

T = TypeVar("T")


class Environment(EnvProtocol):
    __slots__ = (
        "_primitive_to_entity",
        "_currently_committing",
        "_adapter",
        "_observers",
        "_entities",
        "_temporary_entities",
    )

    def __init__(
        self,
        *,
        client: SaengraClient | None = None,
        adapter: Adapter | None = None,
        entity_types: Iterable[Type[Entity]] | None = None,
    ) -> None:
        entity_types = [] if entity_types is None else [*entity_types]
        self._primitive_to_entity = {
            entity_type.primitive_type: entity_type for entity_type in entity_types
        }
        self._currently_committing = False

        if adapter is not None:
            self._adapter = adapter
        elif client is not None:
            self._adapter = SocketAdapter(client=client)
        else:
            self._adapter = DirectAdapter()
        # self._profiler = Profiler()
        self._observers: dict[str, Observer] = {}

        self._entities: dict[Primitive, Entity] = {}
        self._temporary_entities: WeakValueDictionary[Primitive, Entity] = (
            WeakValueDictionary()
        )
        """ 
        Temporary entities are entities that refer to primitives that are not in the graph.
        E.g. in the following code

              e = MyEntity(graph=g, ...)
              if not e.alive:
                  env.add(e)
                  ...

        a temporary entity will be created in the line 1 which will be converted to a normal 
        entity in the line 3.
        """

    def add(self, *entities: Entity) -> None:
        updates: list[Update] = []
        for entity in entities:
            if entity.alive:
                continue
            updates.append(AddVertex(entity.primitive))
            entity._after_add()
            self._entities[entity.primitive] = entity
            del self._temporary_entities[entity.primitive]
        self._adapter.update(updates)

    def remove(self, *entities: Entity) -> None:
        updates: list[Update] = []
        for entity in entities:
            if not entity.alive:
                continue
            updates.append(RemoveVertex(entity.primitive))
            entity._after_remove()
            del self._entities[entity.primitive]
            self._temporary_entities[entity.primitive] = entity
        self._adapter.update(updates)

    def update(self, *updates: Update) -> None:
        """
        Apply low-level updates to the graph manually.

        Not compatible with Entity manipulation, so use at own risk.
        """
        self._adapter.update(updates)

    def update_many(self, updates: list[Update]) -> None:
        self._adapter.update(updates)

    def commit(self) -> None:
        if self._currently_committing:
            raise RuntimeError("should not invoke .commit() in pre-commit hooks")
        try:
            self._currently_committing = True
            self._do_commit()
        except Exception:
            self.rollback()
            raise
        finally:
            self._currently_committing = False

    def rollback(self) -> None:
        self._adapter.rollback()
        self.sync()

    def sync(self) -> None:
        """Synchronize entities with current graph state, probably after rollback."""
        self._entities.clear()
        self._temporary_entities.clear()

        vertices, edges = self._adapter.find_all()

        # Create entity objects
        for vertex in vertices:
            entity = self._wrap_into_new_entity(vertex)
            if isinstance(entity, Entity):
                entity._after_add()
                self._entities[entity.primitive] = entity
                del self._temporary_entities[entity.primitive]

        # Fill entity relations
        vertex_to_edges = group_by(edges, key=lambda e: e.from_)
        for entity in self._entities.values():
            edges = vertex_to_edges.get(entity.primitive, [])
            labels_and_tos = [
                (edge.label, self._wrap_into_existing_entity(edge.to)) for edge in edges
            ]
            entity._sync(edges=labels_and_tos)

    def find_all(self) -> tuple[list[Primitive], list[Edge]]:
        return self._adapter.find_all()

    def find_edges(
        self,
        *,
        with_label: str | None = None,
        to: Primitive | Entity | list[Primitive | Entity] | None = None,
    ) -> list[Edge]:
        container = [] if to is None else to if isinstance(to, (list, tuple)) else [to]
        edges = self._adapter.find_edges(
            with_label=with_label, to_vertices=[_unwrap_primitive(v) for v in container]
        )
        return edges

    def find_all_of_type(self, type_: Type[T]) -> set[T]:
        primitive_type = _get_primitive_type(type_)
        vertices = self._adapter.find_vertices(with_type_name=primitive_type.__name__)
        return {
            self._wrap_into_existing_entity(v)
            for v in vertices
            if isinstance(v, primitive_type)
        }

    def match(
        self, expression: str, *placeholder_values: Primitive
    ) -> list[dict[str, Primitive | Entity]]:
        subgraphs = self._adapter.match(expression, *placeholder_values)
        return [
            {k: self._wrap_into_existing_entity(v) for k, v in sg.refs.items()}
            for sg in subgraphs
        ]

    def register_observers(self, observers: list[Observer]) -> None:
        new_observers: dict[str, Observer] = {}
        for w in observers:
            if w.id in self._observers:
                raise RuntimeError(
                    f"duplicate observer ID: {w.id}\n\n"
                    "To avoid this error, register observers all at once, or make an extra ID deduplication step"
                )
            new_observers[w.id] = w
        self._observers.update(new_observers)
        self._adapter.observe(w.to_api() for w in observers)

    def _get_living_entity(self, primitive: Primitive) -> Entity | None:
        return self._entities.get(primitive, None)

    def _get_temporary_entity(self, primitive: Primitive) -> Entity | None:
        return self._temporary_entities.get(primitive, None)

    def _register_temporary_entity(
        self, primitive: Primitive, entity: "Entity"
    ) -> None:
        self._temporary_entities[primitive] = entity

    def _do_commit(self) -> None:
        before = perf_counter()
        should_commit_again = True
        num_iterations = 0
        while should_commit_again:
            should_commit_again, observations = self._adapter.commit()
            for o in observations:
                self._invoke_handlers(o)

            num_iterations += 1
            if should_commit_again and num_iterations > 100:
                raise RuntimeError("stuck in pre-commit loop")
        after = perf_counter()
        logger.info(
            f"Commit took {num_iterations} iterations, {after - before:.3f} seconds"
        )

    def _invoke_handlers(self, o: Observation) -> None:
        observer = self._observers[o.observer_id]
        handlers = observer.pick_handlers(o.type)
        for handler in handlers:
            self._invoke_handler(handler, o.variables)

    def _invoke_handler(self, handler: RefsHandler, refs: dict[str, Primitive]) -> None:
        logger.debug(
            f"Invoking {light_green(handler.__name__)}(%s)", LazyRefsFormatter(refs)
        )
        enriched_refs: dict[str, Primitive | Entity | Environment] = {}
        if env_arg := _get_env_arg(handler):
            enriched_refs[env_arg] = self
        for ref, primitive in refs.items():
            if entity_type := self._primitive_to_entity.get(type(primitive)):
                enriched_refs[ref] = entity_type(self, primitive)
            else:
                enriched_refs[ref] = primitive
        handler(**enriched_refs)

    def _wrap_into_new_entity(self, v: Primitive) -> Primitive | Entity:
        if type_ := self._primitive_to_entity.get(type(v)):
            return type_(env=self, primitive=v)
        return v

    def _wrap_into_existing_entity(self, v: Primitive) -> Primitive | Entity:
        if type_ := self._primitive_to_entity.get(type(v)):
            # Corresponding entity is supposed to exist.
            return self._entities[v]
        return v


def _unwrap_primitive(v: Primitive | Entity) -> Primitive:
    if isinstance(v, Entity):
        return v.primitive
    return v


def _get_primitive_type(t: Type) -> Type:
    if issubclass(t, Entity):
        return t.primitive_type
    return t


@cache
def _get_env_arg(handler: RefsHandler) -> str | None:
    for key, value in handler.__annotations__.items():
        if value is Environment:
            return key
    return None
