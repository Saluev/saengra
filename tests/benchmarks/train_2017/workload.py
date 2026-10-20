"""
Train Benchmark model generator, reference query evaluator and workload traces.

The generator is a port of `ScalableModelGenerator` from https://github.com/FTSRG/trainbenchmark
(uses bit-exact `java.util.Random`, the same seeds and the same ID assignment), and the
Inject/Repair workloads follow `BenchmarkScript.groovy` and the EMF reference transformations.

Every engine is fed with exactly the same sequence of elementary graph operations (a trace),
and must produce exactly the same number of matches for every query after every step.
The candidates for transformations are selected using the reference evaluator, which is not
timed: in the original benchmark, candidate selection is not timed as well.

Differences from the original:
* `Region` containment edges (`elements`, `sensors`) and `semaphores` edges are not emitted
  since no query uses them (IDs are still allocated for regions to keep IDs identical).
* `ConnectedSegments` repair removes edges of the bypassed segment, but keeps the vertex
  (the EMF reference implementation does the same; Cypher one removes it).
"""

from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Iterable, TypeAlias

from .javarandom import JavaRandom

RANDOM_SEED = 19871053
DEFAULT_SEGMENT_LENGTH = 500
POSITIONS = ("FAILURE", "STRAIGHT", "DIVERGING")

QUERIES = (
    "ConnectedSegments",
    "PosLength",
    "RouteSensor",
    "SemaphoreNeighbor",
    "SwitchMonitored",
    "SwitchSet",
)

# Elementary operations:
#   ("N", type, id)             — add vertex
#   ("E", label, from, to)      — add edge
#   ("R", label, from, to)      — remove edge
#   ("A", attribute, id, value) — set (single-valued) attribute, value is int, str or bool
Op: TypeAlias = tuple
Match: TypeAlias = tuple[int, ...]


@dataclass
class Workload:
    name: str
    size: int
    load: list[Op]
    transformations: list[list[Op]]
    expected: list[dict[str, int]]
    """ Expected number of matches after load and after every transformation. """

    @property
    def num_elements(self) -> int:
        return sum(1 for op in self.load if op[0] in ("N", "E"))


# ============================================================================
# Reference model
# ============================================================================


@dataclass
class Model:
    types: dict[int, str] = field(default_factory=dict)
    out: dict[str, dict[int, set[int]]] = field(
        default_factory=lambda: defaultdict(lambda: defaultdict(set))
    )
    inc: dict[str, dict[int, set[int]]] = field(
        default_factory=lambda: defaultdict(lambda: defaultdict(set))
    )
    attrs: dict[str, dict[int, object]] = field(
        default_factory=lambda: defaultdict(dict)
    )
    by_type: dict[str, list[int]] = field(default_factory=lambda: defaultdict(list))
    next_id: int = 1

    def apply(self, op: Op) -> None:
        match op:
            case ("N", type_, id_):
                self.types[id_] = type_
                self.by_type[type_].append(id_)
                self.next_id = max(self.next_id, id_ + 1)
            case ("E", label, a, b):
                self.out[label][a].add(b)
                self.inc[label][b].add(a)
            case ("R", label, a, b):
                self.out[label][a].discard(b)
                self.inc[label][b].discard(a)
            case ("A", attr, id_, value):
                self.attrs[attr][id_] = value
            case _:
                raise ValueError(op)

    def is_(self, id_: int, type_: str) -> bool:
        return self.types.get(id_) == type_


class OpSink:
    """Applies operations to the reference model and records them."""

    def __init__(self, model: Model) -> None:
        self.model = model
        self.ops: list[Op] = []

    def __call__(self, *op: object) -> None:
        self.model.apply(op)
        self.ops.append(op)

    def new_vertex(self, type_: str) -> int:
        id_ = self.model.next_id
        self("N", type_, id_)
        return id_


# ============================================================================
# Generator
# ============================================================================


