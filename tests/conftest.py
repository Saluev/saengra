import pytest
from saengra import DirectAdapter, Environment
from saengra.utilities.loggers import configure_logging


@pytest.fixture(scope="session", autouse=True)
def enable_logging() -> None:
    configure_logging()


@pytest.fixture(scope="function")
def empty_env() -> Environment:
    return Environment(adapter=DirectAdapter())
