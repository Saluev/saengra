"""
Waltz line labeling benchmark from the OPS5 benchmark suite (University of Texas at Austin, 1991).

The program labels the lines of a 2D drawing as edges of 3D objects: it duplicates every line
into two directed edges, detects junctions (L, arrow, fork, tee) by the angles between their
edges, labels the two outermost junctions as boundaries and then propagates labels
(`B`oundary, `+` convex, `-` concave) through the junctions until no rule applies.

Drawings are generated the same way as the classic waltz12/25/37/50 data sets (verified to
produce the same lines): `n` regions give n + 1 blocks of four identical shapes, connected
by short lines. Points are encoded as x * 100 + y.
"""

import hashlib
import math
from collections import Counter

MOD = 100

Line = tuple[int, int]

# A single shape, with the point (x, y) encoded as x * 100 + y.
_SHAPE = [
    ((0, 0), (200, 0)), ((200, 0), (300, 0)), ((300, 0), (400, 0)),
    ((0, 0), (0, 2)), ((0, 2), (0, 3)), ((0, 3), (0, 4)),
    ((0, 4), (400, 4)), ((400, 4), (400, 0)), ((400, 0), (500, 1)),
    ((500, 1), (500, 2)), ((500, 2), (500, 3)), ((500, 3), (500, 5)),
    ((500, 5), (400, 4)), ((500, 5), (300, 5)), ((300, 5), (200, 5)),
    ((200, 5), (100, 5)), ((100, 5), (0, 4)),
]  # fmt: skip

# Lines connecting the four shapes of a block, and a block with the next one.
_INNER_CONNECTORS = [((500, 3), (600, 3)), ((300, 5), (300, 6)), ((800, 5), (800, 6)), ((500, 8), (600, 8))]
_OUTER_CONNECTORS = [((1100, 3), (1200, 3)), ((900, 5), (900, 6)), ((1400, 5), (1400, 6)), ((1100, 8), (1200, 8))]


def generate(regions: int) -> list[Line]:
    def point(x: int, y: int) -> int:
        return x * MOD + y

    lines: list[Line] = []
    for block in range(regions + 1):
        bx = 1200 * block
        connectors = _INNER_CONNECTORS + (_OUTER_CONNECTORS if block < regions else [])
        lines += [(point(bx + x1, y1), point(bx + x2, y2)) for (x1, y1), (x2, y2) in connectors]
        for dx, dy in ((0, 0), (600, 0), (0, 6), (600, 6)):
            lines += [(point(bx + dx + x1, dy + y1), point(bx + dx + x2, dy + y2)) for (x1, y1), (x2, y2) in _SHAPE]
    return lines


# ============================================================================
# Junction classification (implemented in C in the original OPS5 benchmark)
# ============================================================================


def _angle(p1: int, p2: int) -> float:
    return math.atan2(p2 % MOD - p1 % MOD, p2 // MOD - p1 // MOD)


def _inscribed_angle(base: int, p1: int, p2: int) -> float:
    diff = abs(_angle(base, p1) - _angle(base, p2))
    return 2 * math.pi - diff if diff > math.pi else diff


def classify_3_junction(base: int, p1: int, p2: int, p3: int) -> tuple[str, int, int, int]:
    """Returns (type, barb1, shaft, barb2) for three edges meeting at `base`."""
    a12 = _inscribed_angle(base, p1, p2)
    a13 = _inscribed_angle(base, p1, p3)
    a23 = _inscribed_angle(base, p2, p3)
    s1213, s1223, s1323 = a12 + a13, a12 + a23, a13 + a23
    if s1213 < s1223:
        total, shaft, barb1, barb2 = (s1213, p1, p2, p3) if s1213 < s1323 else (s1323, p3, p1, p2)
    else:
        total, shaft, barb1, barb2 = (s1223, p2, p1, p3) if s1223 < s1323 else (s1323, p3, p1, p2)
    if abs(total - math.pi) < 0.001:
        type_ = "tee"
    elif total > math.pi:
        type_ = "fork"
    else:
        type_ = "arrow"
    return type_, barb1, shaft, barb2


# ============================================================================
# Results
# ============================================================================

LABELS = ("B", "+", "-", "nil")


def summarize(edge_labels: dict[tuple[int, int], str], junction_types: list[str]) -> str:
    """
    Engine-independent summary of the final state: counts of edge labels and junction types,
    and a digest of the label of every edge.
    """
    labels = Counter(edge_labels.values())
    junctions = Counter(junction_types)
    digest = hashlib.sha1(repr(sorted(edge_labels.items())).encode()).hexdigest()[:12]
    return (
        " ".join(f"{label}={labels.get(label, 0)}" for label in LABELS)
        + " | "
        + " ".join(f"{t}={junctions.get(t, 0)}" for t in ("L", "arrow", "fork", "tee"))
        + f" | {digest}"
    )
