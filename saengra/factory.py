from typing import Any, Generic, Type, TypeVar

from saengra.entity import Entity
from saengra.environment import Environment

T = TypeVar("T", bound=Entity)


class EntityFactory(Generic[T]):
    def __init__(
        self,
        entity_type: Type[T],
        env: Environment,
        *,
        defaults: dict[str, Any] | None = None,
        **kwargs,
    ) -> None:
        self._entity_type = entity_type
        self._env = env
        self._defaults = defaults
        self._kwargs = kwargs

    def get(self) -> T:
        return self._entity_type.get(
            env=self._env, _Entity__allow_extra_kwargs=True, **self._kwargs
        )

    def get_or_none(self) -> T | None:
        return self._entity_type.get_or_none(
            env=self._env, _Entity__allow_extra_kwargs=True, **self._kwargs
        )

    def get_or_create(self) -> T:
        return self._entity_type.get_or_create(
            env=self._env, defaults=self._defaults, **self._kwargs
        )

    def create(self) -> T:
        return self._entity_type.create(env=self._env, **self._kwargs)

    def discard(self) -> None:
        if e := self.get_or_none():
            e.discard()
