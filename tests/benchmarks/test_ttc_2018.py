"""
TTC 2018 Social Media case: Saengra vs Rete and TREAT (Python references, CLIPS, Drools)
and official contest solutions.

    RUN_HEAVY_BENCHMARKS=1 pytest tests/benchmarks/test_ttc_2018.py -s

Optional settings:
    TTC_2018_REPOSITORY  clone of github.com/TransformationToolContest/ttc2018liveContest,
                         default ~/.cache/saengra/ttc2018liveContest (cloned automatically)
    TTC_2018_SIZES       model sizes, default "1,4,16,64" (available: 1, 2, 4, ..., 512)
    TTC_2018_ENGINES     default: all engines listed in ttc_2018/harness.py
    TTC_2018_RUNS        runs per engine, query and size, default 3 (median is reported)
    TTC_2018_TIMEOUT     seconds per run, default 300; on timeout larger sizes are skipped

Engines other than Saengra and the Python references run in Docker; images are built on first use.
Results are checked against the official expected results after every change set.
"""

import os
from pathlib import Path

import pytest

from tests.benchmarks.train_2017.harness import docker_available
from tests.benchmarks.ttc_2018.harness import (
    ENGINES,
    Measurement,
    build_docker_image,
    first_mismatch,
    format_report,
    run_engine,
    write_trace,
)
from tests.benchmarks.ttc_2018.workload import ensure_repository, load_workload


@pytest.mark.skipif(
    not os.environ.get("RUN_HEAVY_BENCHMARKS"),
    reason="heavy benchmark, set RUN_HEAVY_BENCHMARKS=1 to run",
)
def test_ttc_2018(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    repository = ensure_repository(
        Path(os.environ.get("TTC_2018_REPOSITORY", Path.home() / ".cache/saengra/ttc2018liveContest"))
    )
    sizes = [int(s) for s in os.environ.get("TTC_2018_SIZES", "1,4,16,64").split(",")]
    engines = os.environ.get("TTC_2018_ENGINES", ",".join(ENGINES)).split(",")
    runs = int(os.environ.get("TTC_2018_RUNS", "3"))
    timeout = float(os.environ.get("TTC_2018_TIMEOUT", "300"))

    images: dict[str, str] = {}
    if any(ENGINES[e].image for e in engines):
        with capsys.disabled():
            if docker_available():
                for name in dict.fromkeys(ENGINES[e].image for e in engines if ENGINES[e].image):
                    images[name] = build_docker_image(name, repository)
            else:
                print("Docker is not available, skipping engines that run in Docker")
                engines = [e for e in engines if not ENGINES[e].image]

    measurements: list[Measurement] = []
    for query in ("Q1", "Q2"):
        timed_out: set[str] = set()
        for size in sizes:
            workload = load_workload(repository, size)
            trace = tmp_path / f"{size}.trace"
            if "drools" in engines and not trace.exists():
                write_trace(workload, trace)
            for engine in engines:
                m = Measurement(engine, query, size, workload.num_elements)
                measurements.append(m)
                if engine in timed_out:
                    m.error = "skipped"
                    continue
                with capsys.disabled():
                    print(f"{query} size={size} {ENGINES[engine].title}...", end=" ", flush=True)
                for _ in range(runs):
                    try:
                        result = run_engine(engine, query, size, repository, images=images, trace=trace, timeout=timeout)
                    except Exception as e:
                        m.error = "timeout" if "timed out" in str(e) else f"error: {e}"[:60]
                        timed_out.add(engine)
                        break
                    m.runs.append(result)
                    if mismatch := first_mismatch(result, workload.expected[query]):
                        m.mismatch = mismatch
                        break
                with capsys.disabled():
                    print(m.error or ("WRONG RESULT" if m.mismatch else "ok"), flush=True)

    with capsys.disabled():
        print(format_report(measurements, runs))

    wrong = [f"{m.engine} {m.query} size={m.size}: {m.mismatch}" for m in measurements if m.mismatch]
    assert not wrong, "engines produced wrong results:\n" + "\n".join(wrong)
