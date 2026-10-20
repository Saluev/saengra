from dataclasses import dataclass
from typing import Protocol

from tests.benchmarks.train_2017.workload import Op
from tests.benchmarks.ttc_2018.scoring import ComponentsScorer, Scorer


class TtcEngine(Protocol):
    def load(self, ops: list[Op]) -> None: ...

    def transform(self, ops: list[Op]) -> None: ...

    def check(self) -> str: ...


def create_engine(name: str, query: str) -> TtcEngine:
    match name:
        case "saengra":
            return SaengraTupleEngine(query)
        case "saengra-all":
            return SaengraAggregateEngine(query)
        case "rete-py" | "treat-py":
            return RelationalEngine(name, query)
        case "clips":
            from tests.benchmarks.ttc_2018.clips_engine import ClipsEngine

            return ClipsEngine(query)
        case _:
            raise ValueError(f"unknown engine: {name}")


def _make_scorer(query: str) -> Scorer:
    return Scorer("Post") if query == "Q1" else ComponentsScorer()


# ============================================================================
# Saengra
# ============================================================================


@dataclass(frozen=True, slots=True)
class User:
    id: int


@dataclass(frozen=True, slots=True)
class Post:
    id: int


@dataclass(frozen=True, slots=True)
class Comment:
    id: int


TYPES = {t.__name__: t for t in (User, Post, Comment)}


class _SaengraBase:
    def __init__(self, query: str) -> None:
        from saengra import Environment

        self.env = Environment()
        self.scorer = _make_scorer(query)
        self.primitives: dict[int, object] = {}

    def _apply(self, ops: list[Op]) -> None:
        from saengra.graph import AddEdge, AddVertex

        self.scorer.observe_ops(ops)
        updates = []
        primitives = self.primitives
        for op in ops:
            match op:
                case ("N", type_, id_):
                    p = primitives[id_] = TYPES[type_](id_)
                    updates.append(AddVertex(p))
                case ("E", label, a, b):
                    updates.append(AddEdge(primitives[a], label, primitives[b]))
                case ("A", _, _, _):
                    pass  # only timestamps, which are used by the scorer
                case _:
                    raise ValueError(f"unexpected change: {op}")
        self.env.update_many(updates)
        self.env.commit()

    def load(self, ops: list[Op]) -> None:
        self._apply(ops)

    def transform(self, ops: list[Op]) -> None:
        self._apply(ops)

    def check(self) -> str:
        return self.scorer.result()


def _no_deletions(**refs: object) -> None:
    raise AssertionError(f"change sequences are insert-only, but a match disappeared: {refs}")


class SaengraTupleEngine(_SaengraBase):
    """One observer per relationship, the scorer aggregates individual matches."""

    def __init__(self, query: str) -> None:
        from saengra.observer import observer

        super().__init__(query)
        scorer = self.scorer
        if query == "Q1":
            comments = observer("Comment as c -post> Post as p", dont_register=True)
            likes = observer("User as u -likes> Comment as c -post> Post as p", dont_register=True)

            @comments.on_create
            def on_comment(c: Comment, p: Post) -> None:
                scorer.add(p.id, 10)

            @likes.on_create
            def on_like(u: User, c: Comment, p: Post) -> None:
                scorer.add(p.id, 1)

            observers = [comments, likes]
        else:
            assert isinstance(scorer, ComponentsScorer)
            likers = observer("User as u -likes> Comment as c", dont_register=True)
            friends = observer(
                "Comment as c <likes- User as u1 -friends> User as u2 (?= -likes> c)",
                dont_register=True,
            )

            @likers.on_create
            def on_liker(u: User, c: Comment) -> None:
                scorer.liker(c.id, u.id)

            @friends.on_create
            def on_friends(c: Comment, u1: User, u2: User) -> None:
                scorer.friends(c.id, u1.id, u2.id)

            observers = [likers, friends]
        for o in observers:
            o.on_delete(_no_deletions)
        self.env.register_observers(observers)


class SaengraAggregateEngine(_SaengraBase):
    """
    One `(all:)` observer per scored entity, as Saengra is meant to be used. Observer callbacks
    only receive refs, not the matched subgraph, so the handler has to match it again.
    """

    def __init__(self, query: str) -> None:
        from saengra.observer import observer

        super().__init__(query)
        env, scorer = self.env, self.scorer
        if query == "Q1":
            body = "(all: <post- Comment (maybe: <likes- User))"
            posts = observer(f"Post as p {body}", dont_register=True)

            @posts.on_create_or_change
            def on_post_changed(p: Post) -> None:
                (sg,) = env.match_subgrapghs(f"? {body}", p)
                comments = sum(1 for v in sg.vertices if isinstance(v, Comment))
                likes = sum(1 for e in sg.edges if e.label == "likes")
                scorer.set(p.id, 10 * comments + likes)

            observers = [posts]
        else:
            body = "(all: <likes- User (maybe: -friends> User (?= -likes> c)))"
            comments = observer(f"Comment as c {body}", dont_register=True)

            @comments.on_create_or_change
            def on_comment_changed(c: Comment) -> None:
                (sg,) = env.match_subgrapghs(f"? as c {body}", c)
                local = ComponentsScorer()
                for v in sg.vertices:
                    if isinstance(v, User):
                        local.liker(c.id, v.id)
                for e in sg.edges:
                    if e.label == "friends":
                        local.friends(c.id, e.from_.id, e.to.id)
                scorer.set(c.id, local.scores.get(c.id, 0))

            observers = [comments]
        for o in observers:
            o.on_delete(_no_deletions)
        self.env.register_observers(observers)


# ============================================================================
# Reference Rete and TREAT (see train_2017/engines/relational.py)
# ============================================================================


class RelationalEngine:
    def __init__(self, name: str, query: str) -> None:
        from tests.benchmarks.train_2017.engines.relational import Pos, ReteEngine, TreatEngine

        self.scorer = scorer = _make_scorer(query)
        if query == "Q1":
            queries = {
                "comment": [Pos("post", ("?c", "?p"))],
                "like": [Pos("likes", ("?u", "?c")), Pos("post", ("?c", "?p"))],
            }

            def listener(q: str, match: tuple, sign: int) -> None:
                assert sign > 0, "change sequences are insert-only"
                if q == "comment":
                    scorer.add(match[1], 10)  # (c, p)
                else:
                    scorer.add(match[2], 1)  # (u, c, p)

        else:
            assert isinstance(scorer, ComponentsScorer)
            queries = {
                "liker": [Pos("likes", ("?u", "?c"))],
                "friends": [
                    Pos("likes", ("?u1", "?c")),
                    Pos("friends", ("?u1", "?u2")),
                    Pos("likes", ("?u2", "?c")),
                ],
            }

            def listener(q: str, match: tuple, sign: int) -> None:
                assert sign > 0, "change sequences are insert-only"
                if q == "liker":
                    scorer.liker(match[1], match[0])  # (u, c)
                else:
                    scorer.friends(match[1], match[0], match[2])  # (u1, c, u2)

        engine_type = ReteEngine if name == "rete-py" else TreatEngine
        self.engine = engine_type(queries, listener)

    def load(self, ops: list[Op]) -> None:
        self.scorer.observe_ops(ops)
        self.engine.load(ops)

    def transform(self, ops: list[Op]) -> None:
        self.scorer.observe_ops(ops)
        self.engine.transform(ops)

    def check(self) -> str:
        return self.scorer.result()
