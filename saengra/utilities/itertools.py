from collections import defaultdict
from typing import Callable, Iterable, TypeVar

T = TypeVar("T")
K = TypeVar("K")


def group_by(what: Iterable[T], key: Callable[[T], K]) -> defaultdict[K, list[T]]:
    result: defaultdict[K, list[T]] = defaultdict(list)
    for item in what:
        item_key = key(item)
        result[item_key].append(item)
    return result
