"""
Shared post-processing: scores and top-3 results, fed by match events from the engines.

Engines differ only in how they find matches; every engine reports matches to the same
scorer, so scoring costs the same everywhere. Change sequences are insert-only, so scores
only grow, and the top 3 can be maintained from the previous top 3 and the changed entities.
"""

from typing import Iterable

from tests.benchmarks.train_2017.workload import Op


class Scorer:
    def __init__(self, entity_type: str) -> None:
        self.entity_type = entity_type
        self.timestamps: dict[int, str] = {}
        self.scores: dict[int, int] = {}
        self.top: list[int] = []
        self.changed: set[int] = set()

    def observe_ops(self, ops: Iterable[Op]) -> None:
        """Registers scored entities (posts or comments) with their timestamps."""
        for op in ops:
            if op[0] == "N" and op[1] == self.entity_type:
                self.scores.setdefault(op[2], 0)
                self.changed.add(op[2])
            elif op[0] == "A" and op[1] == "timestamp" and op[2] in self.scores:
                self.timestamps[op[2]] = op[3]

    def add(self, entity: int, delta: int) -> None:
        self.scores[entity] = self.scores.get(entity, 0) + delta
        self.changed.add(entity)

    def set(self, entity: int, score: int) -> None:
        self.scores[entity] = score
        self.changed.add(entity)

    def result(self) -> str:
        candidates = set(self.top) | self.changed
        self.changed = set()
        self.top = sorted(
            candidates,
            key=lambda e: (self.scores[e], self.timestamps.get(e, ""), e),
            reverse=True,
        )[:3]
        return "|".join(str(e) for e in self.top)


class ComponentsScorer(Scorer):
    """Q2: per comment, sum of squared sizes of friendship components among its likers."""

    def __init__(self) -> None:
        super().__init__("Comment")
        self.parent: dict[tuple[int, int], tuple[int, int]] = {}
        self.size: dict[tuple[int, int], int] = {}

    def _find(self, node: tuple[int, int]) -> tuple[int, int]:
        parent = self.parent
        if node not in parent:
            parent[node] = node
            self.size[node] = 1
            self.add(node[0], 1)
            return node
        while parent[node] != node:
            parent[node] = parent[parent[node]]
            node = parent[node]
        return node

    def liker(self, comment: int, user: int) -> None:
        self._find((comment, user))

    def friends(self, comment: int, u1: int, u2: int) -> None:
        a, b = self._find((comment, u1)), self._find((comment, u2))
        if a == b:
            return
        if self.size[a] < self.size[b]:
            a, b = b, a
        self.parent[b] = a
        # (x + y)² = x² + y² + 2xy
        self.add(comment, 2 * self.size[a] * self.size[b])
        self.size[a] += self.size[b]
