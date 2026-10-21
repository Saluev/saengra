from typing import Protocol

from tests.benchmarks.waltz.workload import Line


class WaltzEngine(Protocol):
    firings: int
    """ Number of rule firings (observer callbacks for Saengra), for information only. """

    def run(self, lines: list[Line]) -> None: ...

    def state(self) -> tuple[dict[tuple[int, int], str], list[str]]:
        """Returns labels of all edges and types of all junctions."""
        ...


def create_engine(name: str) -> WaltzEngine:
    match name:
        case "saengra":
            from tests.benchmarks.waltz.saengra_engine import SaengraEngine

            return SaengraEngine()
        case "rete-py":
            from tests.benchmarks.waltz.rete_engine import ReteWaltzEngine

            return ReteWaltzEngine()
        case "clips":
            from tests.benchmarks.waltz.clips_engine import ClipsEngine

            return ClipsEngine()
        case _:
            raise ValueError(f"unknown engine: {name}")
