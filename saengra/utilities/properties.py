from sys import intern
from typing import Any, assert_never, TYPE_CHECKING

from saengra.utilities.typing import is_entity, check_alive

if TYPE_CHECKING:
    from saengra.entity import EnvProtocol, Entity
from saengra.graph import Label, Primitive, RemoveEdgesToAll, AddVertex, AddEdge
from saengra.utilities.annotations import (
    CollectionAnnotation,
    LookaheadType,
    ScalarAnnotation,
    OptionalAnnotation,
    ParsedAnnotation,
)
from saengra.utilities.containers import RelatedEntitiesSet


class EntityProperty:

    def compose_init_code(self) -> tuple[list[str], dict[str, Any]]:
        """Returns lines of code to add to "Entity".__init__ method and corresponding update to locals() for exec()."""
        return [], {}

    def compose_slots(self) -> list[str]:
        return []

    def init_cache(self, env: "EnvProtocol", e: "Entity", primitive: Primitive) -> None:
        pass

    def clear_cache(self, e: "Entity") -> None:
        pass

    def add_to_cache(self, e: "Entity", what: Any):
        pass

    def remove_from_cache(self, e: "Entity", what: "Entity") -> None:
        pass


class PrimitiveProperty(EntityProperty):
    __slots__ = ("_attr_name",)

    def __init__(self, attr_name: str) -> None:
        self._attr_name = attr_name

    def __get__(self, instance: "Entity | None", owner):
        if instance is None:
            return self

        return object.__getattribute__(instance.primitive, self._attr_name)

    def __delete__(self, instance: "Entity"):
        raise AttributeError(self._attr_name)


class RelatedCollectionProperty(EntityProperty):
    __slots__ = (
        "_label",
        "_cache_attr_name",
        "_item_type_annotation",
        "_proxy_type",
    )

    def __init__(self, label: Label, annotation: CollectionAnnotation) -> None:
        self._label = intern(label)
        self._cache_attr_name = _make_cache_attr_name(label)
        self._item_type_annotation = annotation.item_type
        self._proxy_type = {
            set: RelatedEntitiesSet,
        }[
            annotation.collection_type  # type: ignore
        ]

    def make_collection(self, env: "EnvProtocol", e: "Entity", primitive: Primitive):
        return self._proxy_type(env, e, primitive, self._label)

    def __get__(self, instance: "Entity | None", owner):
        if instance is None:
            return self
        return object.__getattribute__(instance, self._cache_attr_name)

    def __set__(self, instance: "Entity", value: Any) -> None:
        collection = object.__getattribute__(instance, self._cache_attr_name)
        collection.assign([*value])

    def __delete__(self, e: "Entity"):
        raise AttributeError(self._label)

    def compose_init_code(self) -> tuple[list[str], dict[str, Any]]:
        result = [
            f"self.{self._cache_attr_name} = cls.{self._label}.make_collection(self._env, self, self.primitive)"
        ]
        return result, {}

    def compose_slots(self) -> list[str]:
        return [self._cache_attr_name]

    def init_cache(self, env: "EnvProtocol", e: "Entity", primitive: Primitive) -> None:
        collection = self.make_collection(env, e, primitive)
        setattr(e, self._cache_attr_name, collection)

    def clear_cache(self, e: "Entity") -> None:
        collection = object.__getattribute__(e, self._cache_attr_name)
        collection._remove_inverse(collection._cached_values)
        collection._cached_values.clear()

    def add_to_cache(self, e: "Entity", what: Any):
        collection = object.__getattribute__(e, self._cache_attr_name)
        collection._add_to_cache(what)

    def remove_from_cache(self, e: "Entity", what: "Entity") -> None:
        collection = object.__getattribute__(e, self._cache_attr_name)
        collection._cached_values.remove(what)


class RelatedEntityPropertyBase(EntityProperty):
    __slots__ = ("_label", "_cache_attr_name")

    def __init__(
        self, label: Label, annotation: ScalarAnnotation | OptionalAnnotation
    ) -> None:
        self._label = intern(label)
        self._cache_attr_name = _make_cache_attr_name(label)

    def compose_slots(self) -> list[str]:
        return [self._cache_attr_name]