def generate(size: int, scenario: str) -> tuple[Model, list[Op]]:
    error_percentages = {
        "batch": dict(cs=0, pl=0, rs=0, sn=0, sm=0, ss=0, rr=0),
        "inject": dict(cs=5, pl=2, rs=4, sn=7, sm=2, ss=8, rr=2),
        "repair": dict(cs=5, pl=10, rs=10, sn=25, sm=18, ss=15, rr=1),
    }[scenario]

    connected_segment_random = JavaRandom(RANDOM_SEED + 0)
    pos_length_random = JavaRandom(RANDOM_SEED + 1)
    route_sensor_random = JavaRandom(RANDOM_SEED + 2)
    semaphore_neighbor_random = JavaRandom(RANDOM_SEED + 3)
    switch_monitored_random = JavaRandom(RANDOM_SEED + 4)
    switch_set_random = JavaRandom(RANDOM_SEED + 5)
    route_reachability_random = JavaRandom(RANDOM_SEED + 8)
    length_random = JavaRandom(RANDOM_SEED)
    positions_random = JavaRandom(RANDOM_SEED)
    sensors_random = JavaRandom(RANDOM_SEED)
    swps_random = JavaRandom(RANDOM_SEED)

    max_segments = 5
    max_routes = 5 * size
    max_switch_positions = 20
    max_sensors = 10

    model = Model()
    emit = OpSink(model)

    def create_segment(current_track: list[int], sensor: int) -> int:
        pos_length_error = pos_length_random.next_int(100) < error_percentages["pl"]
        length = (-1 if pos_length_error else 1) * length_random.next_int(1000) + 1
        segment = emit.new_vertex("Segment")
        emit("A", "length", segment, length)
        emit("E", "monitoredBy", segment, sensor)
        current_track.append(segment)
        return segment

    prev_semaphore = first_semaphore = None
    first_tracks: list[int] = []
    prev_tracks: list[int] | None = None
    for i in range(max_routes):
        if prev_semaphore is None:
            prev_semaphore = emit.new_vertex("Semaphore")
            emit("A", "signal", prev_semaphore, "GO")
            first_semaphore = prev_semaphore

        if i != max_routes - 1:
            semaphore = emit.new_vertex("Semaphore")
            emit("A", "signal", semaphore, "GO")
        else:
            semaphore = first_semaphore

        semaphore_neighbor_error = (
            semaphore_neighbor_random.next_int(100) < error_percentages["sn"]
        )
        route = emit.new_vertex("Route")
        emit("A", "active", route, True)
        if not semaphore_neighbor_error:
            emit("E", "entry", route, prev_semaphore)
        emit("E", "exit", route, semaphore)
        model.next_id += 1  # region

        swps = swps_random.next_int(max_switch_positions - 1) + 1
        current_track: list[int] = []
        for _ in range(swps):
            position_ordinal = positions_random.next_int(len(POSITIONS))
            sw = emit.new_vertex("Switch")
            emit("A", "currentPosition", sw, POSITIONS[position_ordinal])
            current_track.append(sw)

            sensors = sensors_random.next_int(max_sensors - 1) + 1
            for _ in range(sensors):
                sensor = emit.new_vertex("Sensor")
                switch_monitored_error = (
                    switch_monitored_random.next_int(100) < error_percentages["sm"]
                )
                if not switch_monitored_error:
                    emit("E", "monitoredBy", sw, sensor)
                    route_sensor_error = (
                        route_sensor_random.next_int(100) < error_percentages["rs"]
                    )
                    if not route_sensor_error:
                        emit("E", "requires", route, sensor)

                for _ in range(max_segments):
                    create_segment(current_track, sensor)

                if connected_segment_random.next_int(100) < error_percentages["cs"]:
                    create_segment(current_track, sensor)

            switch_set_error = switch_set_random.next_int(100) < error_percentages["ss"]
            invalid_ordinal = (
                (len(POSITIONS) - 1) - position_ordinal
                if switch_set_error
                else position_ordinal
            )
            swp = emit.new_vertex("SwitchPosition")
            emit("A", "position", swp, POSITIONS[invalid_ordinal])
            emit("E", "target", swp, sw)
            emit("E", "follows", route, swp)

        for j in range(1, len(current_track)):
            route_reachability_error = (
                route_reachability_random.next_int(1000) < error_percentages["rr"]
            )
            if not route_reachability_error:
                emit("E", "connectsTo", current_track[j - 1], current_track[j])

        if prev_tracks and current_track:
            emit("E", "connectsTo", prev_tracks[-1], current_track[0])

        if i == max_routes - 1 and current_track and first_tracks:
            emit("E", "connectsTo", current_track[-1], first_tracks[0])

        if prev_tracks is None:
            first_tracks = current_track
        prev_tracks = current_track
        prev_semaphore = semaphore

    return model, emit.ops


