"""
CLIPS 6.4 (Rete, written in C) via clipspy, running the benchmark's own CLIPS program
(waltz.clp) entirely inside CLIPS.
"""

from pathlib import Path

import clips

from tests.benchmarks.waltz.workload import Line

RULES = Path(__file__).resolve().parent / "waltz.clp"


class ClipsEngine:
    def __init__(self) -> None:
        self.env = clips.Environment()
        self.env.load(str(RULES))
        self.firings = 0

    def run(self, lines: list[Line]) -> None:
        self.env.assert_string("(stage (value duplicate))")
        line = self.env.find_template("line")
        for p1, p2 in lines:
            line.assert_fact(p1=p1, p2=p2)
        self.firings = self.env.run()

    def state(self) -> tuple[dict[tuple[int, int], str], list[str]]:
        edges: dict[tuple[int, int], str] = {}
        junctions: list[str] = []
        for fact in self.env.facts():
            name = fact.template.name
            if name == "edge":
                edges[fact["p1"], fact["p2"]] = str(fact["label"])
            elif name == "junction":
                junctions.append(str(fact["type"]))
        return edges, junctions
