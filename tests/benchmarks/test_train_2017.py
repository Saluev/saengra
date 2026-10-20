"""
Train Benchmark (2017 edition) comparing Saengra with Rete and TREAT engines.

    RUN_HEAVY_BENCHMARKS=1 pytest tests/benchmarks/test_train_2017.py -s

Optional settings:
    TRAIN_BENCHMARK_SIZES    model sizes, default "1,2,4,8,16"
                             (size 1 ≈ 2k elements, the model grows linearly)
    TRAIN_BENCHMARK_ENGINES  default "saengra,rete-py,treat-py,clips,drools"
    TRAIN_BENCHMARK_RUNS     runs per engine and size, default 3 (median is reported)
    TRAIN_BENCHMARK_TIMEOUT  seconds per run, default 300; on timeout larger sizes are skipped

CLIPS and Drools are run in Docker; the image is built automatically on the first run.
See train_2017/workload.py for details on the workload and deviations from the original.
"""

import os
from pathlib import Path

import pytest

from tests.benchmarks.train_2017.harness import (
    ENGINES,
    Measurement,
    build_docker_image,
    docker_available,
    first_mismatch,
    format_report,
    run_engine,
)
from tests.benchmarks.train_2017.workload import build_workload, write_trace


@pytest.mark.skipif(
    not os.environ.get("RUN_HEAVY_BENCHMARKS"),
    reason="heavy benchmark, set RUN_HEAVY_BENCHMARKS=1 to run",
)
def test_train_2017(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    sizes = [int(s) for s in os.environ.get("TRAIN_BENCHMARK_SIZES", "1,2,4,8,16").split(",")]
    engines = os.environ.get("TRAIN_BENCHMARK_ENGINES", ",".join(ENGINES)).split(",")
    runs = int(os.environ.get("TRAIN_BENCHMARK_RUNS", "3"))
    timeout = float(os.environ.get("TRAIN_BENCHMARK_TIMEOUT", "300"))

    image = None
    if any(ENGINES[e].in_docker for e in engines):
        if docker_available():
            with capsys.disabled():
                image = build_docker_image()
        else:
            engines = [e for e in engines if not ENGINES[e].in_docker]
            with capsys.disabled():
                print("Docker is not available, skipping CLIPS and Drools")

    measurements: list[Measurement] = []
    for workload_name in ("inject", "repair"):
        timed_out: set[str] = set()
        for size in sizes:
            workload = build_workload(workload_name, size)
            trace = tmp_path / f"{workload_name}-{size}.trace"
            write_trace(workload, trace)
            for engine in engines:
                m = Measurement(engine, workload_name, size, workload.num_elements)
                measurements.append(m)
                if engine in timed_out:
                    m.error = "skipped"
                    continue
                with capsys.disabled():
                    print(f"{workload_name} size={size} {ENGINES[engine].title}...", end=" ", flush=True)
                for _ in range(runs):
                    try:
                        result = run_engine(engine, trace, image=image, timeout=timeout)
                    except Exception as e:
                        m.error = "timeout" if "timed out" in str(e) else f"error: {e}"[:60]
                        timed_out.add(engine)
                        break
                    m.runs.append(result)
                    if mismatch := first_mismatch(result, workload.expected):
                        m.mismatch = mismatch
                        break
                with capsys.disabled():
                    print(m.error or ("WRONG RESULT" if m.mismatch else "ok"), flush=True)

    with capsys.disabled():
        print(format_report(measurements, runs))

    wrong = [f"{m.engine} {m.workload} size={m.size}: {m.mismatch}" for m in measurements if m.mismatch]
    assert not wrong, "engines produced wrong results:\n" + "\n".join(wrong)
