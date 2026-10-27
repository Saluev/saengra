from collections import Counter
from collections.abc import Iterator
from dataclasses import dataclass
from random import Random

import pytest

from saengra import Environment
from saengra.graph import AddEdge, AddVertex, RemoveEdge, Subgraph
from saengra.observer import Observer, observer


@dataclass(frozen=True, slots=True)
class Anchor:
    id: int


def edges_of(sg: Subgraph) -> frozenset[tuple]:
    return frozenset((e.from_, e.label, e.to) for e in sg.edges)


def commit(env: Environment, *updates: object) -> None:
    env.update_many(list(updates))
    env.commit()


@pytest.fixture
def chain(empty_env: Environment) -> Environment:
    # 1 -> 2 -> 3 -> 4 -> 5, and a dead end 2 -> 6.
    commit(
        empty_env,
        *[AddVertex(v) for v in range(1, 7)],
        *[AddEdge(v, "n", v + 1) for v in range(1, 5)],
        AddEdge(2, "n", 6),
    )
    return empty_env


# Chains from each start as (start, repeats): 1 -> ... -> 5 and 1 -> 2 -> 6 are maximal, but 1 -> 2 isn't.
@pytest.mark.parametrize(
    ("repeat", "chains"),
    [
        ("3x", [(1, 3), (2, 3)]),
        ("2-3x", [(1, 2), (1, 3), (2, 3), (3, 2)]),
        ("1+x", [(1, 2), (1, 4), (2, 1), (2, 3), (3, 2), (4, 1)]),
        ("3+x", [(1, 4), (2, 3)]),
        ("0+x", [(1, 2), (1, 4), (2, 1), (2, 3), (3, 2), (4, 1), (5, 0), (6, 0)]),
        ("", [(1, 2), (1, 4), (2, 1), (2, 3), (3, 2), (4, 1), (5, 0), (6, 0)]),
        ("0x", [(v, 0) for v in range(1, 7)]),
    ],
)
def test_repeat_matches_maximal_chains(
    chain: Environment, repeat: str, chains: list[tuple[int, int]]
) -> None:
    subgraphs = chain.match_subgrapghs(f"int as s (repeat {repeat}: -n> int)")
    assert sorted((sg.refs["s"], len(sg.edges)) for sg in subgraphs) == chains


def test_chains_with_max_repeats_are_maximal(chain: Environment) -> None:
    subgraphs = chain.match_subgrapghs("int as s (repeat 2x: -n> int)")
    assert sorted(
        (sg.refs["s"], sorted(edges_of(sg))) for sg in subgraphs if sg.refs["s"] == 1
    ) == [
        (1, [(1, "n", 2), (2, "n", 3)]),
        (1, [(1, "n", 2), (2, "n", 6)]),
    ]


def test_repeat_doesnt_backtrack(chain: Environment) -> None:
    # The maximal chains from 1 end at 5 and 6, which have no `m` edge; 1 -> 2 -> 3 isn't tried.
    commit(chain, AddEdge(3, "m", 1))
    assert chain.match_subgrapghs("int as s (repeat 1-4x: -n> int) -m> int") == []
    assert len(chain.match_subgrapghs("int as s (repeat 1-2x: -n> int) -m> int")) == 1


def test_zero_repeats_match_like_noop(chain: Environment) -> None:
    assert Counter(
        (sg.refs["s"], edges_of(sg))
        for sg in chain.match_subgrapghs("int as s (repeat 0x: -n> int)")
    ) == Counter(
        (sg.refs["s"], edges_of(sg)) for sg in chain.match_subgrapghs("int as s .")
    )


def test_minimum_cant_exceed_maximum(empty_env: Environment) -> None:
    with pytest.raises(RuntimeError, match="parse error"):
        empty_env.match_subgrapghs("int (repeat 3-2x: -n> int)")


# Randomized checks against a reference implementation, on graphs with cycles.

ANCHORS = [Anchor(i) for i in range(3)]
VALUES = list(range(5))
VERTICES = [*ANCHORS, *VALUES]
LABELS = ["n", "m"]


