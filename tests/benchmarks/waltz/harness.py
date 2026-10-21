import hashlib
import json
import statistics
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

from tests.benchmarks.train_2017.harness import REPO_ROOT, _fmt_mem, _fmt_time
from tests.benchmarks.waltz.workload import Line, summarize

DOCKER_DIR = Path(__file__).resolve().parent / "docker"


@dataclass(frozen=True)
class EngineInfo:
    title: str
    approach: str
    language: str
    in_docker: bool = False


ENGINES: dict[str, EngineInfo] = {
    "saengra": EngineInfo("Saengra", "observers¹", "C++ / Py callbacks"),
    "rete-py": EngineInfo("Rete (ref.)", "Rete + agenda", "Python"),
    "clips": EngineInfo("CLIPS 6.4", "Rete", "C", in_docker=True),
    "drools": EngineInfo("Drools 8.44", "Phreak (lazy Rete)", "Java 21", in_docker=True),
}  # fmt: skip

SAENGRA_NOTE = (
    "¹ Saengra has no agenda: stage transitions are done by the driver once a commit reaches a fixpoint,\n"
    "  callbacks get all matches of a commit iteration at once and skip ones invalidated by earlier\n"
    "  callbacks, and the driver finds the two boundary junctions (see waltz/saengra_engine.py).\n"
    "  Its firings include the skipped callbacks."
)


@dataclass
class RunResult:
    time: float
    firings: int
    result: str
    memory: int


@dataclass
class Measurement:
    engine: str
    regions: int
    lines: int
    runs: list[RunResult] = field(default_factory=list)
    error: str | None = None
    mismatch: str | None = None

    def median(self, fn) -> float:
        return statistics.median(fn(r) for r in self.runs)


def build_docker_image() -> str:
    """Builds the image with CLIPS and Drools, tagged by the hash of its sources."""
    digest = hashlib.sha256()
    for path in sorted(DOCKER_DIR.rglob("*")):
        if path.is_file():
            digest.update(str(path.relative_to(DOCKER_DIR)).encode())
            digest.update(path.read_bytes())
    tag = f"saengra-waltz:{digest.hexdigest()[:12]}"
    if subprocess.run(["docker", "image", "inspect", tag], capture_output=True).returncode != 0:
        print(f"Building {tag} (first build downloads Drools, takes a few minutes)...", flush=True)
        subprocess.run(["docker", "build", "-t", tag, str(DOCKER_DIR)], check=True)
    return tag


def write_lines(lines: list[Line], path: Path) -> None:
    path.write_text("".join(f"{p1} {p2}\n" for p1, p2 in lines))


def run_engine(engine: str, regions: int, *, image: str | None, lines_file: Path, timeout: float) -> RunResult:
    if not ENGINES[engine].in_docker:
        cmd = [sys.executable, "-m", "tests.benchmarks.waltz.runner", engine, str(regions)]
    else:
        assert image is not None
        docker = [
            "docker", "run", "--rm",
            "-v", f"{lines_file.parent}:/data:ro",
            "-v", f"{REPO_ROOT / 'tests'}:/repo/tests:ro",
            "-w", "/repo",
            image,
        ]  # fmt: skip
        if engine == "drools":
            cmd = docker + ["java", "-jar", "/opt/waltz-drools.jar", f"/data/{lines_file.name}"]
        else:
            cmd = docker + ["python3", "-m", "tests.benchmarks.waltz.runner", engine, str(regions)]
    proc = subprocess.run(cmd, cwd=REPO_ROOT, capture_output=True, text=True, timeout=timeout)
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip().splitlines()[-1] if proc.stderr.strip() else f"exit code {proc.returncode}")
    # Engines may print warnings around the result (e.g. CLIPS does on exit).
    data = json.loads(next(l for l in reversed(proc.stdout.splitlines()) if l.startswith("{")))
    edges = {(p1, p2): label for p1, p2, label in data["edges"]}
    return RunResult(
        data["time"], data["firings"], summarize(edges, data["junctions"]), data["mem_peak"] - data["mem_baseline"]
    )


def format_report(measurements: list[Measurement], runs: int) -> str:
    headers = ["regions", "lines", "engine", "approach", "language", "time", "vs Saengra", "firings", "memory Δ"]
    rows: list[list[str]] = []
    for regions in sorted({m.regions for m in measurements}):
        same = [m for m in measurements if m.regions == regions]
        saengra = next((m for m in same if m.engine == "saengra" and m.runs and not m.mismatch), None)
        for m in same:
            info = ENGINES[m.engine]
            row = [str(regions), f"{m.lines:,}", info.title, info.approach, info.language]
            if m.error:
                row += [m.error, "", "", ""]
            elif m.mismatch:
                row += ["WRONG RESULT", m.mismatch, "", ""]
            else:
                time = m.median(lambda r: r.time)
                ratio = ""
                if saengra is not None and m is not saengra:
                    ratio = f"{time / saengra.median(lambda r: r.time):.2g}×"
                mem = _fmt_mem(int(m.median(lambda r: r.memory)))
                row += [_fmt_time(time), ratio, f"{m.runs[0].firings:,}", mem]
            rows.append(row)
        rows.append([])
    widths = [max(len(h), *(len(r[i]) for r in rows if r)) for i, h in enumerate(headers)]
    lines = ["", f"Waltz line labeling (OPS5 benchmark suite); median of {runs} run(s)"]
    lines.append("  ".join(h.ljust(w) for h, w in zip(headers, widths)))
    lines.append("  ".join("─" * w for w in widths))
    for row in rows[:-1]:
        lines.append("  ".join(c.ljust(w) for c, w in zip(row, widths)) if row else "")
    expected = next((m.runs[0].result for m in measurements if m.engine == "clips" and m.runs), None)
    lines += [
        "",
        "time: from asserting the lines until no rule can fire (building the rule base excluded);",
        "vs Saengra: time relative to Saengra (>1× means slower than Saengra);",
        "results (edge labels and junction types) are checked against CLIPS running the original rules.",
        SAENGRA_NOTE,
        "Engines in Docker run in a Linux VM.",
    ]
    if expected is None:
        lines.append("CLIPS didn't run, so results were compared with the Python Rete instead.")
    return "\n".join(lines)
