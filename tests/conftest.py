import sys

import pytest
from saengra import DirectAdapter, Environment
from saengra.utilities.loggers import configure_logging

if sys.version_info < (3, 12):
    # Uses the `type` statement.
    collect_ignore = ["test_15_type_aliases.py"]


@pytest.fixture(scope="session", autouse=True)
def enable_logging() -> None:
    configure_logging()


@pytest.fixture(scope="function")
def empty_env() -> Environment:
    return Environment(adapter=DirectAdapter())
