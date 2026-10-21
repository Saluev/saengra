"""
Waltz on Saengra: every rule of waltz.clp is an observer, stages are switched by the driver.

The drawing is a graph of points. A directed edge of the drawing is a graph edge labeled with
its Waltz label (`unlabeled`, `boundary`, `convex` or `concave`), plus an `unjoined` edge until
it becomes a part of a junction and a `plotted` edge once plotted. Junctions are vertices of
types L, Arrow, Fork and Tee with `base`, `p1`, `p2` and `p3` edges to points.

Differences from a production system, which has an agenda:
  - Rules guarded by the stage check it in their patterns, through the `Drawing` vertex all
    points belong to. Transitions to the next stage (rules with salience -10, firing when
    nothing else can) happen in the driver once a commit reaches a fixpoint.
  - Saengra invokes callbacks for all matches found by a commit iteration at once, so
    callbacks check that the edges they join or label are still unjoined or unlabeled,
    as an agenda would drop activations invalidated by earlier firings.
  - Finding the boundary junctions (the ones with the largest and the smallest base points)
    needs numeric comparisons, which patterns don't have, so the driver does it.
"""

from dataclasses import dataclass

from tests.benchmarks.waltz.workload import Line, classify_3_junction


@dataclass(frozen=True, slots=True)
class Point:
    id: int


@dataclass(frozen=True, slots=True)
class Drawing:
    id: int


@dataclass(frozen=True, slots=True)
class Stage:
    name: str


@dataclass(frozen=True, slots=True)
class L:
    base: int


@dataclass(frozen=True, slots=True)
class Arrow:
    base: int


@dataclass(frozen=True, slots=True)
class Fork:
    base: int


@dataclass(frozen=True, slots=True)
class Tee:
    base: int


JUNCTION_TYPES = {"L": L, "arrow": Arrow, "fork": Fork, "tee": Tee}
LABELS = {"B": "boundary", "+": "convex", "-": "concave", "nil": "unlabeled"}
LABEL_NAMES = {v: k for k, v in LABELS.items()}
ANY = "|".join(LABELS.values())
DRAWING = Drawing(0)
STAGES = ["duplicate", "detect_junctions", "find_boundaries", "labeling", "plot_remaining_edges", "done"]
GATE = "(?= -in> Drawing -stage> ?)"
""" Stage check, placed right after a point. """


