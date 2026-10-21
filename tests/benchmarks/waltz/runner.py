"""
Runs one engine on one drawing size in a fresh process, printing a JSON line.

    python -m tests.benchmarks.waltz.runner ENGINE REGIONS

The Drools runner (docker/drools) prints the same JSON.
"""

import json
import sys
from time import perf_counter

from tests.benchmarks.train_2017.runner import peak_rss_bytes
from tests.benchmarks.waltz.engines import create_engine
from tests.benchmarks.waltz.workload import generate


def main(engine_name: str, regions: str) -> None:
    lines = generate(int(regions))
    engine = create_engine(engine_name)
    mem_baseline = peak_rss_bytes()

    t = perf_counter()
    engine.run(lines)
    elapsed = perf_counter() - t

    edges, junctions = engine.state()
    print(
        json.dumps(
            {
                "time": elapsed,
                "firings": engine.firings,
                "edges": [[p1, p2, label] for (p1, p2), label in edges.items()],
                "junctions": junctions,
                "mem_baseline": mem_baseline,
                "mem_peak": peak_rss_bytes(),
            }
        ),
        flush=True,
    )


if __name__ == "__main__":
    main(*sys.argv[1:])
