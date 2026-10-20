"""
Runs one engine on one trace in a fresh process and prints timings as a single JSON line.

    python -m tests.benchmarks.train_2017.runner ENGINE TRACE_PATH

Every run happens in a separate process, so that peak memory can be measured and engines
don't affect each other. The Drools runner (docker/drools) prints the same JSON.
"""

import json
import resource
import sys
from pathlib import Path
from time import perf_counter

from .engines import create_engine
from .workload import read_trace


def peak_rss_bytes() -> int:
    rss = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    return rss if sys.platform == "darwin" else rss * 1024


def main(engine_name: str, trace_path: str) -> None:
    workload = read_trace(Path(trace_path))
    engine = create_engine(engine_name)
    mem_baseline = peak_rss_bytes()

    t0 = perf_counter()
    engine.load(workload.load)
    t1 = perf_counter()
    counts = [engine.check()]
    t2 = perf_counter()

    transform: list[float] = []
    recheck: list[float] = []
    for ops in workload.transformations:
        a = perf_counter()
        engine.transform(ops)
        b = perf_counter()
        counts.append(engine.check())
        c = perf_counter()
        transform.append(b - a)
        recheck.append(c - b)

    print(
        json.dumps(
            {
                "read": t1 - t0,
                "check": t2 - t1,
                "transform": transform,
                "recheck": recheck,
                "counts": counts,
                "mem_baseline": mem_baseline,
                "mem_peak": peak_rss_bytes(),
            }
        ),
        flush=True,
    )


if __name__ == "__main__":
    main(*sys.argv[1:])
