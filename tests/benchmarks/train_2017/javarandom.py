"""
Bit-exact port of `java.util.Random`, so that the generated models and the
transformation candidates match the ones of the reference Java implementation.
"""

from typing import MutableSequence, TypeVar

T = TypeVar("T")

_MULTIPLIER = 0x5DEECE66D
_ADDEND = 0xB
_MASK = (1 << 48) - 1


def _to_int32(value: int) -> int:
    value &= 0xFFFFFFFF
    return value - (1 << 32) if value & 0x80000000 else value


class JavaRandom:
    __slots__ = ("_seed",)

    def __init__(self, seed: int) -> None:
        self._seed = (seed ^ _MULTIPLIER) & _MASK

    def _next(self, bits: int) -> int:
        self._seed = (self._seed * _MULTIPLIER + _ADDEND) & _MASK
        return _to_int32(self._seed >> (48 - bits))

    def next_int(self, bound: int) -> int:
        if bound <= 0:
            raise ValueError("bound must be positive")
        if bound & -bound == bound:
            return (bound * self._next(31)) >> 31
        while True:
            bits = self._next(31)
            value = bits % bound
            if _to_int32(bits - value + (bound - 1)) >= 0:
                return value

    def shuffle(self, items: MutableSequence[T]) -> None:
        """Same as `java.util.Collections.shuffle(list, random)` for random access lists."""
        for i in range(len(items), 1, -1):
            j = self.next_int(i)
            items[i - 1], items[j] = items[j], items[i - 1]
