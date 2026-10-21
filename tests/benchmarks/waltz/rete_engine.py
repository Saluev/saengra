"""
The reference Python Rete (train_2017/engines/relational.py) turned into a production system:
rule matches are activations on an agenda, fired one at a time by salience, newest first
(CLIPS's default depth strategy). Rules are a literal translation of waltz.clp.
"""

import heapq
from dataclasses import dataclass, field
from typing import Callable

from tests.benchmarks.train_2017.engines.relational import Condition, Neg, Pos, ReteEngine, Test, is_var
from tests.benchmarks.waltz.workload import Line, classify_3_junction

FIELDS = {
    "stage": ("value",),
    "line": ("p1", "p2"),
    "edge": ("p1", "p2", "joined", "label", "plotted"),
    "junction": ("p1", "p2", "p3", "base_point", "type"),
}

Bindings = dict[str, object]


@dataclass
class Rule:
    name: str
    conditions: list[Condition]
    action: Callable[["ProductionSystem", Bindings], None]
    salience: int = 0
    facts: dict[str, Pos] = field(default_factory=dict)
    """ Conditions bound to fact variables (`?f <- (...)` in CLIPS), by variable. """


def pattern(rel: str, fact: str | None = None, **slots: object) -> Pos:
    """
    Condition on a fact with given slots; other slots are bound to variables `?FACT.slot`
    if the fact is named, so that it can be modified.
    """
    args = tuple(slots.get(f, f"?{fact}.{f}" if fact else "_") for f in FIELDS[rel])
    return Pos(rel, args)


def rule(name: str, *conditions: Condition | tuple[str, Pos], action, salience: int = 0) -> Rule:
    facts: dict[str, Pos] = {}
    flat: list[Condition] = []
    for cond in conditions:
        if isinstance(cond, tuple):
            facts[cond[0]], cond = cond[1], cond[1]
        flat.append(cond)
    return Rule(name, flat, action, salience, facts)


def named(fact: str, rel: str, **slots: object) -> tuple[str, Pos]:
    return fact, pattern(rel, fact, **slots)


class ProductionSystem:
    def __init__(self, rules: list[Rule]) -> None:
        self.rules = {r.name: r for r in rules}
        self.engine = ReteEngine({r.name: r.conditions for r in rules}, self._on_match)
        self.agenda: dict[tuple[str, tuple], int] = {}
        self.queue: list[tuple[int, int, str, tuple]] = []
        self.counter = 0
        self.firings = 0
        self.facts: set[tuple] = set()

    def _on_match(self, rule: str, token: tuple, sign: int) -> None:
        key = (rule, token)
        if sign > 0:
            self.counter += 1
            self.agenda[key] = self.counter
            heapq.heappush(self.queue, (-self.rules[rule].salience, -self.counter, rule, token))
        else:
            self.agenda.pop(key, None)

    def assert_fact(self, fact: tuple) -> None:
        if fact not in self.facts:
            self.facts.add(fact)
            self.engine.apply_facts([(1, fact)])

    def retract_fact(self, fact: tuple) -> None:
        if fact in self.facts:
            self.facts.discard(fact)
            self.engine.apply_facts([(-1, fact)])

    def modify(self, b: Bindings, fact_var: str, **slots: object) -> None:
        cond = b["$rule"].facts[fact_var]
        old = (cond.rel, *(b[a] if is_var(a) else a for a in cond.args))
        new = (cond.rel, *(slots.get(f, v) for f, v in zip(FIELDS[cond.rel], old[1:])))
        if old != new:
            self.retract_fact(old)
            self.assert_fact(new)

    def retract(self, b: Bindings, fact_var: str) -> None:
        cond = b["$rule"].facts[fact_var]
        self.retract_fact((cond.rel, *(b[a] if is_var(a) else a for a in cond.args)))

    def run(self) -> None:
        layouts = self.engine.layouts
        while self.queue:
            _, neg_counter, name, token = heapq.heappop(self.queue)
            if self.agenda.get((name, token)) != -neg_counter:
                continue  # the activation is gone, or was replaced by a newer one
            del self.agenda[name, token]
            rule = self.rules[name]
            bindings: Bindings = dict(zip(layouts[name], token))
            bindings["$rule"] = rule
            rule.action(self, bindings)
            self.firings += 1


