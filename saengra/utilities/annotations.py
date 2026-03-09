import typing
from dataclasses import dataclass
from functools import reduce
from types import GenericAlias, UnionType
from typing import _UnionGenericAlias  # type: ignore
from typing import Any, ForwardRef

from _operator import or_

from saengra.utilities.typing import Module


@dataclass(frozen=True, slots=True)
class LookaheadType:
    module: Any
    type_: str

    def resolve(self) -> type:
        return getattr(self.module, self.type_)


@dataclass
class UnionAnnotation:
    types: tuple[type, ...]


@dataclass
class CollectionAnnotation:
    collection_type: type | UnionAnnotation
    item_type: type | LookaheadType


@dataclass
class OptionalAnnotation:
    type_: type | UnionAnnotation | LookaheadType


@dataclass
class ScalarAnnotation:
    type_: type | UnionAnnotation | LookaheadType


ParsedAnnotation = CollectionAnnotation | OptionalAnnotation | ScalarAnnotation


class Types:
    """This namespace makes it more convenient to write match-case statements involving types."""

    NoneType = type(None)
    frozenset = frozenset
    list = list
    set = set
    tuple = tuple


def parse_annotation(module: Module, annotation: Any) -> ParsedAnnotation:
    match annotation:
        case GenericAlias(
            __origin__=Types.list | Types.set as collection_type,
            __args__=(item_type,),
        ):
            return CollectionAnnotation(
                collection_type=collection_type,
                item_type=_parse_scalar_annotation(module, item_type),
            )

        case _UnionGenericAlias(
            __origin__=typing.Union, __args__=(item_type, Types.NoneType)  # type: ignore
        ):
            return OptionalAnnotation(type_=_parse_scalar_annotation(module, item_type))

        case UnionType(__args__=(item_type, Types.NoneType)):
            # Foo | None
            return OptionalAnnotation(type_=_parse_scalar_annotation(module, item_type))

        case UnionType(__args__=(*item_types, Types.NoneType)):
            # Foo | Bar | None
            union_without_none = reduce(or_, item_types)
            return OptionalAnnotation(
                type_=_parse_scalar_annotation(module, union_without_none)
            )

        case UnionType(__args__=item_types):
            # Foo | Bar
            return ScalarAnnotation(type_=UnionAnnotation(types=item_types))

        case ForwardRef(__forward_arg__=a):
            return ScalarAnnotation(type_=_parse_scalar_annotation(module, a))

        case item_type:
            return ScalarAnnotation(type_=_parse_scalar_annotation(module, item_type))


def _parse_scalar_annotation(
    module: Module, annotation: Any
) -> type | UnionAnnotation | LookaheadType:
    if isinstance(annotation, str):
        return LookaheadType(module=module, type_=annotation)
    if isinstance(annotation, ForwardRef):
        return LookaheadType(module=module, type_=annotation.__forward_arg__)
    if isinstance(annotation, UnionType):
        return UnionAnnotation(types=annotation.__args__)
    return annotation
