class SaengraError(Exception):
    """Base exception for Saengra client errors."""


class ConnectionError(SaengraError):
    """Error connecting to server."""


class CommandError(SaengraError):
    """Error executing command."""


class ConnectError(SaengraError):
    """Error connecting to graph."""


class UpdateError(CommandError):
    """Error applying updates."""


class CommitError(CommandError):
    """Error committing changes."""


class RollbackError(CommandError):
    """Error rolling back changes."""


class ObserveError(CommandError):
    """Error registering observers."""


class FindError(CommandError):
    """Error finding vertices or edges."""


class MatchError(CommandError):
    """Error matching patterns."""
