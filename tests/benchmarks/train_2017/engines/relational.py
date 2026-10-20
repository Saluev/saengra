"""
Reference Rete and TREAT implementations in pure Python over the same relational encoding.

There is no maintained open source TREAT implementation, so both algorithms are implemented
here side by side, sharing fact storage and alpha memories, so that the difference between
them is purely algorithmic.

Facts are tuples: `(Type, id)` for vertices, `(label, from, to)` for edges and
`(attribute, id, value)` for attributes. Queries are conjunctions of positive conditions,
negative conditions and tests; strings starting with `?` are variables, `_` is a wildcard
(allowed in negative conditions only).
"""

from collections import defaultdict
from dataclasses import dataclass
from typing import Callable, Iterable

from ..workload import Op


@dataclass(frozen=True)
class Pos:
    rel: str
    args: tuple


@dataclass(frozen=True)
class Neg:
    rel: str
    args: tuple


@dataclass(frozen=True)
class Test:
    vars: tuple[str, ...]
    fn: Callable[..., bool]


Condition = Pos | Neg | Test

MatchListener = Callable[[str, tuple, int], None]
""" Called with (query, match, +1/-1) whenever a match appears or disappears. """


def segment_chain() -> list[Condition]:
    conditions: list[Condition] = [Pos("monitoredBy", ("?s1", "?sen")), Pos("Segment", ("?s1",))]
    for i in range(2, 7):
        conditions += [
            Pos("connectsTo", (f"?s{i - 1}", f"?s{i}")),
            Pos("Segment", (f"?s{i}",)),
            Pos("monitoredBy", (f"?s{i}", "?sen")),
        ]
    return conditions


# Type conditions implied by the metamodel (e.g. `requires` always points to a Sensor)
# are omitted; they are only kept where the edge label doesn't determine the type.
QUERIES: dict[str, list[Condition]] = {
    "PosLength": [
        Pos("length", ("?s", "?l")),
        Test(("?l",), lambda l: l <= 0),
    ],
    "SwitchMonitored": [
        Pos("Switch", ("?sw",)),
        Neg("monitoredBy", ("?sw", "_")),
    ],
    "RouteSensor": [
        Pos("follows", ("?r", "?swP")),
        Pos("target", ("?swP", "?sw")),
        Pos("monitoredBy", ("?sw", "?sen")),
        Neg("requires", ("?r", "?sen")),
    ],
    "SwitchSet": [
        Pos("signal", ("?sem", "GO")),
        Pos("entry", ("?r", "?sem")),
        Pos("active", ("?r", True)),
        Pos("follows", ("?r", "?swP")),
        Pos("target", ("?swP", "?sw")),
        Pos("currentPosition", ("?sw", "?cp")),
        Pos("position", ("?swP", "?p")),
        Test(("?cp", "?p"), lambda cp, p: cp != p),
    ],
    "ConnectedSegments": segment_chain(),
    "SemaphoreNeighbor": [
        Pos("exit", ("?r1", "?sem")),
        Pos("requires", ("?r1", "?sen1")),
        Pos("monitoredBy", ("?te1", "?sen1")),
        Pos("connectsTo", ("?te1", "?te2")),
        Pos("monitoredBy", ("?te2", "?sen2")),
        Pos("requires", ("?r2", "?sen2")),
        Test(("?r1", "?r2"), lambda r1, r2: r1 != r2),
        Neg("entry", ("?r2", "?sem")),
    ],
}


def is_var(arg: object) -> bool:
    return isinstance(arg, str) and arg.startswith("?")


# ============================================================================
# Alpha memories (shared by both algorithms)
# ============================================================================


