from dataclasses import dataclass
from random import Random

import pytest

from saengra import Environment
from saengra.graph import AddEdge, AddVertex, RemoveEdge, Subgraph
from saengra.observer import Observer, observer


@dataclass(frozen=True, slots=True)
class Anchor:
    id: int


ANCHORS = [Anchor(i) for i in range(3)]
VALUES = list(range(6))
VERTICES = [*ANCHORS, *VALUES]
LABELS = ["e", "f", "g"]

# Operands of (all: ), (maybe: ) and & are matched as nested expressions, whose subgraphs are
# cached, maintained incrementally and shared by all observers. Lookaheads are matched in place.
EXPRESSIONS = [
    "Anchor as a (all: -e> * -f> * as y)",
    "Anchor as a (all: -e> * (maybe: -f> * as y))",
    "Anchor as a (all: -e> * -f> a | -g> *)",
    "Anchor as a (-e> * -f> * as y & -g> * as y)",
    "Anchor as a (all: -e> * (all: -f> * -g> *))",
    "Anchor as a (?= -e> * -f> *) -g> * as y",
    "Anchor as a -e> * as x (?! -f> * -g> x)",
    "Anchor as a (?! -e> * -f> * as y) -g> * as y",
    "Anchor as a (all: -e> * (?= -f> * -g> a))",
    # The same nested expression as in the first one, from other start positions.
    "Anchor as a -g> Anchor (all: -e> * -f> * as y)",
]


def key(subgraph: Subgraph) -> tuple:
    return tuple(sorted(subgraph.refs.items(), key=repr)), frozenset(
        (e.from_, e.label, e.to) for e in subgraph.edges
    )


def track(expression: str) -> tuple[Observer, dict[tuple, set[frozenset]]]:
    matched: dict[tuple, set[frozenset]] = {}
    o = observer(expression, dont_register=True)

    @o.on_create
    def on_create(sg: Subgraph, **refs: object) -> None:
        refs_key, edges = key(sg)
        matched.setdefault(refs_key, set()).add(edges)

    @o.on_change
    def on_change(sg: Subgraph, **refs: object) -> None:
        refs_key, edges = key(sg)
        matched[refs_key] = {edges}

    @o.on_delete
    def on_delete(sg: Subgraph, **refs: object) -> None:
        refs_key, edges = key(sg)
        matched[refs_key].discard(edges)
        if not matched[refs_key]:
            del matched[refs_key]

    return o, matched


@pytest.mark.parametrize("seed", range(10))
def test_nested_expressions_are_maintained_incrementally(empty_env: Environment, seed: int) -> None:
    random = Random(seed)
    tracked = [track(expression) for expression in EXPRESSIONS]
    empty_env.register_observers([o for o, _ in tracked])
    empty_env.update_many([AddVertex(v) for v in VERTICES])
    empty_env.commit()

    edges: set[tuple] = set()
    for _ in range(40):
        updates = []
        for _ in range(random.randint(1, 4)):
            edge = (random.choice(VERTICES), random.choice(LABELS), random.choice(VERTICES))
            if edge in edges:
                edges.discard(edge)
                updates.append(RemoveEdge(*edge))
            else:
                edges.add(edge)
                updates.append(AddEdge(*edge))
        empty_env.update_many(updates)
        empty_env.commit()

        for expression, (_, matched) in zip(EXPRESSIONS, tracked):
            expected: dict[tuple, set[frozenset]] = {}
            for sg in empty_env.match_subgrapghs(expression):
                refs_key, sg_edges = key(sg)
                expected.setdefault(refs_key, set()).add(sg_edges)
            assert matched == expected, expression