# ============================================================================
# Waltz rules
# ============================================================================


def _ne(x: object, *values: object) -> bool:
    return all(x != v for v in values)


def _stage(value: str) -> Pos:
    return pattern("stage", value=value)


def _set_stage(ps: ProductionSystem, b: Bindings, value: str) -> None:
    ps.modify(b, "s", value=value)


def _reverse_edges(ps: ProductionSystem, b: Bindings) -> None:
    p1, p2 = b["?p1"], b["?p2"]
    ps.assert_fact(("edge", p1, p2, False, "nil", "nil"))
    ps.assert_fact(("edge", p2, p1, False, "nil", "nil"))
    ps.retract(b, "l")


def _make_3_junction(ps: ProductionSystem, b: Bindings) -> None:
    type_, barb1, shaft, barb2 = classify_3_junction(b["?bp"], b["?p1"], b["?p2"], b["?p3"])
    ps.assert_fact(("junction", barb1, shaft, barb2, b["?bp"], type_))
    for f in ("f2", "f3", "f4"):
        ps.modify(b, f, joined=True)


def _make_l(ps: ProductionSystem, b: Bindings) -> None:
    ps.assert_fact(("junction", b["?p2"], b["?p3"], "nil", b["?bp"], "L"))
    ps.modify(b, "f2", joined=True)
    ps.modify(b, "f3", joined=True)


def _labels(stage: str | None = None, **labels: str) -> Callable[[ProductionSystem, Bindings], None]:
    """Action setting labels of named facts; a label starting with `?` is taken from a variable."""

    def action(ps: ProductionSystem, b: Bindings) -> None:
        for fact, label in labels.items():
            ps.modify(b, fact, label=b[label] if is_var(label) else label)
        if stage is not None:
            _set_stage(ps, b, stage)

    return action


def _boundary_rules(stage: str, next_stage: str, prefix: str, outermost: Callable[[int, int], bool]) -> list[Rule]:
    return [
        rule(
            f"{prefix}_boundary_junction_L",
            named("s", "stage", value=stage),
            pattern("junction", type="L", base_point="?bp", p1="?p1", p2="?p2"),
            named("f3", "edge", p1="?bp", p2="?p1"),
            named("f4", "edge", p1="?bp", p2="?p2"),
            Neg("junction", ("_", "_", "_", "?other", "_"), Test(("?other", "?bp"), outermost)),
            action=_labels(next_stage, f3="B", f4="B"),
        ),
        rule(
            f"{prefix}_boundary_junction_arrow",
            named("s", "stage", value=stage),
            pattern("junction", type="arrow", base_point="?bp", p1="?p1", p2="?p2", p3="?p3"),
            named("f3", "edge", p1="?bp", p2="?p1"),
            named("f4", "edge", p1="?bp", p2="?p2"),
            named("f5", "edge", p1="?bp", p2="?p3"),
            Neg("junction", ("_", "_", "_", "?other", "_"), Test(("?other", "?bp"), outermost)),
            action=_labels(next_stage, f3="B", f4="+", f5="B"),
        ),
    ]


