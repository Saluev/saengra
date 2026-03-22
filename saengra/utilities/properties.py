from sys import intern
from typing import TYPE_CHECKING, Any, assert_never

from saengra.utilities.typing import check_alive, is_entity

if TYPE_CHECKING:
    from saengra.entity import EnvProtocol, Entity

from saengra.graph import AddEdge, AddVertex, Label, Primitive, RemoveEdgesToAll
from saengra.utilities.annotations import (
    CollectionAnnotation,
    OptionalAnnotation,
    ParsedAnnotation,
    ScalarAnnotation,
)
from saengra.utilities.containers import RelatedEntitiesSet

_object_getattribute = object.__getattribute__
_object_setattr = object.__setattr__
_object_delattr = object.__delattr__


class EntityProperty:

    def compose_init_code(self) -> tuple[list[str], dict[str, Any]]:
        """Returns lines of code to add to "Entity".__init__ method and corresponding update to locals() for exec()."""
        return [], {}

    def compose_slots(self) -> list[str]:
        """Returns the list of attribute names that are going to be used and have to be added to entity's __slots__."""
        return []

    def reset_cache(
        self, env: "EnvProtocol", e: "Entity", primitive: Primitive
    ) -> None:
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

        return _object_getattribute(instance.primitive, self._attr_name)

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
        return _object_getattribute(instance, self._cache_attr_name)

    def __set__(self, instance: "Entity", value: Any) -> None:
        collection = _object_getattribute(instance, self._cache_attr_name)
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

    def reset_cache(
        self, env: "EnvProtocol", e: "Entity", primitive: Primitive
    ) -> None:
        collection = self.make_collection(env, e, primitive)
        _object_setattr(e, self._cache_attr_name, collection)

    def clear_cache(self, e: "Entity") -> None:
        collection = _object_getattribute(e, self._cache_attr_name)
        collection._remove_inverse(collection._cached_values)
        collection._cached_values.clear()

    def add_to_cache(self, e: "Entity", what: Any):
        collection = _object_getattribute(e, self._cache_attr_name)
        collection._add_to_cache(what)

    def remove_from_cache(self, e: "Entity", what: "Entity") -> None:
        collection = _object_getattribute(e, self._cache_attr_name)
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

    def add_to_cache(self, e: "Entity", what: Any) -> None:
        if is_entity(prev := _object_getattribute(e, self._cache_attr_name)):
            prev._inverse.remove((e, self._label))
        _object_setattr(e, self._cache_attr_name, what)
        if is_entity(what):
            what._inverse.add((e, self._label))


class RelatedOptionalEntityProperty(RelatedEntityPropertyBase):
    __slots__ = RelatedEntityPropertyBase.__slots__

    def __get__(self, instance: "Entity | None", owner):
        if instance is None:
            return self
        return _object_getattribute(instance, self._cache_attr_name)

    def __set__(self, instance: "Entity", value: Any) -> None:
        # Update graph
        from_, label = instance.primitive, self._label
        instance._env.update(RemoveEdgesToAll(from_, label))
        if value is not None:
            if is_entity(value):
                if not value.alive:
                    raise RuntimeError(
                        f"trying to assign `.{self._label}` to {value} that is not alive"
                    )
                instance._env.update(AddEdge(from_, label, value.primitive))
            else:
                instance._env.update(AddVertex(value), AddEdge(from_, label, value))
        # Update entities
        self.add_to_cache(instance, value)

    def __delete__(self, instance: "Entity"):
        raise AttributeError(self._label)

    def compose_init_code(self) -> tuple[list[str], dict[str, Any]]:
        return [f"self.{self._cache_attr_name} = None"], {}

    def reset_cache(
        self, env: "EnvProtocol", e: "Entity", primitive: Primitive
    ) -> None:
        _object_setattr(e, self._cache_attr_name, None)

    def clear_cache(self, e: "Entity") -> None:
        _object_setattr(e, self._cache_attr_name, None)

    def remove_from_cache(self, e: "Entity", what: "Entity") -> None:
        _object_setattr(e, self._cache_attr_name, None)


class Uninitialized:
    def __str__(self) -> str:
        return "Uninitialized"

    def __repr__(self) -> str:
        return "Uninitialized"


_uninitialized = Uninitialized()


class RelatedEntityProperty(RelatedEntityPropertyBase):
    __slots__ = RelatedEntityPropertyBase.__slots__

    def __get__(self, instance: "Entity | None", owner):
        if instance is None:
            return self
        try:
            return _object_getattribute(instance, self._cache_attr_name)
        except AttributeError as exc:
            raise RuntimeError(
                f"{instance!r} doesn't have a linked {self._label!r}"
            ) from exc

    def __set__(self, instance: "Entity", value: Any) -> None:
        # Update graph
        from_, label = instance.primitive, self._label
        instance._env.update(RemoveEdgesToAll(from_, label))
        if is_entity(value):
            if not value.alive:
                raise RuntimeError(
                    f"trying to assign `.{self._label}` to {value} that is not alive"
                )
            instance._env.update(AddEdge(from_, label, value.primitive))
        else:
            instance._env.update(AddVertex(value), AddEdge(from_, label, value))
        # Update entities
        self.add_to_cache(instance, value)

    def __delete__(self, instance: "Entity"):
        raise AttributeError(self._label)

    def compose_init_code(self) -> tuple[list[str], dict[str, Any]]:
        result = [f"self.{self._cache_attr_name} = _uninitialized"]
        return result, {"_uninitialized": _uninitialized}

    def reset_cache(
        self, env: "EnvProtocol", e: "Entity", primitive: Primitive
    ) -> None:
        pass  # don't init cache, this attribute has to be set

    def clear_cache(self, e: "Entity") -> None:
        _object_setattr(e, self._cache_attr_name, _uninitialized)

    def remove_from_cache(self, e: "Entity", what: "Entity") -> None:
        _object_setattr(e, self._cache_attr_name, _uninitialized)


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