class AlphaMemory:
    """All facts of a relation passing constant tests, with lazily created hash indexes."""

    def __init__(self, rel: str, constants: tuple[tuple[int, object], ...]) -> None:
        self.rel = rel
        self.constants = constants
        self.facts: set[tuple] = set()
        self.indexes: dict[tuple[int, ...], dict[tuple, set[tuple]]] = {}
        self.successors: list = []

    def accepts(self, fact: tuple) -> bool:
        return all(fact[1 + i] == c for i, c in self.constants)

    def index(self, positions: tuple[int, ...]) -> dict[tuple, set[tuple]]:
        index = self.indexes.get(positions)
        if index is None:
            index = self.indexes[positions] = defaultdict(set)
            for fact in self.facts:
                index[tuple(fact[1 + p] for p in positions)].add(fact)
        return index

    def add(self, fact: tuple) -> None:
        self.facts.add(fact)
        for positions, index in self.indexes.items():
            index[tuple(fact[1 + p] for p in positions)].add(fact)

    def remove(self, fact: tuple) -> None:
        self.facts.discard(fact)
        for positions, index in self.indexes.items():
            key = tuple(fact[1 + p] for p in positions)
            bucket = index[key]
            bucket.discard(fact)
            if not bucket:
                del index[key]


class WorkingMemory:
    def __init__(self) -> None:
        self.alphas: dict[tuple, AlphaMemory] = {}
        self.alphas_by_rel: dict[str, list[AlphaMemory]] = defaultdict(list)
        self.attributes: dict[tuple[str, int], tuple] = {}

    def alpha(self, rel: str, args: tuple) -> AlphaMemory:
        constants = tuple(
            (i, a) for i, a in enumerate(args) if not is_var(a) and a != "_"
        )
        key = (rel, constants)
        if key not in self.alphas:
            self.alphas[key] = AlphaMemory(rel, constants)
            self.alphas_by_rel[rel].append(self.alphas[key])
        return self.alphas[key]

    def to_facts(self, ops: Iterable[Op]) -> Iterable[tuple[int, tuple]]:
        """Converts operations to (sign, fact) pairs."""
        for op in ops:
            match op:
                case ("N", type_, id_):
                    yield 1, (type_, id_)
                case ("E", label, a, b):
                    yield 1, (label, a, b)
                case ("R", label, a, b):
                    yield -1, (label, a, b)
                case ("A", attr, id_, value):
                    old = self.attributes.get((attr, id_))
                    if old is not None:
                        yield -1, old
                    fact = self.attributes[attr, id_] = (attr, id_, value)
                    yield 1, fact

    def apply(self, ops: Iterable[Op], on_fact: Callable[[int, tuple, AlphaMemory], None]) -> None:
        for sign, fact in self.to_facts(ops):
            for alpha in self.alphas_by_rel.get(fact[0], ()):
                if not alpha.accepts(fact):
                    continue
                if sign > 0:
                    if fact in alpha.facts:
                        continue
                    alpha.add(fact)
                else:
                    if fact not in alpha.facts:
                        continue
                    alpha.remove(fact)
                on_fact(sign, fact, alpha)


# ============================================================================
# Rete
# ============================================================================


