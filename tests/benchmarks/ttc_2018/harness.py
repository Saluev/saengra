import csv
import hashlib
import json
import statistics
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable

from tests.benchmarks.train_2017.harness import REPO_ROOT, _fmt_mem, _fmt_time
from tests.benchmarks.train_2017.workload import Op
from tests.benchmarks.ttc_2018.workload import Workload

DOCKER_DIR = Path(__file__).resolve().parent / "docker"


@dataclass(frozen=True)
class EngineInfo:
    title: str
    approach: str
    language: str
    image: str | None = None
    """ Docker image directory in `docker/`, or None for engines that run natively. """
    official: bool = False
    """ Official contest solution, run with the contest's own harness protocol. """
    commands: dict[str, list[str]] = field(default_factory=dict)
    """ Official solutions: command per query. """
    env: dict[str, str] = field(default_factory=dict)
    """ Official solutions: extra environment variables. """


ENGINES: dict[str, EngineInfo] = {
    "saengra": EngineInfo("Saengra", "tuple observers", "C++ / Py callbacks"),
    "saengra-all": EngineInfo("Saengra (all:)", "(all:) + re-query", "C++ / Py callbacks"),
    "rete-py": EngineInfo("Rete (ref.)", "Rete", "Python"),
    "treat-py": EngineInfo("TREAT (ref.)", "TREAT", "Python"),
    "clips": EngineInfo("CLIPS 6.4", "Rete", "C / Py callbacks", image="rules"),
    "drools": EngineInfo("Drools 8.44", "Phreak (lazy Rete)", "Java 21", image="rules"),
    "differential": EngineInfo(
        "Differential", "differential dataflow", "Rust", image="differential", official=True,
        commands={q: ["/opt/differential", "HARNESS"] for q in ("Q1", "Q2")},
    ),
    "nmf": EngineInfo(
        "NMF Incremental", "incremental model queries", "C# / .NET Core 3.1", image="nmf", official=True,
        commands={q: ["dotnet", "/opt/nmf/NMFSolution.dll", "incremental"] for q in ("Q1", "Q2")},
    ),
    "GB-Incr-x1": EngineInfo(
        "GraphBLAS Incr.", "sparse linear algebra", "C / SuiteSparse", image="graphblas", official=True,
        commands={q: ["/opt/ttc"] for q in ("Q1", "Q2")}, env={"ThreadsNum": "1"},
    ),
    "ddlog": EngineInfo(
        "Diff. Datalog", "incremental Datalog", "DDlog → Rust", image="ddlog", official=True,
        commands={"Q1": ["/opt/snq1", "HARNESS"], "Q2": ["/opt/snq2", "HARNESS"]},
    ),
}  # fmt: skip

OFFICIAL_SOLUTIONS_NOTE = (
    "Differential, NMF, GraphBLAS and Differential Datalog are official contest solutions; their initial time excludes\n"
    "their own Load phase, which for GraphBLAS also builds its matrices"
)


@dataclass
class RunResult:
    initial: float
    updates: list[float]
    results: list[str]
    memory: int | None


@dataclass
class Measurement:
    engine: str
    query: str
    size: int
    elements: int
    runs: list[RunResult] = field(default_factory=list)
    error: str | None = None
    mismatch: str | None = None

    def median(self, fn) -> float:
        return statistics.median(fn(r) for r in self.runs)


def build_docker_image(name: str, repository: Path) -> str:
    """
    Builds the image from `docker/NAME`, tagged by the hash of its sources. Official solutions
    are built from the contest repository's `solutions` directory, the rest from their own one.
    """
    directory = DOCKER_DIR / name
    official = any(info.official for info in ENGINES.values() if info.image == name)
    digest = hashlib.sha256()
    for path in sorted(directory.rglob("*")):
        if path.is_file():
            digest.update(str(path.relative_to(directory)).encode())
            digest.update(path.read_bytes())
    tag = f"saengra-ttc2018-{name}:{digest.hexdigest()[:12]}"
    if subprocess.run(["docker", "image", "inspect", tag], capture_output=True).returncode != 0:
        print(f"Building {tag} (may take a while)...", flush=True)
        context = repository / "solutions" if official else directory
        subprocess.run(["docker", "build", "-t", tag, "-f", str(directory / "Dockerfile"), str(context)], check=True)
    return tag


def write_trace(workload: Workload, path: Path) -> None:
    """Format: `LOAD`, then operations; `CHANGE` and operations for every change set."""

    def lines(ops: list[Op]) -> Iterable[str]:
        return (" ".join(str(x) for x in op) + "\n" for op in ops)

    with path.open("w") as f:
        f.write("LOAD\n")
        f.writelines(lines(workload.load))
        for ops in workload.changes:
            f.write("CHANGE\n")
            f.writelines(lines(ops))


def _parse_json_result(stdout: str) -> RunResult:
    # Engines may print warnings around the result (e.g. CLIPS does on exit).
    data = json.loads(next(l for l in reversed(stdout.splitlines()) if l.startswith("{")))
    return RunResult(data["initial"], data["updates"], data["results"], data["mem_peak"] - data["mem_baseline"])


