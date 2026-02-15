from enum import Enum
from typing import TypeVar, Any, get_args

from frozendict import frozendict as fd

try:
    from pydantic import GetCoreSchemaHandler
    from pydantic_core import CoreSchema
    from pydantic_core.core_schema import (
        dict_schema,
        is_instance_schema,
        no_info_after_validator_function,
        str_schema,
    )
except ImportError:
    # No pydantic!
    GetCoreSchemaHandler = None  # type: ignore[assignment,misc]

K = TypeVar("K")
V = TypeVar("V")


class frozendict(fd[K, V]):

    if GetCoreSchemaHandler:  # type: ignore[truthy-function]

        @classmethod
        def __get_pydantic_core_schema__(
            cls, source: Any, handler: GetCoreSchemaHandler
        ) -> CoreSchema:
            args = get_args(source)
            if not args or len(args) != 2:
                return is_instance_schema(cls)
            key_type, value_type = args
            value_schema = handler.generate_schema(value_type)
            schema = dict_schema(keys_schema=str_schema(), values_schema=value_schema)

            def convert_keys(kv: dict) -> "frozendict":
                if isinstance(key_type, type) and issubclass(key_type, Enum):
                    return cls({key_type(k): v for k, v in kv.items()})
                return cls(kv)

            return no_info_after_validator_function(convert_keys, schema)

    def set(self, key, val):
        new_self = dict(self)
        new_self[key] = val
        return self.__class__(new_self)

    def setdefault(self, key, default=None):
        if key in self:
            return self
        new_self = dict(self)
        new_self[key] = default
        return self.__class__(new_self)

    def discard(self, *keys):
        new_self = dict(self)
        for key in keys:
            new_self.pop(key, None)
        if new_self:
            return self.__class__(new_self)
        return self.__class__()

    def delete(self, *keys):
        new_self = dict(self)
        for key in keys:
            del new_self[key]
        if new_self:
            return self.__class__(new_self)
        return self.__class__()
