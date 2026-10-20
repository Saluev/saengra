"""
CLIPS 6.4 (Rete, written in C) via clipspy.

One rule per relationship, as in the other engines; rule actions call back into Python to
feed the shared scorer. Rules fire during `run`, which is a part of the check phase.
"""

import clips

from tests.benchmarks.train_2017.workload import Op
from tests.benchmarks.ttc_2018.scoring import ComponentsScorer, Scorer

RULES = {
    "Q1": [
        "(defrule comment (post ?c ?p) => (on-comment ?p))",
        "(defrule like (likes ?u ?c) (post ?c ?p) => (on-like ?p))",
    ],
    "Q2": [
        "(defrule liker (likes ?u ?c) => (on-liker ?c ?u))",
        "(defrule friends (likes ?u1 ?c) (friends ?u1 ?u2) (likes ?u2 ?c) => (on-friends ?c ?u1 ?u2))",
    ],
}


class ClipsEngine:
    def __init__(self, query: str) -> None:
        self.env = clips.Environment()
        self.scorer = scorer = Scorer("Post") if query == "Q1" else ComponentsScorer()
        if query == "Q1":
            self.env.define_function(lambda p: scorer.add(p, 10), "on-comment")
            self.env.define_function(lambda p: scorer.add(p, 1), "on-like")
        else:
            assert isinstance(scorer, ComponentsScorer)
            self.env.define_function(scorer.liker, "on-liker")
            self.env.define_function(scorer.friends, "on-friends")
        for rule in RULES[query]:
            self.env.build(rule)

    def _apply(self, ops: list[Op]) -> None:
        self.scorer.observe_ops(ops)
        assert_string = self.env.assert_string
        for op in ops:
            if op[0] == "E":
                assert_string(f"({op[1]} {op[2]} {op[3]})")

    def load(self, ops: list[Op]) -> None:
        self._apply(ops)

    def transform(self, ops: list[Op]) -> None:
        self._apply(ops)

    def check(self) -> str:
        self.env.run()
        return self.scorer.result()