# ============================================================================
# Reference queries
# ============================================================================


def q_pos_length(m: Model) -> list[Match]:
    return [(s,) for s, length in m.attrs["length"].items() if length <= 0]


def q_switch_monitored(m: Model) -> list[Match]:
    mon = m.out["monitoredBy"]
    return [(sw,) for sw in m.by_type["Switch"] if not mon.get(sw)]


def q_route_sensor(m: Model) -> list[Match]:
    result = []
    for route, swps in m.out["follows"].items():
        requires = m.out["requires"].get(route, ())
        for swp in swps:
            for sw in m.out["target"].get(swp, ()):
                for sensor in m.out["monitoredBy"].get(sw, ()):
                    if sensor not in requires:
                        result.append((route, sensor, swp, sw))
    return result


def q_switch_set(m: Model) -> list[Match]:
    result = []
    signal = m.attrs["signal"]
    active = m.attrs["active"]
    current_position = m.attrs["currentPosition"]
    position = m.attrs["position"]
    for semaphore, routes in m.inc["entry"].items():
        if signal.get(semaphore) != "GO":
            continue
        for route in routes:
            if active.get(route) is not True:
                continue
            for swp in m.out["follows"].get(route, ()):
                for sw in m.out["target"].get(swp, ()):
                    if current_position.get(sw) != position.get(swp):
                        result.append((semaphore, route, swp, sw))
    return result


def q_connected_segments(m: Model) -> list[Match]:
    result = []
    mon = m.out["monitoredBy"]
    conn = m.out["connectsTo"]

    def extend(path: list[int], sensor: int) -> None:
        if len(path) == 6:
            result.append((sensor, *path))
            return
        for nxt in conn.get(path[-1], ()):
            if m.is_(nxt, "Segment") and sensor in mon.get(nxt, ()):
                path.append(nxt)
                extend(path, sensor)
                path.pop()

    for sensor, segments in m.inc["monitoredBy"].items():
        if not m.is_(sensor, "Sensor"):
            continue
        for s1 in segments:
            if m.is_(s1, "Segment"):
                extend([s1], sensor)
    return result


def q_semaphore_neighbor(m: Model) -> list[Match]:
    result = []
    entry = m.out["entry"]
    for semaphore, routes1 in m.inc["exit"].items():
        for route1 in routes1:
            for sensor1 in m.out["requires"].get(route1, ()):
                for te1 in m.inc["monitoredBy"].get(sensor1, ()):
                    for te2 in m.out["connectsTo"].get(te1, ()):
                        for sensor2 in m.out["monitoredBy"].get(te2, ()):
                            for route2 in m.inc["requires"].get(sensor2, ()):
                                if route1 == route2:
                                    continue
                                if semaphore in entry.get(route2, ()):
                                    continue
                                result.append(
                                    (
                                        semaphore,
                                        route1,
                                        route2,
                                        sensor1,
                                        sensor2,
                                        te1,
                                        te2,
                                    )
                                )
    return result


CHECK_QUERIES: dict[str, Callable[[Model], list[Match]]] = {
    "ConnectedSegments": q_connected_segments,
    "PosLength": q_pos_length,
    "RouteSensor": q_route_sensor,
    "SemaphoreNeighbor": q_semaphore_neighbor,
    "SwitchMonitored": q_switch_monitored,
    "SwitchSet": q_switch_set,
}


def q_connected_segments_inject(m: Model) -> list[Match]:
    result = []
    mon = m.out["monitoredBy"]
    for s1 in m.by_type["Segment"]:
        for s3 in m.out["connectsTo"].get(s1, ()):
            if not m.is_(s3, "Segment"):
                continue
            for sensor in mon.get(s1, ()):
                if sensor in mon.get(s3, ()):
                    result.append((sensor, s1, s3))
    return result


