import csv
import hashlib
import json
import statistics
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

from tests.benchmarks.train_2017.harness import REPO_ROOT, _fmt_mem, _fmt_time

DOCKER_DIR = Path(__file__).resolve().parent / "docker"


@dataclass(frozen=True)
class EngineInfo:
    title: str
    approach: str
    language: str
    official: bool
    """ Official contest solution, run in Docker with the contest's own harness protocol. """


ENGINES: dict[str, EngineInfo] = {
    "saengra": EngineInfo("Saengra", "tuple observers", "C++ / Py callbacks", False),
    "saengra-all": EngineInfo("Saengra (all:)", "(all:) + re-query", "C++ / Py callbacks", False),
    "rete-py": EngineInfo("Rete (ref.)", "Rete", "Python", False),
    "treat-py": EngineInfo("TREAT (ref.)", "TREAT", "Python", False),
    "differential": EngineInfo("Differential", "differential dataflow", "Rust", True),
}


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


def build_docker_image(repository: Path) -> str:
    digest = hashlib.sha256((DOCKER_DIR / "Dockerfile").read_bytes()).hexdigest()[:12]
    tag = f"saengra-ttc2018-baselines:{digest}"
    if subprocess.run(["docker", "image", "inspect", tag], capture_output=True).returncode != 0:
        print(f"Building {tag} (compiles the Differential Dataflow solution, takes ~15 minutes)...", flush=True)
        subprocess.run(
            ["docker", "build", "-t", tag, "-f", str(DOCKER_DIR / "Dockerfile"), str(repository / "solutions")],
            check=True,
        )
    return tag


def run_engine(engine: str, query: str, size: int, repository: Path, *, image: str | None, timeout: float) -> RunResult:
    if not ENGINES[engine].official:
        cmd = [sys.executable, "-m", "tests.benchmarks.ttc_2018.runner", engine, query, str(repository), str(size)]
        proc = subprocess.run(cmd, cwd=REPO_ROOT, capture_output=True, text=True, timeout=timeout)
        if proc.returncode != 0:
            raise RuntimeError(proc.stderr.strip().splitlines()[-1] if proc.stderr.strip() else f"exit code {proc.returncode}")
        data = json.loads(next(l for l in reversed(proc.stdout.splitlines()) if l.startswith("{")))
        return RunResult(data["initial"], data["updates"], data["results"], data["mem_peak"] - data["mem_baseline"])

    assert image is not None
    env = {"ChangePath": f"/models/{size}", "ChangeSet": str(size), "Query": query,
           "RunIndex": "0", "Sequences": "20", "Tool": engine}  # fmt: skip
    cmd = ["docker", "run", "--rm", "-v", f"{repository / 'models'}:/models:ro"]
    for k, v in env.items():
        cmd += ["-e", f"{k}={v}"]
    cmd += [image, "/opt/differential", "HARNESS"]
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
    # Load (parsing CSV files) is excluded, same as for the in-process engines.
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
        "Differential is the official contest solution by Frank McSherry, run in Docker (Linux VM).",
    ]
    return "\n".join(lines)
