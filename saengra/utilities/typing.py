import sys
from typing import Any, TypeAlias

Module: TypeAlias = type(sys)  # type: ignore[valid-type]

Entity: type = None  # type: ignore[assignment]
# This is filled later in entity.py.
# Used to avoid circular imports.
# And this is way faster than doing local import in every `is_entity()` call (~2x speedup).


def is_entity(obj: object) -> bool:
    # return isinstance(obj, Entity)
    return type(obj).__mro__[1] is Entity
    # This optimization actually saves 50% of time spent on this function calls
    # (and there are normally LOTS of calls, up to 1% of total runtime).


def check_alive(k: str, v: Any) -> Any:
    if is_entity(v) and not v.alive:
        raise RuntimeError(f"trying to assign `.{k}` to {v} that is not alive")
    return v