def q_pos_length_inject(m: Model) -> list[Match]:
    return [(s,) for s in m.by_type["Segment"]]


def q_route_sensor_inject(m: Model) -> list[Match]:
    return [(r, s) for r, sensors in m.out["requires"].items() for s in sensors]


def q_semaphore_neighbor_inject(m: Model) -> list[Match]:
    return [(r, s) for r, semaphores in m.out["entry"].items() for s in semaphores]


def q_switch_inject(m: Model) -> list[Match]:
    return [(sw,) for sw in m.by_type["Switch"]]


# ============================================================================
# Transformations (EMF reference semantics)
# ============================================================================


def t_connected_segments_inject(emit: OpSink, sensor: int, s1: int, s3: int) -> None:
    s2 = emit.new_vertex("Segment")
    emit("A", "length", s2, DEFAULT_SEGMENT_LENGTH)
    emit("R", "connectsTo", s1, s3)
    emit("E", "connectsTo", s1, s2)
    emit("E", "connectsTo", s2, s3)
    emit("E", "monitoredBy", s2, sensor)


def t_pos_length_inject(emit: OpSink, segment: int) -> None:
    emit("A", "length", segment, 0)


def t_route_sensor_inject(emit: OpSink, route: int, sensor: int) -> None:
    emit("R", "requires", route, sensor)


def t_semaphore_neighbor_inject(emit: OpSink, route: int, semaphore: int) -> None:
    if semaphore in emit.model.out["entry"].get(route, ()):
        emit("R", "entry", route, semaphore)


def t_switch_set_inject(emit: OpSink, sw: int) -> None:
    current = POSITIONS.index(emit.model.attrs["currentPosition"][sw])
    emit("A", "currentPosition", sw, POSITIONS[(current + 1) % len(POSITIONS)])


def t_switch_monitored_inject(emit: OpSink, sw: int) -> None:
    for sensor in sorted(emit.model.out["monitoredBy"].get(sw, ())):
        emit("R", "monitoredBy", sw, sensor)


def t_connected_segments_repair(emit: OpSink, sensor: int, s1: int, s2: int, s3: int, *_: int) -> None:
    emit("R", "connectsTo", s1, s2)
    emit("E", "connectsTo", s1, s3)
    for te in sorted(emit.model.out["connectsTo"].get(s2, ())):
        emit("R", "connectsTo", s2, te)
    for se in sorted(emit.model.out["monitoredBy"].get(s2, ())):
        emit("R", "monitoredBy", s2, se)


def t_pos_length_repair(emit: OpSink, segment: int) -> None:
    emit("A", "length", segment, -emit.model.attrs["length"][segment] + 1)


def t_route_sensor_repair(emit: OpSink, route: int, sensor: int, *_: int) -> None:
    emit("E", "requires", route, sensor)


def t_semaphore_neighbor_repair(emit: OpSink, semaphore: int, route1: int, route2: int, *_: int) -> None:
    for old in sorted(emit.model.out["entry"].get(route2, ())):
        emit("R", "entry", route2, old)
    emit("E", "entry", route2, semaphore)


def t_switch_set_repair(emit: OpSink, semaphore: int, route: int, swp: int, sw: int) -> None:
    emit("A", "currentPosition", sw, emit.model.attrs["position"][swp])


def t_switch_monitored_repair(emit: OpSink, sw: int) -> None:
    sensor = emit.new_vertex("Sensor")
    emit("E", "monitoredBy", sw, sensor)


INJECT_OPERATIONS = [
    (q_connected_segments_inject, t_connected_segments_inject),
    (q_pos_length_inject, t_pos_length_inject),
    (q_route_sensor_inject, t_route_sensor_inject),
    (q_semaphore_neighbor_inject, t_semaphore_neighbor_inject),
    (q_switch_inject, t_switch_set_inject),
    (q_switch_inject, t_switch_monitored_inject),
]

REPAIR_OPERATIONS = [
    ("ConnectedSegments", t_connected_segments_repair),
    ("PosLength", t_pos_length_repair),
    ("RouteSensor", t_route_sensor_repair),
    ("SemaphoreNeighbor", t_semaphore_neighbor_repair),
    ("SwitchSet", t_switch_set_repair),
    ("SwitchMonitored", t_switch_monitored_repair),
]