def _arrow_rules() -> list[Rule]:
    """label_arrow-1A ... label_arrow-5B: (known edge, its labels, edge set, result labels)."""
    variants = [
        # name, known edge, known labels, f4 edge, f5 edge, f4 label, f5 label
        ("1", "?p1", ("B", "-"), "?p2", "?p3", "+", "?label"),
        ("2", "?p3", ("B", "-"), "?p2", "?p1", "+", "?label"),
        ("3", "?p1", ("+",), "?p2", "?p3", "-", "+"),
        ("4", "?p3", ("+",), "?p2", "?p1", "-", "+"),
    ]
    rules = []
    junction = pattern("junction", type="arrow", base_point="?bp", p1="?p1", p2="?p2", p3="?p3")
    for name, known, labels, e4, e5, l4, l5 in variants:
        known_edge = pattern("edge", p1="?bp", p2=known, label="?label")
        is_known = Test(("?label",), lambda l, labels=labels: l in labels)
        rules += [
            rule(
                f"label_arrow-{name}A", _stage("labeling"), junction, known_edge, is_known,
                named("f4", "edge", p1="?bp", p2=e4, label="nil"), named("f5", "edge", p1="?bp", p2=e5),
                action=_labels(f4=l4, f5=l5), salience=5,
            ),
            rule(
                f"label_arrow-{name}B", _stage("labeling"), junction, known_edge, is_known,
                named("f4", "edge", p1="?bp", p2=e4), named("f5", "edge", p1="?bp", p2=e5, label="nil"),
                action=_labels(f4=l4, f5=l5),
            ),
        ]  # fmt: skip
    minus_edge = pattern("edge", p1="?bp", p2="?p2", label="-")
    rules += [
        rule(
            "label_arrow-5A", _stage("labeling"), junction, minus_edge,
            named("f4", "edge", p1="?bp", p2="?p1"), named("f5", "edge", p1="?bp", p2="?p3", label="nil"),
            action=_labels(f4="+", f5="+"), salience=5,
        ),
        rule(
            "label_arrow-5B", _stage("labeling"), junction, minus_edge,
            named("f4", "edge", p1="?bp", p2="?p1", label="nil"), named("f5", "edge", p1="?bp", p2="?p3"),
            action=_labels(f4="+", f5="+"),
        ),
    ]  # fmt: skip
    return rules


def _fork_rules() -> list[Rule]:
    fork = pattern("junction", type="fork", base_point="?bp")
    rules = [
        rule(
            "label_fork-1", _stage("labeling"), fork,
            pattern("edge", p1="?bp", p2="?p1", label="+"),
            named("f4", "edge", p1="?bp", p2="?p2", label="nil"), Test(("?p2", "?p1"), _ne),
            named("f5", "edge", p1="?bp", p2="?p3"), Test(("?p3", "?p2", "?p1"), _ne),
            action=_labels(f4="+", f5="+"),
        ),
    ]  # fmt: skip
    for name, l1, l2, result in (("2", "B", "-", "B"), ("3", "B", "B", "-"), ("4", "-", "-", "-")):
        rules.append(
            rule(
                f"label_fork-{name}", _stage("labeling"), fork,
                pattern("edge", p1="?bp", p2="?p1", label=l1),
                pattern("edge", p1="?bp", p2="?p2", label=l2), Test(("?p2", "?p1"), _ne),
                named("f5", "edge", p1="?bp", p2="?p3", label="nil"), Test(("?p3", "?p2", "?p1"), _ne),
                action=_labels(f5=result),
            )
        )  # fmt: skip
    return rules