class RelatedOptionalEntityProperty(RelatedEntityPropertyBase):
    __slots__ = RelatedEntityPropertyBase.__slots__

    def __init__(self, label: Label, annotation: OptionalAnnotation) -> None:
        super().__init__(label, annotation)

    def __get__(self, instance: "Entity | None", owner):
        if instance is None:
            return self
        return instance.__dict__.get(self._cache_attr_name, None)

    def __set__(self, instance: "Entity", value: Any) -> None:
        check_alive(self._label, value)
        # Update graph
        from_, label = instance.primitive, self._label
        instance._env.update(RemoveEdgesToAll(from_, label))
        if value is not None:
            to = value.primitive if is_entity(value) else value
            instance._env.update(AddVertex(to), AddEdge(from_, label, to))
        # Update entities
        self.add_to_cache(instance, value)

    def __delete__(self, instance: "Entity"):
        raise AttributeError(self._label)

    def compose_init_code(self) -> tuple[list[str], dict[str, Any]]:
        return [], {}

    def init_cache(self, env: "EnvProtocol", e: "Entity", primitive: Primitive) -> None:
        pass

    def clear_cache(self, e: "Entity") -> None:
        e.__dict__.pop(self._cache_attr_name, None)

    def add_to_cache(self, e: "Entity", what: Any) -> None:
        if is_entity(prev := getattr(e, self._cache_attr_name, None)):
            prev._inverse.remove((e, self._label))
        setattr(e, self._cache_attr_name, what)
        if is_entity(what):
            what._inverse.add((e, self._label))

    def remove_from_cache(self, e: "Entity", what: "Entity") -> None:
        e.__dict__.pop(self._cache_attr_name, None)


class Uninitialized:
    def __str__(self) -> str:
        return "Uninitialized"

    def __repr__(self) -> str:
        return "Uninitialized"


_uninitialized = Uninitialized()


class RelatedEntityProperty(RelatedEntityPropertyBase):
    __slots__ = RelatedEntityPropertyBase.__slots__

    def __init__(self, label: Label, annotation: ScalarAnnotation) -> None:
        super().__init__(label, annotation)

    def __get__(self, instance: "Entity | None", owner):
        if instance is None:
            return self
        try:
            return object.__getattribute__(instance, self._cache_attr_name)
        except AttributeError as exc:
            raise RuntimeError(
                f"{instance!r} doesn't have a linked {self._label!r}"
            ) from exc

    def __set__(self, instance: "Entity", value: Any) -> None:
        check_alive(self._label, value)
        # Update graph
        from_, label = instance.primitive, self._label
        to = value.primitive if is_entity(value) else value
        instance._env.update(
            RemoveEdgesToAll(from_, label), AddVertex(to), AddEdge(from_, label, to)
        )
        # Update entities
        self.add_to_cache(instance, value)

    def __delete__(self, instance: "Entity"):
        raise AttributeError(self._label)

    def compose_init_code(self) -> tuple[list[str], dict[str, Any]]:
        result = [f"self.{self._cache_attr_name} = _uninitialized"]
        return result, {"_uninitialized": _uninitialized}

    def init_cache(self, env: "EnvProtocol", e: "Entity", primitive: Primitive) -> None:
        pass  # don't init cache, this attribute has to be set

    def clear_cache(self, e: "Entity") -> None:
        delattr(e, self._cache_attr_name)

    def add_to_cache(self, e: "Entity", what: Any) -> None:
        if is_entity(prev := getattr(e, self._cache_attr_name, None)):
            prev._inverse.remove((e, self._label))
        setattr(e, self._cache_attr_name, what)
        if is_entity(what):
            what._inverse.add((e, self._label))

    def remove_from_cache(self, e: "Entity", what: "Entity") -> None:
        delattr(e, self._cache_attr_name)


def related_entity_property(
    label: Label, annotation: ParsedAnnotation
) -> EntityProperty:
    match annotation:
        case CollectionAnnotation():
            return RelatedCollectionProperty(label, annotation)

        case OptionalAnnotation():
            return RelatedOptionalEntityProperty(label, annotation)

        case ScalarAnnotation():
            return RelatedEntityProperty(label, annotation)

        case other:
            assert_never(other)


def _make_cache_attr_name(label: Label) -> str:
    return intern(f"_{label}")
