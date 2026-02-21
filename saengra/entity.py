import sys
import typing
from dataclasses import dataclass
from inspect import getmodule
from typing import (
    Any,
    Protocol,
    Self,
    Type,
    TypeAlias,
)

from saengra.graph import Primitive, Label, Update
from saengra.utilities.typing import check_alive


class EnvProtocol(Protocol):
    def _get_living_entity(self, primitive: Primitive) -> "Entity | None":
        pass

    def _get_temporary_entity(self, primitive: Primitive) -> "Entity | None":
        pass

    def _register_temporary_entity(
        self, primitive: Primitive, entity: "Entity"
    ) -> None:
        pass

    def add(self, *entities: "Entity") -> None:
        pass

    def remove(self, *entities: "Entity") -> None:
        pass

    def update(self, *updates: Update) -> None:
        pass


@typing.dataclass_transform(eq_default=True, order_default=True, frozen_default=True)
def primitive(cls: Type) -> Type:
    result = dataclass(frozen=True, slots=True, order=True)(cls)
    setattr(result, PRIMITIVE_MARKER_ATTR_NAME, True)
    return result


def _find_primitive_ancestor_type(name: str, mro: tuple[type, ...]) -> Type[Primitive]:
    primitives = [t for t in mro if not issubclass(t, Entity) and _is_primitive_type(t)]
    if not primitives:
        raise ValueError(f"{name}: no ancestor marked as primitive")
    if len(primitives) > 1:
        raise ValueError(
            f"{name}: too many ancestors marked as primitives: {primitives}"
        )
    return primitives[0]


def _is_primitive_type(t: type) -> bool:
    return getattr(t, PRIMITIVE_MARKER_ATTR_NAME, False)


InverseRefs: TypeAlias = set[tuple["Entity", Label]]


class EntityMeta(type):
    def __new__(mcls, name: str, bases: tuple[type, ...], ns: dict[str, Any]) -> type:
        from saengra.utilities.annotations import parse_annotation
        from saengra.utilities.properties import related_entity_property
        from saengra.utilities.properties import EntityProperty
        from saengra.utilities.properties import (
            PrimitiveProperty,
            RelatedOptionalEntityProperty,
        )

        module = sys.modules[ns["__module__"]]
        annotations = ns.get("__annotations__", {})

        if name == "Entity" and module == getmodule(mcls):
            return super().__new__(mcls, name, bases, ns)

        primitive_type = _find_primitive_ancestor_type(name, bases)
        properties: list[EntityProperty] = []
        for attr_name in primitive_type.__annotations__:
            ns[attr_name] = property_ = PrimitiveProperty(attr_name)
            properties.append(property_)

        for attr_name, annotation in [*annotations.items()]:
            label = Label(attr_name)
            parsed_annotation = parse_annotation(module, annotation)
            property_ = related_entity_property(label, parsed_annotation)
            ns[attr_name] = property_
            properties.append(property_)

        init_cache_lines = []
        init_code_locals = {}
        slots: list[str] = [
            "__weakref__",
            "_cache_initialized",
            "_env",
            "_inverse",
            "alive",
            "primitive",
        ]
        for property_ in properties:
            extra_lines, extra_locals = property_.compose_init_code()
            init_cache_lines.extend(extra_lines)
            init_code_locals.update(extra_locals)
            if isinstance(property_, RelatedOptionalEntityProperty):
                if "__dict__" not in slots:
                    slots.append("__dict__")
            else:
                slots.extend(property_.compose_slots())

        ns["__slots__"] = tuple(slots)

        if "__init__" in ns:
            raise NotImplementedError("custom __init__ is not supported yet")

        init_code_lines = [
            "self._env = env",
            "if primitive is None:",
            "    primitive, _, kwargs = self._construct_primitive(**kwargs)",
            "self.primitive = primitive",
            "if not self._cache_initialized:",
            "    cls = type(self)",
            *[f"    {line}" for line in init_cache_lines],
            "    self._cache_initialized = True",
            "for k, v in kwargs.items():",
            "    setattr(self, k, v)",
        ]
        init_code = (
            "def __init__(self, env, primitive = None, **kwargs) -> None:\n    "
            + "\n    ".join(init_code_lines)
        )
        exec(init_code, init_code_locals, ns)

        result = super().__new__(mcls, name, bases, ns)

        if result.__mro__[1] is not Entity:
            raise RuntimeError(
                "Entity must be first base class, inheriting Entity subclasses is not supported"
            )
            # We assume that Entity is the first base class to optimize `is_entity()`.

        return result