def waltz_rules() -> list[Rule]:
    return [
        rule(
            "reverse_edges", _stage("duplicate"), named("l", "line", p1="?p1", p2="?p2"), action=_reverse_edges
        ),
        rule(
            "done_reversing", named("s", "stage", value="duplicate"), Neg("line", ("_", "_")),
            action=lambda ps, b: _set_stage(ps, b, "detect_junctions"), salience=-10,
        ),
        rule(
            "make-3_junction", _stage("detect_junctions"),
            named("f2", "edge", p1="?bp", p2="?p1", joined=False),
            named("f3", "edge", p1="?bp", p2="?p2", joined=False), Test(("?p2", "?p1"), _ne),
            named("f4", "edge", p1="?bp", p2="?p3", joined=False), Test(("?p3", "?p1", "?p2"), _ne),
            action=_make_3_junction, salience=10,
        ),
        rule(
            "make_L", _stage("detect_junctions"),
            named("f2", "edge", p1="?bp", p2="?p2", joined=False),
            named("f3", "edge", p1="?bp", p2="?p3", joined=False), Test(("?p3", "?p2"), _ne),
            Neg("edge", ("?bp", "?other", "_", "_", "_"), Test(("?other", "?p2", "?p3"), _ne)),
            action=_make_l,
        ),
        rule(
            "done_detecting", named("s", "stage", value="detect_junctions"),
            Neg("edge", ("_", "_", False, "_", "_")),
            action=lambda ps, b: _set_stage(ps, b, "find_initial_boundary"), salience=-10,
        ),
        *_boundary_rules("find_initial_boundary", "find_second_boundary", "initial", lambda o, bp: o > bp),
        *_boundary_rules("find_second_boundary", "labeling", "second", lambda o, bp: o < bp),
        rule(
            "match_edge", _stage("labeling"), named("f2", "edge", p1="?p1", p2="?p2", label="?label"),
            Test(("?label",), lambda l: l in ("+", "-", "B")),
            named("f3", "edge", p1="?p2", p2="?p1", label="nil"),
            action=lambda ps, b: (ps.modify(b, "f2", plotted="t"), ps.modify(b, "f3", label=b["?label"], plotted="t")),
        ),
        rule(
            "label_L", _stage("labeling"), pattern("junction", type="L", base_point="?bp"),
            pattern("edge", p1="?bp", p2="?p2", label="?label"), Test(("?label",), lambda l: l in ("+", "-")),
            named("f4", "edge", p1="?bp", p2="?p3", label="nil"), Test(("?p3", "?p2"), _ne),
            action=_labels(f4="B"),
        ),
        rule(
            "label_tee_A", _stage("labeling"),
            pattern("junction", type="tee", base_point="?bp", p1="?p1", p3="?p3"),
            named("f3", "edge", p1="?bp", p2="?p1", label="nil"), named("f4", "edge", p1="?bp", p2="?p3"),
            action=_labels(f3="B", f4="B"), salience=5,
        ),
        rule(
            "label_tee_B", _stage("labeling"),
            pattern("junction", type="tee", base_point="?bp", p1="?p1", p3="?p3"),
            named("f3", "edge", p1="?bp", p2="?p1"), named("f4", "edge", p1="?bp", p2="?p3", label="nil"),
            action=_labels(f3="B", f4="B"),
        ),
        *_fork_rules(),
        *_arrow_rules(),
        rule(
            "done_labeling", named("s", "stage", value="labeling"),
            action=lambda ps, b: _set_stage(ps, b, "plot_remaining_edges"), salience=-10,
        ),
        rule(
            "plot_remaining", _stage("plot_remaining_edges"), named("f2", "edge", plotted="nil", label="?label"),
            Test(("?label",), lambda l: l != "nil"), action=lambda ps, b: ps.modify(b, "f2", plotted="t"),
        ),
        rule(
            "plot_boundaries", _stage("plot_remaining_edges"), named("f2", "edge", plotted="nil", label="nil"),
            action=lambda ps, b: ps.modify(b, "f2", plotted="t"),
        ),
        rule(
            "done_plotting", named("s", "stage", value="plot_remaining_edges"),
            Neg("edge", ("_", "_", "_", "_", "nil")),
            action=lambda ps, b: _set_stage(ps, b, "done"), salience=-10,
        ),
    ]  # fmt: skip


class ReteWaltzEngine:
    def __init__(self) -> None:
        self.ps = ProductionSystem(waltz_rules())

    @property
    def firings(self) -> int:
        return self.ps.firings

    def run(self, lines: list[Line]) -> None:
        self.ps.assert_fact(("stage", "duplicate"))
        for p1, p2 in lines:
            self.ps.assert_fact(("line", p1, p2))
        self.ps.run()

    def state(self) -> tuple[dict[tuple[int, int], str], list[str]]:
        edges = {(f[1], f[2]): f[4] for f in self.ps.facts if f[0] == "edge"}
        junctions = [f[5] for f in self.ps.facts if f[0] == "junction"]
        return edges, junctions