def run_engine(
    engine: str, query: str, size: int, repository: Path, *, images: dict[str, str], trace: Path, timeout: float
) -> RunResult:
    info = ENGINES[engine]
    if info.image is None:
        cmd = [sys.executable, "-m", "tests.benchmarks.ttc_2018.runner", engine, query, str(repository), str(size)]
        proc = subprocess.run(cmd, cwd=REPO_ROOT, capture_output=True, text=True, timeout=timeout)
        if proc.returncode != 0:
            raise RuntimeError(proc.stderr.strip().splitlines()[-1] if proc.stderr.strip() else f"exit code {proc.returncode}")
        return _parse_json_result(proc.stdout)

    image = images[info.image]
    if not info.official:
        docker = [
            "docker", "run", "--rm",
            "-v", f"{repository}:/ttc:ro",
            "-v", f"{trace.parent}:/data:ro",
            "-v", f"{REPO_ROOT / 'tests'}:/repo/tests:ro",
            "-w", "/repo",
            image,
        ]  # fmt: skip
        if engine == "drools":
            cmd = docker + ["java", "-jar", "/opt/drools-ttc.jar", query, f"/data/{trace.name}"]
        else:
            cmd = docker + ["python3", "-m", "tests.benchmarks.ttc_2018.runner", engine, query, "/ttc", str(size)]
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        if proc.returncode != 0:
            raise RuntimeError(proc.stderr.strip().splitlines()[-1] if proc.stderr.strip() else f"exit code {proc.returncode}")
        return _parse_json_result(proc.stdout)

    env = {"ChangePath": f"/models/{size}", "ChangeSet": str(size), "Query": query,
           "RunIndex": "0", "Sequences": "20", "Tool": engine, **info.env}  # fmt: skip
    cmd = ["docker", "run", "--rm", "-v", f"{repository / 'models'}:/models:ro"]
    for k, v in env.items():
        cmd += ["-e", f"{k}={v}"]
    cmd += [image, *info.commands[query]]
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    if proc.returncode != 0:
        raise RuntimeError(f"exit code {proc.returncode}")
    # Lines look like: "differential";"Q1";8;0;3;"Update";"Time";123456
    times: dict[tuple[str, int], float] = {}
    results: dict[int, str] = {}
    for row in csv.reader(proc.stdout.splitlines(), delimiter=";"):
        if len(row) != 8 or row[1] != query:
            continue
        if row[6] == "Time":
            times[row[5], int(row[4])] = int(row[7]) / 1e9
        elif row[6] == "Elements":
            results[int(row[4])] = row[7]
    # Load (parsing input files) is excluded, same as for the other engines.
    initial = times["Initial", 0]
    updates = [times["Update", i] for i in range(1, 21)]
    return RunResult(initial, updates, [results.get(i, "?") for i in range(21)], None)


def first_mismatch(result: RunResult, expected: list[str]) -> str | None:
    for i, (got, want) in enumerate(zip(result.results, expected)):
        if got != want:
            return f"step {i}: {got} instead of {want}"
    return None


def format_report(measurements: list[Measurement], runs: int) -> str:
    headers = ["size", "elements", "engine", "approach", "language", "initial", "20 updates", "vs Saengra", "memory Δ"]
    lines: list[str] = []
    for query in ("Q1", "Q2"):
        rows: list[list[str]] = []
        group = [m for m in measurements if m.query == query]
        for size in sorted({m.size for m in group}):
            same = [m for m in group if m.size == size]
            saengra = next((m for m in same if m.engine == "saengra" and m.runs and not m.mismatch), None)
            for m in same:
                info = ENGINES[m.engine]
                row = [str(size), f"{m.elements:,}", info.title, info.approach, info.language]
                if m.error:
                    row += [m.error, "", "", ""]
                elif m.mismatch:
                    row += ["WRONG RESULT", m.mismatch, "", ""]
                else:
                    upd = m.median(lambda r: sum(r.updates))
                    ratio = ""
                    if saengra is not None and m is not saengra:
                        ratio = f"{upd / saengra.median(lambda r: sum(r.updates)):.2g}×"
                    mem = "n/a" if m.runs[0].memory is None else _fmt_mem(int(m.median(lambda r: r.memory)))
                    row += [_fmt_time(m.median(lambda r: r.initial)), _fmt_time(upd), ratio, mem]
                rows.append(row)
            rows.append([])
        if not rows:
            continue
        widths = [max(len(h), *(len(r[i]) for r in rows if r)) for i, h in enumerate(headers)]
        title = {
            "Q1": "Q1: top 3 posts by 10 × comments + likes on comments",
            "Q2": "Q2: top 3 comments by Σ (size of friend component among likers)²",
        }[query]
        lines += ["", f"TTC 2018 Social Media — {title}; median of {runs} run(s)"]
        lines.append("  ".join(h.ljust(w) for h, w in zip(headers, widths)))
        lines.append("  ".join("─" * w for w in widths))
        for row in rows[:-1]:
            lines.append("  ".join(c.ljust(w) for c, w in zip(row, widths)) if row else "")
    lines += [
        "",
        "initial: loading the model into the engine and computing the first result (file parsing excluded);",
        "20 updates: applying all change sets and recomputing the result, in total;",
        "vs Saengra: update time relative to Saengra (>1× means slower than Saengra).",
        f"{OFFICIAL_SOLUTIONS_NOTE}; CLIPS and Drools run rules written for this benchmark.",
        "Engines in Docker run in a Linux VM; memory of official solutions isn't measured.",
    ]
    return "\n".join(lines)
