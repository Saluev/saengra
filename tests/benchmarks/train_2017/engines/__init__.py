from typing import Protocol

from ..workload import Op


class Engine(Protocol):
    """
    An incremental query engine under benchmark.

    The engine must maintain the match sets of all six Train Benchmark queries
    and report their sizes on `check()`.
    """

    name: str
    algorithm: str
    language: str

    def load(self, ops: list[Op]) -> None: ...

    def transform(self, ops: list[Op]) -> None: ...

    def check(self) -> dict[str, int]: ...


def create_engine(name: str) -> Engine:
    # Imports are local, so that every engine only needs its own dependencies.
    match name:
        case "saengra":
            from .saengra_engine import SaengraEngine

            return SaengraEngine()
        case "saengra-cypher":
            from .saengra_engine import QUERIES_CYPHER_ORDER, SaengraEngine

            return SaengraEngine(QUERIES_CYPHER_ORDER)
        case "rete-py":
            from .relational import ReteEngine

            return ReteEngine()
        case "treat-py":
            from .relational import TreatEngine

            return TreatEngine()
        case "clips":
            from .clips_engine import ClipsEngine

            return ClipsEngine()
        case _:
            raise ValueError(f"unknown engine: {name}")