class _ReteNode:
    """Join or negative node with its left (beta) memory, indexed by the join key."""

    def __init__(self, alpha: AlphaMemory, cond: Pos | Neg, layout: list[str], depth: int) -> None:
        self.alpha = alpha
        self.depth = depth
        self.negative = isinstance(cond, Neg)
        self.left_positions = tuple(layout.index(a) for a in cond.args if is_var(a) and a in layout)
        self.right_positions = tuple(i for i, a in enumerate(cond.args) if is_var(a) and a in layout)
        self.new_positions = tuple(
            i for i, a in enumerate(cond.args) if is_var(a) and a not in layout
        )
        self.right_index = alpha.index(self.right_positions)
        self.left_memory: dict[tuple, set[tuple]] = defaultdict(set)
        self.tests: list[tuple[tuple[int, ...], Callable[..., bool]]] = []
        self.child: "_ReteNode | _ReteTerminal | None" = None

    def _extend(self, token: tuple, fact: tuple) -> tuple | None:
        out = token + tuple(fact[1 + i] for i in self.new_positions)
        for positions, fn in self.tests:
            if not fn(*(out[p] for p in positions)):
                return None
        return out

    def left_activate(self, token: tuple, sign: int) -> None:
        key = tuple(token[p] for p in self.left_positions)
        if sign > 0:
            self.left_memory[key].add(token)
        else:
            bucket = self.left_memory[key]
            bucket.discard(token)
            if not bucket:
                del self.left_memory[key]
        facts = self.right_index.get(key, ())
        if self.negative:
            if not facts:
                self.child.left_activate(token, sign)
            return
        for fact in facts:
            out = self._extend(token, fact)
            if out is not None:
                self.child.left_activate(out, sign)

    def right_activate(self, fact: tuple, sign: int) -> None:
        key = tuple(fact[1 + p] for p in self.right_positions)
        tokens = self.left_memory.get(key)
        if not tokens:
            return
        if self.negative:
            remaining = len(self.right_index.get(key, ()))
            if sign > 0 and remaining == 1:
                for token in list(tokens):
                    self.child.left_activate(token, -1)
            elif sign < 0 and remaining == 0:
                for token in list(tokens):
                    self.child.left_activate(token, 1)
            return
        for token in list(tokens):
            out = self._extend(token, fact)
            if out is not None:
                self.child.left_activate(out, sign)


class _ReteTerminal:
    def __init__(self, query: str, listener: MatchListener | None) -> None:
        self.query = query
        self.listener = listener
        self.matches: set[tuple] = set()

    def left_activate(self, token: tuple, sign: int) -> None:
        if sign > 0:
            self.matches.add(token)
        else:
            self.matches.discard(token)
        if self.listener is not None:
            self.listener(self.query, token, sign)


class ReteEngine:
    name = "Rete (reference)"
    algorithm = "Rete"
    language = "Python"

    def __init__(
        self,
        queries: dict[str, list[Condition]] = QUERIES,
        listener: MatchListener | None = None,
    ) -> None:
        self.wm = WorkingMemory()
        self.terminals: dict[str, _ReteTerminal] = {}
        nodes_by_alpha: dict[int, list[_ReteNode]] = defaultdict(list)
        for query, conditions in queries.items():
            layout: list[str] = []
            nodes: list[_ReteNode] = []
            for cond in conditions:
                if isinstance(cond, Test):
                    nodes[-1].tests.append((tuple(layout.index(v) for v in cond.vars), cond.fn))
                    continue
                node = _ReteNode(self.wm.alpha(cond.rel, cond.args), cond, layout, len(nodes))
                if nodes:
                    nodes[-1].child = node
                nodes.append(node)
                if isinstance(cond, Pos):
                    layout = layout + [a for a in cond.args if is_var(a) and a not in layout]
                nodes_by_alpha[id(node.alpha)].append(node)
            nodes[-1].child = self.terminals[query] = _ReteTerminal(query, listener)
            nodes[0].left_memory[()].add(())  # dummy top token
        for alpha in self.wm.alphas.values():
            # Descendants are right-activated before ancestors, so that a fact appearing in
            # several conditions of the same query produces every token exactly once.
            alpha.successors = sorted(nodes_by_alpha[id(alpha)], key=lambda n: -n.depth)

    def _on_fact(self, sign: int, fact: tuple, alpha: AlphaMemory) -> None:
        for node in alpha.successors:
            node.right_activate(fact, sign)

    def load(self, ops: list[Op]) -> None:
        self.wm.apply(ops, self._on_fact)

    def transform(self, ops: list[Op]) -> None:
        self.wm.apply(ops, self._on_fact)

    def check(self) -> dict[str, int]:
        return {q: len(t.matches) for q, t in self.terminals.items()}


# ============================================================================
# TREAT
# ============================================================================


@dataclass
class _Step:
    index: dict[tuple, set[tuple]]
    key_vars: tuple[str, ...]
    assigns: tuple[tuple[int, str], ...]
    checks: list["_Check"]


