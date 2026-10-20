import hashlib
import json
import shutil
import statistics
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
DOCKER_DIR = Path(__file__).resolve().parent / "docker"


@dataclass(frozen=True)
class EngineInfo:
    title: str
    algorithm: str
    language: str
    in_docker: bool


ENGINES: dict[str, EngineInfo] = {
    "saengra": EngineInfo("Saengra", "dep.-tracked rematch", "C++ / Py callbacks", False),
    "saengra-cypher": EngineInfo("Saengra (Cypher anchors)", "dep.-tracked rematch", "C++ / Py callbacks", False),
    "rete-py": EngineInfo("Rete (ref.)", "Rete", "Python", False),
    "treat-py": EngineInfo("TREAT (ref.)", "TREAT", "Python", False),
    "clips": EngineInfo("CLIPS 6.4", "Rete", "C", True),
    "drools": EngineInfo("Drools 8.44", "Phreak (lazy Rete)", "Java 21", True),
    "jena": EngineInfo("Jena 5.2 rules", "Rete¹", "Java 21", True),
}


@dataclass
class RunResult:
    read: float
    check: float
    transform: list[float]
    recheck: list[float]
    counts: list[dict[str, int]]
    mem_baseline: int
    mem_peak: int

    @property
    def read_and_check(self) -> float:
        return self.read + self.check

    @property
    def transform_and_recheck(self) -> float:
        return sum(self.transform) + sum(self.recheck)

    @property
    def memory(self) -> int:
        return self.mem_peak - self.mem_baseline


@dataclass
class Measurement:
    engine: str
    workload: str
    size: int
    elements: int
    runs: list[RunResult] = field(default_factory=list)
    error: str | None = None
    mismatch: str | None = None

    def median(self, attr: str) -> float:
        return statistics.median(getattr(r, attr) for r in self.runs)


def docker_available() -> bool:
    if shutil.which("docker") is None:
        return False
    return subprocess.run(["docker", "info"], capture_output=True).returncode == 0


def build_docker_image() -> str:
    """Builds the image with third-party engines, tagged by the hash of its sources."""
    digest = hashlib.sha256()
    for path in sorted(DOCKER_DIR.rglob("*")):
        if path.is_file():
            digest.update(str(path.relative_to(DOCKER_DIR)).encode())
            digest.update(path.read_bytes())
    tag = f"saengra-train-benchmark:{digest.hexdigest()[:12]}"
    exists = subprocess.run(["docker", "image", "inspect", tag], capture_output=True)
    if exists.returncode != 0:
        print(f"Building {tag} (first build downloads Drools, takes a few minutes)...", flush=True)
        subprocess.run(["docker", "build", "-t", tag, str(DOCKER_DIR)], check=True)
    return tag


def run_engine(engine: str, trace: Path, *, image: str | None, timeout: float) -> RunResult:
    info = ENGINES[engine]
    if not info.in_docker:
        cmd = [sys.executable, "-m", "tests.benchmarks.train_2017.runner", engine, str(trace)]
    else:
        assert image is not None
        docker = [
            "docker", "run", "--rm",
            "-v", f"{trace.parent}:/data:ro",
            "-v", f"{REPO_ROOT / 'tests'}:/repo/tests:ro",
            "-w", "/repo",
            image,
        ]  # fmt: skip
        if engine in ("drools", "jena"):
            cmd = docker + ["java", "-jar", f"/opt/{engine}-bench.jar", f"/data/{trace.name}"]
        else:
            cmd = docker + ["python3", "-m", "tests.benchmarks.train_2017.runner", engine, f"/data/{trace.name}"]
    proc = subprocess.run(cmd, cwd=REPO_ROOT, capture_output=True, text=True, timeout=timeout)
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip().splitlines()[-1] if proc.stderr.strip() else f"exit code {proc.returncode}")
    # Engines may print warnings around the result (e.g. CLIPS does on exit).
    result_line = next(l for l in reversed(proc.stdout.splitlines()) if l.startswith("{"))
    return RunResult(**json.loads(result_line))