def check_all(model: Model) -> dict[str, list[Match]]:
    return {name: query(model) for name, query in CHECK_QUERIES.items()}


def build_workload(name: str, size: int) -> Workload:
    """
    `name` is "inject" (fixed change set of 10 elements, 12 iterations)
    or "repair" (5% of matches, 8 iterations), as in the original `BenchmarkScript.groovy`.
    """
    model, load_ops = generate(size, name)
    random = JavaRandom(RANDOM_SEED)
    matches = check_all(model)
    expected = [{q: len(ms) for q, ms in matches.items()}]
    transformations: list[list[Op]] = []

    iterations = {"inject": 12, "repair": 8}[name]
    for _ in range(iterations):
        emit = OpSink(model)
        if name == "inject":
            plan = []
            for query, transformation in INJECT_OPERATIONS:
                candidates = sorted(query(model))
                random.shuffle(candidates)
                plan.append((transformation, candidates[:10]))
        else:
            plan = []
            for query_name, transformation in REPAIR_OPERATIONS:
                candidates = sorted(matches[query_name])
                n = len(candidates) * 5 // 100
                random.shuffle(candidates)
                plan.append((transformation, candidates[:n]))
        for transformation, candidates in plan:
            for candidate in candidates:
                transformation(emit, *candidate)
        transformations.append(emit.ops)
        matches = check_all(model)
        expected.append({q: len(ms) for q, ms in matches.items()})

    return Workload(name, size, load_ops, transformations, expected)


# ============================================================================
# Trace serialization (consumed by out-of-process engines, e.g. Drools in Docker)
# ============================================================================


def _format_value(value: object) -> str:
    if isinstance(value, bool):
        return f"b:{'true' if value else 'false'}"
    if isinstance(value, int):
        return f"i:{value}"
    return f"s:{value}"


def _parse_value(token: str) -> object:
    kind, raw = token.split(":", 1)
    if kind == "b":
        return raw == "true"
    if kind == "i":
        return int(raw)
    return raw


def _format_ops(ops: Iterable[Op]) -> Iterable[str]:
    for op in ops:
        if op[0] == "A":
            yield f"A {op[1]} {op[2]} {_format_value(op[3])}\n"
        else:
            yield " ".join(map(str, op)) + "\n"


def write_trace(workload: Workload, path: Path) -> None:
    """
    Format: `LOAD`, then operations, then `CHECK` followed by expected counts;
    then `TRANSFORM`, operations and `CHECK` for every iteration.
    """

    def check_line(expected: dict[str, int]) -> str:
        return "CHECK " + " ".join(f"{q}={expected[q]}" for q in QUERIES) + "\n"

    with path.open("w") as f:
        f.write(f"# train-2017 {workload.name} size={workload.size}\n")
        f.write("LOAD\n")
        f.writelines(_format_ops(workload.load))
        f.write(check_line(workload.expected[0]))
        for ops, expected in zip(workload.transformations, workload.expected[1:]):
            f.write("TRANSFORM\n")
            f.writelines(_format_ops(ops))
            f.write(check_line(expected))


def read_trace(path: Path) -> Workload:
    load: list[Op] = []
    transformations: list[list[Op]] = []
    expected: list[dict[str, int]] = []
    current = load
    name, size = "?", 0
    with path.open() as f:
        for line in f:
            parts = line.split()
            match parts[0]:
                case "#":
                    name, size = parts[2], int(parts[3].removeprefix("size="))
                case "LOAD":
                    current = load
                case "TRANSFORM":
                    current = []
                    transformations.append(current)
                case "CHECK":
                    expected.append(
                        {k: int(v) for k, v in (p.split("=") for p in parts[1:])}
                    )
                case "N":
                    current.append(("N", parts[1], int(parts[2])))
                case "E" | "R":
                    current.append((parts[0], parts[1], int(parts[2]), int(parts[3])))
                case "A":
                    current.append(("A", parts[1], int(parts[2]), _parse_value(parts[3])))
    return Workload(name, size, load, transformations, expected)
