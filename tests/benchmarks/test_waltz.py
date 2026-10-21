"""
Waltz line labeling from the OPS5 benchmark suite: Saengra vs a reference Python Rete with
an agenda, CLIPS and Drools, all running translations of the same CLIPS program.

    RUN_HEAVY_BENCHMARKS=1 pytest tests/benchmarks/test_waltz.py -s

Optional settings:
    WALTZ_SIZES     numbers of regions, default "12,25,37,50" (the classic waltz12 ... waltz50 data
                    sets); any positive number works
    WALTZ_ENGINES   default: all engines listed in waltz/harness.py
    WALTZ_RUNS      runs per engine and size, default 3 (median is reported)
    WALTZ_TIMEOUT   seconds per run, default 300; on timeout larger sizes are skipped

CLIPS and Drools run in Docker; the image is built on first use. Every engine's final edge
labels and junctions are compared with CLIPS's (or, without Docker, the Python Rete's).
"""

import os
from pathlib import Path

import pytest

from tests.benchmarks.train_2017.harness import docker_available
from tests.benchmarks.waltz.harness import ENGINES, Measurement, build_docker_image, format_report, run_engine, write_lines
from tests.benchmarks.waltz.workload import generate


@pytest.mark.skipif(
    not os.environ.get("RUN_HEAVY_BENCHMARKS"),
    reason="heavy benchmark, set RUN_HEAVY_BENCHMARKS=1 to run",
)
def test_waltz(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    sizes = [int(s) for s in os.environ.get("WALTZ_SIZES", "12,25,37,50").split(",")]
    engines = os.environ.get("WALTZ_ENGINES", ",".join(ENGINES)).split(",")
    runs = int(os.environ.get("WALTZ_RUNS", "3"))
    timeout = float(os.environ.get("WALTZ_TIMEOUT", "300"))

    image = None
    if any(ENGINES[e].in_docker for e in engines):
        with capsys.disabled():
            if docker_available():
                image = build_docker_image()
            else:
                print("Docker is not available, skipping engines that run in Docker")
                engines = [e for e in engines if not ENGINES[e].in_docker]
    # The reference result comes from the original rules on CLIPS when possible.
    engines.sort(key=lambda e: (e != "clips", e != "rete-py"))

    measurements: list[Measurement] = []
    timed_out: set[str] = set()
    for regions in sizes:
        lines = generate(regions)
        lines_file = tmp_path / f"{regions}.lines"
        write_lines(lines, lines_file)
        expected: str | None = None
        for engine in engines:
            m = Measurement(engine, regions, len(lines))
            measurements.append(m)
            if engine in timed_out:
                m.error = "skipped"
                continue
            with capsys.disabled():
                print(f"waltz{regions} {ENGINES[engine].title}...", end=" ", flush=True)
            for _ in range(runs):
                try:
                    result = run_engine(engine, regions, image=image, lines_file=lines_file, timeout=timeout)
                except Exception as e:
                    m.error = "timeout" if "timed out" in str(e) else f"error: {e}"[:60]
                    timed_out.add(engine)
                    break
                m.runs.append(result)
                if expected is None:
                    expected = result.result
                elif result.result != expected:
                    m.mismatch = f"{result.result} instead of {expected}"
                    break
            with capsys.disabled():
                print(m.error or ("WRONG RESULT" if m.mismatch else "ok"), flush=True)

    with capsys.disabled():
        print(format_report(measurements, runs))

    wrong = [f"{m.engine} waltz{m.regions}: {m.mismatch}" for m in measurements if m.mismatch]
    assert not wrong, "engines produced wrong results:\n" + "\n".join(wrong)