def first_mismatch(result: RunResult, expected: list[dict[str, int]]) -> str | None:
    for step, (got, want) in enumerate(zip(result.counts, expected)):
        diff = {q: (got.get(q), n) for q, n in want.items() if got.get(q) != n}
        if diff:
            details = ", ".join(f"{q}: {g} instead of {w}" for q, (g, w) in diff.items())
            return f"step {step}: {details}"
    if len(result.counts) != len(expected):
        return f"{len(result.counts)} checks instead of {len(expected)}"
    return None


# ============================================================================
# Report
# ============================================================================


def _fmt_time(seconds: float) -> str:
    if seconds < 0.01:
        return f"{seconds * 1000:.1f} ms"
    if seconds < 1:
        return f"{seconds * 1000:.0f} ms"
    return f"{seconds:.2f} s"


def _fmt_mem(n: int) -> str:
    return f"{n / 2**20:.0f} MB"


def format_report(measurements: list[Measurement], runs: int) -> str:
    lines: list[str] = []
    headers = ["size", "elements", "engine", "algorithm", "language", "read+check", "transf.+recheck", "vs Saengra", "memory Δ"]

    for workload in sorted({m.workload for m in measurements}):
        rows: list[list[str]] = []
        group = [m for m in measurements if m.workload == workload]
        for size in sorted({m.size for m in group}):
            same_size = [m for m in group if m.size == size]
            saengra = next((m for m in same_size if m.engine == "saengra" and m.runs and not m.mismatch), None)
            for m in same_size:
                info = ENGINES[m.engine]
                row = [str(size), f"{m.elements:,}", info.title, info.algorithm, info.language]
                if m.error:
                    row += [m.error, "", "", ""]
                elif m.mismatch:
                    row += ["WRONG RESULT", m.mismatch, "", ""]
                else:
                    tr = m.median("transform_and_recheck")
                    ratio = ""
                    if saengra is not None and m is not saengra:
                        ratio = f"{tr / saengra.median('transform_and_recheck'):.1f}×"
                    row += [
                        _fmt_time(m.median("read_and_check")),
                        _fmt_time(tr),
                        ratio,
                        _fmt_mem(int(m.median("memory"))),
                    ]
                rows.append(row)
            rows.append([])

        widths = [max(len(h), *(len(r[i]) for r in rows if r)) for i, h in enumerate(headers)]
        title = {
            "inject": "Inject: 12 × (inject errors into 10 random elements per query, recheck all queries)",
            "repair": "Repair: 8 × (repair 5% of matches of every query, recheck all queries)",
        }.get(workload, workload)
        lines.append("")
        lines.append(f"Train Benchmark — {title}; median of {runs} run(s)")
        lines.append("  ".join(h.ljust(w) for h, w in zip(headers, widths)))
        lines.append("  ".join("─" * w for w in widths))
        for row in rows[:-1]:
            lines.append("  ".join(c.ljust(w) for c, w in zip(row, widths)) if row else "")
    lines += [
        "",
        "read+check: loading the model and computing initial matches; transf.+recheck: all iterations in total;",
        "vs Saengra: transf.+recheck time relative to Saengra (>1× means slower than Saengra);",
        "memory Δ: peak RSS growth after engine initialization (JVM figure includes heap reservation and JIT).",
        "Saengra, Rete (ref.) and TREAT (ref.) run natively; CLIPS, Drools and Jena run in Docker (Linux VM).",
        "¹ Jena's forward engine can't maintain negative conditions (`noValue` isn't re-evaluated), so for",
        "  SwitchMonitored, RouteSensor and SemaphoreNeighbor it maintains the positive part, and negation",
        "  is checked when counting matches.",
    ]
    return "\n".join(lines)