class SaengraEngine:
    def __init__(self) -> None:
        from saengra import Environment

        self.env = Environment()
        self.firings = 0
        self.env.register_observers(self._observers())

    # ------------------------------------------------------------------------
    # Helpers for callbacks
    # ------------------------------------------------------------------------

    def _label(self, p1: Point, p2: Point) -> str:
        for edge in self.env.find_edges(from_=p1, to=p2):
            if edge.label in LABEL_NAMES:
                return edge.label
        raise AssertionError(f"no edge {p1} -> {p2}")

    def _set_label(self, p1: Point, p2: Point, label: str, *, if_unlabeled: bool = False) -> bool:
        from saengra.graph import AddEdge, RemoveEdge

        old = self._label(p1, p2)
        if if_unlabeled and old != "unlabeled":
            return False
        if old != label:
            self.env.update(RemoveEdge(p1, old, p2), AddEdge(p1, label, p2))
        return True

    def _unjoined(self, base: Point, points: tuple[Point, ...]) -> bool:
        unjoined = {e.to for e in self.env.find_edges(from_=base, with_label="unjoined")}
        return all(p in unjoined for p in points)

    def _join(self, junction: object, base: Point, points: tuple[Point, ...]) -> None:
        from saengra.graph import AddEdge, AddVertex, RemoveEdge

        updates = [AddVertex(junction), AddEdge(junction, "base", base)]
        for i, p in enumerate(points, start=1):
            updates += [AddEdge(junction, f"p{i}", p), RemoveEdge(base, "unjoined", p)]
        self.env.update_many(updates)

    # ------------------------------------------------------------------------
    # Rules
    # ------------------------------------------------------------------------

    def _observers(self) -> list:
        from saengra.graph import AddEdge, RemoveEdge
        from saengra.observer import observer

        engine = self
        observers = []

        def rule(expr: str, stage: str):
            o = observer(expr, Stage(stage), dont_register=True)
            observers.append(o)

            def register(callback):
                def counted(**refs) -> None:
                    engine.firings += 1
                    callback(**refs)

                counted.__name__ = callback.__name__
                o.on_create(counted)
                return callback

            return register

        @rule(f"Point as p1 {GATE} -line> Point as p2", "duplicate")
        def reverse_edges(p1: Point, p2: Point) -> None:
            self.env.update(
                RemoveEdge(p1, "line", p2),
                AddEdge(p1, "unlabeled", p2),
                AddEdge(p2, "unlabeled", p1),
                AddEdge(p1, "unjoined", p2),
                AddEdge(p2, "unjoined", p1),
            )

        @rule(
            f"Point as bp {GATE} (?= -unjoined> Point as p1) (?= -unjoined> (?! p1) Point as p2)"
            " (?= -unjoined> (?! p1) (?! p2) Point as p3)",
            "detect_junctions",
        )
        def make_3_junction(bp: Point, p1: Point, p2: Point, p3: Point) -> None:
            if not self._unjoined(bp, (p1, p2, p3)):
                return
            type_, barb1, shaft, barb2 = classify_3_junction(bp.id, p1.id, p2.id, p3.id)
            self._join(JUNCTION_TYPES[type_](bp.id), bp, (Point(barb1), Point(shaft), Point(barb2)))

        @rule(
            f"Point as bp {GATE} (?= -unjoined> Point as p2) (?= -unjoined> (?! p2) Point as p3)"
            f" (?! -{ANY}> (?! p2) (?! p3) Point)",
            "detect_junctions",
        )
        def make_l(bp: Point, p2: Point, p3: Point) -> None:
            if self._unjoined(bp, (p2, p3)):
                self._join(L(bp.id), bp, (p2, p3))

        for label in ("B", "+", "-"):
            name = LABELS[label]

            @rule(f"Point as p1 {GATE} -{name}> Point as p2 -unlabeled> p1", "labeling")
            def match_edge(p1: Point, p2: Point, label: str = name) -> None:
                if self._set_label(p2, p1, label, if_unlabeled=True):
                    self.env.update(AddEdge(p1, "plotted", p2), AddEdge(p2, "plotted", p1))

        @rule(f"L -base> Point as bp {GATE} (?= -convex|concave> Point as p2) -unlabeled> (?! p2) Point as p3", "labeling")
        def label_l(bp: Point, p2: Point, p3: Point) -> None:
            self._set_label(bp, p3, "boundary", if_unlabeled=True)

        tee = "Tee (?= -p1> Point as p1) (?= -p3> Point as p3) -base> Point as bp"

        @rule(f"{tee} {GATE} (?= -unlabeled> p1) (?= -{ANY}> p3)", "labeling")
        def label_tee_a(bp: Point, p1: Point, p3: Point) -> None:
            if self._set_label(bp, p1, "boundary", if_unlabeled=True):
                self._set_label(bp, p3, "boundary")

        @rule(f"{tee} {GATE} (?= -{ANY}> p1) (?= -unlabeled> p3)", "labeling")
        def label_tee_b(bp: Point, p1: Point, p3: Point) -> None:
            if self._set_label(bp, p3, "boundary", if_unlabeled=True):
                self._set_label(bp, p1, "boundary")

        fork = f"Fork -base> Point as bp {GATE}"

        @rule(
            f"{fork} (?= -convex> Point as p1) (?= -unlabeled> (?! p1) Point as p2)"
            f" (?= -{ANY}> (?! p1) (?! p2) Point as p3)",
            "labeling",
        )
        def label_fork_1(bp: Point, p1: Point, p2: Point, p3: Point) -> None:
            if self._set_label(bp, p2, "convex", if_unlabeled=True):
                self._set_label(bp, p3, "convex")

        for l1, l2, result in (("B", "-", "B"), ("B", "B", "-"), ("-", "-", "-")):

            @rule(
                f"{fork} (?= -{LABELS[l1]}> Point as p1) (?= -{LABELS[l2]}> (?! p1) Point as p2)"
                f" (?= -unlabeled> (?! p1) (?! p2) Point as p3)",
                "labeling",
            )
            def label_fork(bp: Point, p1: Point, p2: Point, p3: Point, result: str = LABELS[result]) -> None:
                self._set_label(bp, p3, result, if_unlabeled=True)

        arrow = "Arrow (?= -p1> Point as p1) (?= -p2> Point as p2) (?= -p3> Point as p3) -base> Point as bp"
        # Rules label_arrow-1A ... label_arrow-4B: known edge, its label, edges f4 and f5, their new labels;
        # None stands for the label of the known edge.
        variants = [
            (known, known_label, "p2", "p1" if known == "p3" else "p3", f4, f5)
            for known in ("p1", "p3")
            for known_label, f4, f5 in (("B", "+", None), ("-", "+", None), ("+", "-", "+"))
        ] + [("p2", "-", "p1", "p3", "+", "+")]
        for known, known_label, e4, e5, l4, l5 in variants:
            l4 = LABELS[l4]
            l5 = LABELS[known_label if l5 is None else l5]
            head = f"{arrow} {GATE} (?= -{LABELS[known_label]}> {known})"
            for unlabeled in (e4, e5):  # variants A and B
                other = e5 if unlabeled == e4 else e4

                @rule(f"{head} (?= -unlabeled> {unlabeled}) (?= -{ANY}> {other})", "labeling")
                def label_arrow(bp: Point, l4=l4, l5=l5, e4=e4, e5=e5, unlabeled=unlabeled, **refs: Point) -> None:
                    target = {"p4": refs[e4], "p5": refs[e5]}
                    first, second = ("p4", "p5") if unlabeled == e4 else ("p5", "p4")
                    labels = {"p4": l4, "p5": l5}
                    if self._set_label(bp, target[first], labels[first], if_unlabeled=True):
                        self._set_label(bp, target[second], labels[second])

        for labeled, name in (("-convex|concave|boundary", "plot_remaining"), ("-unlabeled", "plot_boundaries")):

            @rule(f"Point as p1 {GATE} {labeled}> Point as p2 (?! <plotted- p1)", "plot_remaining_edges")
            def plot(p1: Point, p2: Point) -> None:
                self.env.update(AddEdge(p1, "plotted", p2))

        return observers

    # ------------------------------------------------------------------------
    # Driver
    # ------------------------------------------------------------------------

    def _set_stage(self, stage: str, previous: str | None) -> None:
        from saengra.graph import AddEdge, AddVertex, RemoveEdge

        updates = [AddVertex(Stage(stage)), AddEdge(DRAWING, "stage", Stage(stage))]
        if previous is not None:
            updates.append(RemoveEdge(DRAWING, "stage", Stage(previous)))
        self.env.update_many(updates)

    def _label_boundary(self, junction: object) -> None:
        base = Point(junction.base)
        ends = {e.label: e.to for e in self.env.find_edges(from_=junction) if e.label in ("p1", "p2", "p3")}
        if isinstance(junction, L):
            labels = {"p1": "boundary", "p2": "boundary"}
        elif isinstance(junction, Arrow):
            labels = {"p1": "boundary", "p2": "convex", "p3": "boundary"}
        else:
            raise RuntimeError(f"boundary junction {junction} is neither an L nor an arrow")
        for role, label in labels.items():
            self._set_label(base, ends[role], label)
        self.firings += 1

    def run(self, lines: list[Line]) -> None:
        from saengra.graph import AddEdge, AddVertex

        points = sorted({p for line in lines for p in line})
        updates = [AddVertex(DRAWING)]
        for p in points:
            updates += [AddVertex(Point(p)), AddEdge(Point(p), "in", DRAWING)]
        updates += [AddEdge(Point(p1), "line", Point(p2)) for p1, p2 in lines]
        self.env.update_many(updates)

        previous = None
        for stage in STAGES:
            self._set_stage(stage, previous)
            previous = stage
            if stage == "find_boundaries":
                junctions = [j for t in JUNCTION_TYPES.values() for j in self.env.find_all_of_type(t)]
                self._label_boundary(max(junctions, key=lambda j: j.base))
                self._label_boundary(min(junctions, key=lambda j: j.base))
            self.env.commit()

    def state(self) -> tuple[dict[tuple[int, int], str], list[str]]:
        vertices, edges = self.env.find_all()
        labels = {(e.from_.id, e.to.id): LABEL_NAMES[e.label] for e in edges if e.label in LABEL_NAMES}
        junctions = [name for name, t in JUNCTION_TYPES.items() for v in vertices if isinstance(v, t)]
        return labels, junctions