@dataclass
class _Check:
    """A test, or a negative condition (`index` is set), that can be evaluated at this point."""

    vars: tuple[str, ...]
    fn: Callable[..., bool] | None = None
    index: dict[tuple, set[tuple]] | None = None

    def passes(self, binding: dict[str, object]) -> bool:
        values = tuple(binding[v] for v in self.vars)
        if self.index is not None:
            return not self.index.get(values)
        return self.fn(*values)


class _TreatQuery:
    """
    TREAT keeps no beta memories: only alpha memories and the conflict set (matches).
    On insertion a seeded search is run from the new fact; on deletion matches containing
    the fact are dropped. Negative conditions work the other way round.

    Join orders for every seed are compiled in advance (greedily, most bound arguments first).
    """

    def __init__(
        self,
        wm: WorkingMemory,
        name: str,
        conditions: list[Condition],
        listener: MatchListener | None,
    ) -> None:
        self.name = name
        self.listener = listener
        self.positives = [c for c in conditions if isinstance(c, Pos)]
        self.negatives = [c for c in conditions if isinstance(c, Neg)]
        self.tests = [c for c in conditions if isinstance(c, Test)]
        self.vars: list[str] = []
        for cond in self.positives:
            self.vars += [a for a in cond.args if is_var(a) and a not in self.vars]
        self.alphas = {cond: wm.alpha(cond.rel, cond.args) for cond in self.positives + self.negatives}
        self.matches: set[tuple] = set()
        # Positive condition -> fact -> matches supported by that fact.
        self.supported_by: dict[Pos, dict[tuple, set[tuple]]] = {
            c: defaultdict(set) for c in self.positives
        }
        # Negative condition -> values of its variables -> matches that would be blocked.
        self.blockable: dict[Neg, dict[tuple, set[tuple]]] = {
            c: defaultdict(set) for c in self.negatives
        }
        self.neg_vars = {
            neg: tuple(a for a in neg.args if is_var(a)) for neg in self.negatives
        }
        self.seed_plans = {
            cond: self._compile(
                {a for a in cond.args if is_var(a)},
                [c for c in self.positives if c is not cond],
            )
            for cond in self.positives
        }
        self.unblock_plans = {
            neg: self._compile(set(self.neg_vars[neg]), list(self.positives))
            for neg in self.negatives
        }

    def _checks_for(self, bound: set[str], newly_bound: set[str]) -> list[_Check]:
        checks = []
        for test in self.tests:
            if set(test.vars) <= bound and set(test.vars) & newly_bound:
                checks.append(_Check(test.vars, fn=test.fn))
        for neg in self.negatives:
            vars_ = self.neg_vars[neg]
            if set(vars_) <= bound and set(vars_) & newly_bound:
                positions = tuple(i for i, a in enumerate(neg.args) if is_var(a))
                checks.append(_Check(vars_, index=self.alphas[neg].index(positions)))
        return checks

    def _compile(self, bound: set[str], remaining: list[Pos]) -> tuple[list[_Check], list[_Step]]:
        bound = set(bound)
        initial = self._checks_for(bound, bound)
        steps = []
        remaining = list(remaining)
        while remaining:
            cond = max(remaining, key=lambda c: sum(1 for a in c.args if a in bound))
            remaining.remove(cond)
            positions = tuple(i for i, a in enumerate(cond.args) if is_var(a) and a in bound)
            assigns = tuple((i, a) for i, a in enumerate(cond.args) if is_var(a) and a not in bound)
            new = {a for _, a in assigns}
            bound |= new
            steps.append(
                _Step(
                    index=self.alphas[cond].index(positions),
                    key_vars=tuple(cond.args[p] for p in positions),
                    assigns=assigns,
                    checks=self._checks_for(bound, new),
                )
            )
        return initial, steps

    def _run(self, plan: tuple[list[_Check], list[_Step]], binding: dict[str, object]) -> None:
        initial, steps = plan
        if all(check.passes(binding) for check in initial):
            self._run_steps(steps, 0, binding)

    def _run_steps(self, steps: list[_Step], i: int, binding: dict[str, object]) -> None:
        if i == len(steps):
            self._add(binding)
            return
        step = steps[i]
        facts = step.index.get(tuple(binding[v] for v in step.key_vars))
        if not facts:
            return
        for fact in list(facts):
            new = dict(binding)
            for p, var in step.assigns:
                new[var] = fact[1 + p]
            if all(check.passes(new) for check in step.checks):
                self._run_steps(steps, i + 1, new)

    def _fact_for(self, cond: Pos, binding: dict[str, object]) -> tuple:
        return (cond.rel, *(binding[a] if is_var(a) else a for a in cond.args))

    def _add(self, binding: dict[str, object]) -> None:
        match = tuple(binding[v] for v in self.vars)
        if match in self.matches:
            return
        self.matches.add(match)
        if self.listener is not None:
            self.listener(self.name, match, 1)
        for cond in self.positives:
            self.supported_by[cond][self._fact_for(cond, binding)].add(match)
        for neg in self.negatives:
            self.blockable[neg][tuple(binding[v] for v in self.neg_vars[neg])].add(match)

    def _remove(self, match: tuple) -> None:
        if match not in self.matches:
            return
        self.matches.discard(match)
        if self.listener is not None:
            self.listener(self.name, match, -1)
        binding = dict(zip(self.vars, match))
        for cond in self.positives:
            self.supported_by[cond][self._fact_for(cond, binding)].discard(match)
        for neg in self.negatives:
            self.blockable[neg][tuple(binding[v] for v in self.neg_vars[neg])].discard(match)

    def on_fact(self, sign: int, fact: tuple, alpha: AlphaMemory) -> None:
        for cond in self.positives:
            if self.alphas[cond] is not alpha:
                continue
            if sign > 0:
                binding = {a: fact[1 + i] for i, a in enumerate(cond.args) if is_var(a)}
                self._run(self.seed_plans[cond], binding)
            else:
                for match in list(self.supported_by[cond].pop(fact, ())):
                    self._remove(match)
        for neg in self.negatives:
            if self.alphas[neg] is not alpha:
                continue
            binding = {a: fact[1 + i] for i, a in enumerate(neg.args) if is_var(a)}
            key = tuple(binding[v] for v in self.neg_vars[neg])
            if sign > 0:
                for match in list(self.blockable[neg].get(key, ())):
                    self._remove(match)
            else:
                # The unblock plan checks the negative condition itself, so another
                # remaining blocker prevents re-adding the matches.
                self._run(self.unblock_plans[neg], binding)


class TreatEngine:
    name = "TREAT (reference)"
    algorithm = "TREAT"
    language = "Python"

    def __init__(
        self,
        queries: dict[str, list[Condition]] = QUERIES,
        listener: MatchListener | None = None,
    ) -> None:
        self.wm = WorkingMemory()
        self.queries = {
            q: _TreatQuery(self.wm, q, conds, listener) for q, conds in queries.items()
        }
        by_alpha: dict[int, list[_TreatQuery]] = defaultdict(list)
        for query in self.queries.values():
            for alpha in {id(a): a for a in query.alphas.values()}.values():
                by_alpha[id(alpha)].append(query)
        for alpha in self.wm.alphas.values():
            alpha.successors = by_alpha[id(alpha)]

    def _on_fact(self, sign: int, fact: tuple, alpha: AlphaMemory) -> None:
        for query in alpha.successors:
            query.on_fact(sign, fact, alpha)

    def load(self, ops: list[Op]) -> None:
        self.wm.apply(ops, self._on_fact)

    def transform(self, ops: list[Op]) -> None:
        self.wm.apply(ops, self._on_fact)

    def check(self) -> dict[str, int]:
        return {q: len(query.matches) for q, query in self.queries.items()}
