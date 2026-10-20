"""
CLIPS 6.4 (Rete, written in C) via clipspy.

Match sets are maintained idiomatically with truth maintenance: every rule asserts a match
fact with logical support, which CLIPS retracts automatically once the rule's LHS stops
matching. The (C-side) `run` that fires the rules is a part of the check phase.
"""

import clips

from ..workload import Op, QUERIES

SEGMENT_CHAIN = "(monitoredBy ?s1 ?sen) (Segment ?s1)" + "".join(
    f" (connectsTo ?s{i - 1} ?s{i}) (Segment ?s{i}) (monitoredBy ?s{i} ?sen)"
    for i in range(2, 7)
)

# Type patterns implied by the metamodel are omitted, same as in the relational reference engines.
RULES = f"""
(defrule PosLength
  (logical (length ?s ?l&:(<= ?l 0)))
  => (assert (PosLengthMatch ?s)))

(defrule SwitchMonitored
  (logical (Switch ?sw) (not (monitoredBy ?sw ?)))
  => (assert (SwitchMonitoredMatch ?sw)))

(defrule RouteSensor
  (logical (follows ?r ?swP) (target ?swP ?sw) (monitoredBy ?sw ?sen) (not (requires ?r ?sen)))
  => (assert (RouteSensorMatch ?r ?sen ?swP ?sw)))

(defrule SwitchSet
  (logical (signal ?sem GO) (entry ?r ?sem) (active ?r TRUE) (follows ?r ?swP) (target ?swP ?sw)
           (currentPosition ?sw ?cp) (position ?swP ?p&~?cp))
  => (assert (SwitchSetMatch ?sem ?r ?swP ?sw)))

(defrule ConnectedSegments
  (logical {SEGMENT_CHAIN})
  => (assert (ConnectedSegmentsMatch ?sen ?s1 ?s2 ?s3 ?s4 ?s5 ?s6)))

(defrule SemaphoreNeighbor
  (logical (exit ?r1 ?sem) (requires ?r1 ?sen1) (monitoredBy ?te1 ?sen1) (connectsTo ?te1 ?te2)
           (monitoredBy ?te2 ?sen2) (requires ?r2&~?r1 ?sen2) (not (entry ?r2 ?sem)))
  => (assert (SemaphoreNeighborMatch ?sem ?r1 ?r2 ?sen1 ?sen2 ?te1 ?te2)))
"""


def _value(value: object) -> str:
    if isinstance(value, bool):
        return "TRUE" if value else "FALSE"
    return str(value)


class ClipsEngine:
    name = "CLIPS 6.4"
    algorithm = "Rete"
    language = "C"

    def __init__(self) -> None:
        self.env = clips.Environment()
        for statement in RULES.split("\n\n"):
            if statement.strip():
                self.env.build(statement.strip())
        self.facts: dict[str, clips.Fact] = {}
        self.attributes: dict[tuple[str, int], str] = {}

    def _apply(self, ops: list[Op]) -> None:
        facts = self.facts
        assert_string = self.env.assert_string
        for op in ops:
            match op:
                case ("N", type_, id_):
                    assert_string(f"({type_} {id_})")
                case ("E", label, a, b):
                    key = f"({label} {a} {b})"
                    facts[key] = assert_string(key)
                case ("R", label, a, b):
                    fact = facts.pop(f"({label} {a} {b})", None)
                    if fact is not None:
                        fact.retract()
                case ("A", attr, id_, value):
                    old = self.attributes.pop((attr, id_), None)
                    if old is not None:
                        facts.pop(old).retract()
                    key = self.attributes[attr, id_] = f"({attr} {id_} {_value(value)})"
                    facts[key] = assert_string(key)

    def load(self, ops: list[Op]) -> None:
        self._apply(ops)

    def transform(self, ops: list[Op]) -> None:
        self._apply(ops)

    def check(self) -> dict[str, int]:
        self.env.run()
        return {
            q: int(self.env.eval(f"(length$ (find-all-facts ((?m {q}Match)) TRUE))"))
            for q in QUERIES
        }
