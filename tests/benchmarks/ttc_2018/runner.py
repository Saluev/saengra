"""
Runs one engine on one query and model size in a fresh process, printing a JSON line.

    python -m tests.benchmarks.ttc_2018.runner ENGINE QUERY REPOSITORY SIZE
"""

import json
import sys
from pathlib import Path
from time import perf_counter

from tests.benchmarks.train_2017.runner import peak_rss_bytes
from tests.benchmarks.ttc_2018.engines import create_engine
from tests.benchmarks.ttc_2018.workload import load_workload


def main(engine_name: str, query: str, repository: str, size: str) -> None:
    workload = load_workload(Path(repository), int(size))
    engine = create_engine(engine_name, query)
    mem_baseline = peak_rss_bytes()

    t0 = perf_counter()
    engine.load(workload.load)
    results = [engine.check()]
    initial = perf_counter() - t0

    updates: list[float] = []
    for ops in workload.changes:
        t = perf_counter()
        engine.transform(ops)
        results.append(engine.check())
        updates.append(perf_counter() - t)

    print(
        json.dumps(
            {
                "initial": initial,
                "updates": updates,
                "results": results,
                "mem_baseline": mem_baseline,
                "mem_peak": peak_rss_bytes(),
            }
        ),
        flush=True,
    )


if __name__ == "__main__":
    main(*sys.argv[1:])