class Entity(metaclass=EntityMeta):
    """
    Sample usage:

        @primitive
        class player:
            id: UUID

        class Player(Entity, player):
            name: str
            allies: set["Player"]

        primitive = Player.primitive_type(id=uuid4())
        graph.update(AddVertex(primitive))
        player = Player(graph=graph, primitive=primitive)
    """

    __slots__ = ()

    # Class attributes:
    primitive_type: Type[Primitive]

    # Instance attributes:
    primitive: Primitive
    alive: bool
    _env: EnvProtocol
    _inverse: InverseRefs

    def __init_subclass__(cls) -> None:
        from saengra.utilities.annotations import parse_annotation
        from saengra.utilities.properties import related_entity_property
        from saengra.utilities.properties import PrimitiveProperty

        super().__init_subclass__()

        module = getmodule(cls)

        cls.primitive_type = _find_primitive_ancestor_type(cls.__name__, cls.__mro__)

        for attr_name in cls.primitive_type.__annotations__:
            setattr(cls, attr_name, PrimitiveProperty(attr_name))

        for attr_name, annotation in cls.__annotations__.items():
            label = Label(attr_name)
            parsed_annotation = parse_annotation(module, annotation)
            property_ = related_entity_property(label, parsed_annotation)
            setattr(cls, attr_name, property_)

    ####################################################################################################################

    def __new__(
        cls, env: EnvProtocol, primitive: Primitive | None = None, **kwargs
    ) -> "Entity":
        if primitive is None:
            primitive, _, kwargs = cls._construct_primitive(**kwargs)
        # noinspection PyProtectedMember
        if e := env._get_living_entity(primitive):
            return e
        # noinspection PyProtectedMember
        if e := env._get_temporary_entity(primitive):
            if kwargs:
                raise ValueError(
                    f"can't set attributes — {primitive} doesn't exist yet"
                )
            return e

        e = object.__new__(cls)
        e._inverse = set()
        e._cache_initialized = False
        e.alive = False
        # noinspection PyProtectedMember
        env._register_temporary_entity(primitive, e)
        return e

    @classmethod
    def _construct_primitive(
        cls, *args: Any, **kwargs: Any
    ) -> tuple[Primitive, tuple[Any, ...], dict[str, Any]]:
        total_expected_args = len(cls.primitive_type.__annotations__)
        matching_kwargs = {
            k: v for k, v in kwargs.items() if k in cls.primitive_type.__annotations__
        }
        matching_args = args[: total_expected_args - len(matching_kwargs)]
        primitive = cls.primitive_type(*matching_args, **matching_kwargs)
        remaining_args = args[len(matching_args) :]
        remaining_kwargs = {
            k: v
            for k, v in kwargs.items()
            if k not in cls.primitive_type.__annotations__
        }
        return primitive, remaining_args, remaining_kwargs

    @classmethod
    def _init_cache(cls, env: EnvProtocol, e: "Entity", primitive: Primitive) -> None:
        for attr_name in cls.__annotations__:
            prop = getattr(cls, attr_name)
            prop.init_cache(env, e, primitive)

    ####################################################################################################################

    # Overriding mutation forbidding methods introduced by `@primitive`.
    __setattr__ = object.__setattr__
    __delattr__ = object.__delattr__

    def __hash__(self) -> int:
        return hash(self.primitive)

    def __lt__(self, other: Any) -> bool:
        match other:
            case Entity():
                return self.primitive < other.primitive  # type: ignore
        return NotImplemented

    def __eq__(self, other: Any) -> bool:
        if isinstance(other, Entity):
            other = other.primitive
        return self.primitive == other

    def __repr__(self) -> str:
        result = repr(self.primitive)
        prefix = self.primitive_type.__name__
        if result.startswith(prefix):
            return type(self).__name__ + result.removeprefix(prefix)
        return f"{type(self).__name__}({result})"

    ####################################################################################################################

    @classmethod
    def get(
        cls: Type[Self],
        env: EnvProtocol,
        primitive: Primitive | None = None,
        *,
        __allow_extra_kwargs: bool = False,
        **kwargs: object,
    ) -> Self:
        if primitive is None:
            primitive, _, kwargs = cls._construct_primitive(**kwargs)
        if kwargs and not __allow_extra_kwargs:
            raise ValueError("not supposed to add edges in .get()")
        # noinspection PyProtectedMember
        if e := env._get_living_entity(primitive):
            return e
        raise ValueError(f"{primitive} doesn't exist yet")

    @classmethod
    def get_or_none(
        cls: Type[Self],
        env: EnvProtocol,
        primitive: Primitive | None = None,
        *,
        __allow_extra_kwargs: bool = False,
        **kwargs,
    ) -> Self | None:
        if primitive is None:
            primitive, _, kwargs = cls._construct_primitive(**kwargs)
        if kwargs and not __allow_extra_kwargs:
            raise ValueError("not supposed to add edges in .get_or_none()")
        # noinspection PyProtectedMember
        if e := env._get_living_entity(primitive):
            return e
        return None

    @classmethod
    def get_or_create(
        cls: Type[Self],
        env: EnvProtocol,
        primitive: Primitive | None = None,
        *,
        defaults: dict[str, Any] | None = None,
        **kwargs,
    ) -> Self:
        if primitive is None:
            primitive, _, kwargs = cls._construct_primitive(**kwargs)
        result = cls(env=env, primitive=primitive)
        just_created = False
        if not result.alive:
            just_created = True
            env.add(result)

        for k, v in kwargs.items():
            if just_created and v is None:
                continue
            setattr(result, k, check_alive(k, v))

        if just_created and defaults:
            for k, v in defaults.items():
                if v is not None:
                    setattr(result, k, check_alive(k, v))

        return result

    @classmethod
    def create(
        cls: Type[Self], env: EnvProtocol, primitive: Primitive | None = None, **kwargs
    ) -> Self:
        if primitive is None:
            primitive, _, kwargs = cls._construct_primitive(**kwargs)
        result = cls(env=env, primitive=primitive)
        if result.alive:
            raise ValueError(f"{primitive} already exists")
        env.add(result)

        for k, v in kwargs.items():
            if v is None:
                continue
            setattr(result, k, check_alive(k, v))

        return result

    def update(self, **kwargs) -> Self:
        for k, v in kwargs.items():
            setattr(self, k, check_alive(k, v))
        return self

    def discard(self) -> None:
        if not self.alive:
            return
        self._env.remove(self)

    def remove(self) -> None:
        if not self.alive:
            raise ValueError(f"{self.primitive} doesn't exist")
        self._env.remove(self)

    def _after_add(self) -> None:
        # Invoked by Environment if after the vertex is added.
        self.alive = True

    def _after_remove(self) -> None:
        # Invoked by Environment if after the vertex is removed.
        self.alive = False
        for label in self.__annotations__:
            prop = getattr(type(self), label)
            if isinstance(related_entity := getattr(self, label, None), Entity):
                related_entity._inverse.remove((self, label))
            prop.clear_cache(self)
        for related_entity, label in self._inverse:
            prop = getattr(type(related_entity), label)
            prop.remove_from_cache(related_entity, self)
        self._inverse.clear()

    def _sync(self, edges: list[tuple[Label, "Primitive | Entity"]]) -> None:
        self._init_cache(self._env, self, self.primitive)
        for label, to in edges:
            attr_name = str(label)
            prop = getattr(type(self), attr_name)
            prop.add_to_cache(self, to)


PRIMITIVE_MARKER_ATTR_NAME = "__saengra_primitive__"

import saengra.utilities.typing

saengra.utilities.typing.Entity = Entity
