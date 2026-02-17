import logging
import sys

from saengra.graph import Primitive
from saengra.utilities.colors import cyan, green, yellow, red, bold_red

COLORS = {
    "DEBUG": cyan,
    "INFO": green,
    "WARNING": yellow,
    "ERROR": red,
    "CRITICAL": bold_red,
}


class ColoredFormatter(logging.Formatter):
    def format(self, record):
        levelname = record.levelname
        if levelname in COLORS:
            record.levelname = f"{COLORS[levelname](levelname)}"
        return super().format(record)


def configure_logging():
    """Configure the 'saengra' logger to output all records (DEBUG+) to stdout."""
    logger = logging.getLogger("saengra")
    logger.setLevel(logging.DEBUG)

    # Remove existing handlers to avoid duplicates
    logger.handlers.clear()

    # Create stdout handler
    handler = logging.StreamHandler(sys.stdout)
    handler.setLevel(logging.DEBUG)

    # Set format
    formatter = ColoredFormatter(
        "[saengra] [%(asctime)s.%(msecs)03d] [%(levelname)s] %(message)s",
        datefmt="%H:%M:%S",
    )
    handler.setFormatter(formatter)

    logger.addHandler(handler)

    # Prevent propagation to root logger
    logger.propagate = False


logger = logging.getLogger("saengra")


class LazyRefsFormatter:
    """ Optimize logging calls in production environment by not forming
    strings unless the logging level demands it. """

    __slots__ = ("_refs",)

    def __init__(self, refs: dict[str, Primitive]) -> None:
        self._refs = refs

    def __str__(self) -> str:
        return ", ".join(f"{k}={v!r}" for k, v in sorted(self._refs.items()))