def steps(
    edges: set[tuple], operand: str, v: object
) -> list[tuple[object, list[tuple]]]:
    if operand == "-n> *":
        return [
            (to, [(v, "n", to)])
            for from_, label, to in edges
            if from_ == v and label == "n"
        ]
    assert operand == "<n&m> *"
    return [
        (to, [(v, "m", to), (to, "n", v)])
        for from_, label, to in edges
        if from_ == v and label == "m" and (to, "n", v) in edges
    ]


def maximal_walks(
    edges: set[tuple],
    operand: str,
    v: object,
    walk: list[tuple],
    repeats: int,
    min_: int,
    max_: int,
) -> Iterator[list[tuple]]:
    continuations = [] if repeats == max_ else steps(edges, operand, v)
    if not continuations:
        if repeats >= min_:
            yield walk
        return
    for to, step in continuations:
        yield from maximal_walks(
            edges, operand, to, walk + step, repeats + 1, min_, max_
        )


def reference(edges: set[tuple], operand: str, min_: int, max_: int) -> set[tuple]:
    # Subgraphs are compared as sets: different walks may consist of the same edges.
    return {
        (a, frozenset(walk))
        for a in ANCHORS
        for walk in maximal_walks(edges, operand, a, [], 0, min_, max_)
    }


def random_edges(random: Random) -> set[tuple]:
    edges = set()
    for _ in range(random.randint(0, 20)):
        from_, label, to = (
            random.choice(VERTICES),
            random.choice(LABELS),
            random.choice(VERTICES),
        )
        edges.add((from_, label, to))
        if random.random() < 0.5:
            # Edges back with the other label make `<n&m>` match.
            edges.add((to, "n" if label == "m" else "m", from_))
    return edges


@pytest.mark.parametrize("seed", range(20))
@pytest.mark.parametrize("operand", ["-n> *", "<n&m> *"])
@pytest.mark.parametrize(("min_", "max_"), [(0, 2), (1, 3), (2, 4), (3, 3)])
def test_repeat_matches_reference(
    empty_env: Environment, seed: int, operand: str, min_: int, max_: int
) -> None:
    edges = random_edges(Random(seed))
    commit(empty_env, *[AddVertex(v) for v in VERTICES], *[AddEdge(*e) for e in edges])
    expression = f"Anchor as a (repeat {min_}-{max_}x: {operand})"
    matched = {
        (sg.refs["a"], edges_of(sg)) for sg in empty_env.match_subgrapghs(expression)
    }
    assert matched == reference(edges, operand, min_, max_)


EXPRESSIONS = [
    "Anchor as a (repeat 1-3x: -n> *)",
    "Anchor as a (repeat 0-2x: -n> *)",
    "Anchor as a (repeat 2x: <n&m> *)",
    "Anchor as a (repeat 0-2x: -n> *) -m> * as y",
    "Anchor as a -m> * as y (repeat 1-2x: -n> *)",
    "Anchor as a (?= (repeat 2x: -n> *))",
    "Anchor as a (all: -m> * (repeat 1-2x: -n> *))",
]


def key(subgraph: Subgraph) -> tuple:
    return tuple(sorted(subgraph.refs.items(), key=repr)), edges_of(subgraph)


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
def test_repeat_is_maintained_incrementally(empty_env: Environment, seed: int) -> None:
    random = Random(seed)
    tracked = [track(expression) for expression in EXPRESSIONS]
    empty_env.register_observers([o for o, _ in tracked])
    commit(empty_env, *[AddVertex(v) for v in VERTICES])

    edges: set[tuple] = set()
    for _ in range(40):
        updates = []
        for _ in range(random.randint(1, 4)):
            edge = (
                random.choice(VERTICES),
                random.choice(LABELS),
                random.choice(VERTICES),
            )
            if edge in edges:
                edges.discard(edge)
                updates.append(RemoveEdge(*edge))
            else:
                edges.add(edge)
                updates.append(AddEdge(*edge))
        commit(empty_env, *updates)

        for expression, (_, matched) in zip(EXPRESSIONS, tracked):
            expected: dict[tuple, set[frozenset]] = {}
            for sg in empty_env.match_subgrapghs(expression):
                refs_key, sg_edges = key(sg)
                expected.setdefault(refs_key, set()).add(sg_edges)
            assert matched == expected, expression
