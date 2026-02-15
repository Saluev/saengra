"""High-level Saengra client."""

import os
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Iterable

# from saengra_pyproto import Converter as ConverterV2

from saengra import messages_pb2
from saengra.api import CommitResponse, FindResponse, MatchResponse, Observer
from saengra.conversions import Converter
from saengra.errors import (
    ConnectionError as SaengraConnectionError,
    UpdateError,
    CommitError,
    RollbackError,
    ObserveError,
    ConnectError,
    FindError,
    MatchError,
)
from saengra.graph import Update, Primitive


def find_server_binary() -> str:
    if subprocess.call(["which", "saengra_server"]) == 0:
        return "saengra_server"

    project_root = Path(__file__).parent.parent
    candidates = [project_root / "saengra-server" / "build" / "saengra_server"]
    for candidate in candidates:
        if candidate.exists():
            return str(candidate)
    raise FileNotFoundError(f"could not find saengra-server binary")


class SaengraClient:
    """High-level client for Saengra graph database.

    Can either start saengra-server in the background or connect to an existing
    server via Unix domain socket.
    """

    def __init__(
        self,
        server_binary: str | None = None,
        socket_path: str | None = None,
    ):
        """Initialize the client.

        Args:
            server_binary: Path to saengra-server binary (default: "saengra-server" from PATH).
                          Set to None if connecting to existing socket.
            socket_path: Path to existing Unix socket. If provided, connects to this socket
                        instead of starting a new server process.
        """
        self.server_binary = server_binary or find_server_binary()
        self.socket_path: str | None = socket_path
        self.process: subprocess.Popen | None = None
        self.sock: socket.socket | None = None
        self._owns_server = socket_path is None  # Track if we started the server
        self._converter = Converter()
        # self._converter_v2 = ConverterV2()

    def __enter__(self):
        """Start the server and connect."""
        self.start()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Stop the server and cleanup."""
        self.stop()

    def start(self):
        """Start the saengra-server process or connect to existing socket."""
        if self._owns_server:
            # Start a new server process
            if not self.server_binary:
                raise ValueError(
                    "server_binary must be provided when not using existing socket"
                )

            # Create temporary socket file
            fd, self.socket_path = tempfile.mkstemp(suffix=".sock")
            os.close(fd)
            os.unlink(self.socket_path)  # Server will create it

            # Start server process
            self.process = subprocess.Popen(
                [self.server_binary, self.socket_path],
                stdout=sys.stdout,
                stderr=sys.stderr,
                env={**os.environ, "SAENGRA_LOGS_COLOR": "1"},
            )

            max_wait = 5.0
            wait_interval = 0.1
            elapsed = 0.0
            while not os.path.exists(self.socket_path) and elapsed < max_wait:
                if self.process.poll() is not None:
                    raise RuntimeError("server process exited unexpectedly")
                time.sleep(wait_interval)
                elapsed += wait_interval

            if not os.path.exists(self.socket_path):
                raise RuntimeError(f"Server socket not created after {max_wait}s")
        else:
            # Connect to existing socket
            if not self.socket_path:
                raise ValueError(
                    "socket_path must be provided when connecting to existing server"
                )
            if not os.path.exists(self.socket_path):
                raise FileNotFoundError(
                    f"Socket file does not exist: {self.socket_path}"
                )

        # Connect to socket
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)

        try:
            self.sock.connect(self.socket_path)
        except Exception as exc:
            raise SaengraConnectionError(
                f"failed to connect to {self.socket_path}"
            ) from exc

    def stop(self):
        """Stop the server process and cleanup."""
        if self.sock:
            try:
                self.sock.close()
            except Exception:
                pass
            self.sock = None

        # Only stop the server and clean up socket if we own it
        if self._owns_server:
            if self.process:
                self.process.terminate()
                try:
                    self.process.wait(timeout=5.0)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait()
                self.process = None

            if self.socket_path and os.path.exists(self.socket_path):
                try:
                    os.unlink(self.socket_path)
                except Exception:
                    pass
                self.socket_path = None

    def _send_request(
        self, request: messages_pb2.Request | bytes
    ) -> messages_pb2.Response:
        """Send a request and receive a response.

        Args:
            request: Request message to send

        Returns:
            Response message from server
        """
        if not self.sock:
            raise RuntimeError("Client not connected. Call start() first.")

        # Serialize request
        request_data = (
            request if isinstance(request, bytes) else request.SerializeToString()
        )

        # Send length-prefixed message (4 bytes for length, little-endian)
        length = len(request_data)
        self.sock.sendall(struct.pack("<I", length))
        self.sock.sendall(request_data)

        # Receive response length
        length_bytes = self._recv_exact(4)
        response_length = struct.unpack("<I", length_bytes)[0]

        # Receive response data
        response_data = self._recv_exact(response_length)

        # Parse response
        response = messages_pb2.Response()
        response.ParseFromString(response_data)
        return response

    def _recv_exact(self, num_bytes: int) -> bytes:
        """Receive exact number of bytes from socket.

        Args:
            num_bytes: Number of bytes to receive

        Returns:
            Received bytes
        """
        data = b""
        while len(data) < num_bytes:
            chunk = self.sock.recv(num_bytes - len(data))
            if not chunk:
                raise SaengraConnectionError("Socket connection closed")
            data += chunk
        return data

    def connect(self, graph_id: str) -> bool:
        """Connect to a graph (creates if doesn't exist).

        Args:
            graph_id: ID of the graph to connect to

        Returns:
            True if graph was created, False if it already existed

        Raises:
            ConnectError: If connection fails
        """
        request = messages_pb2.Request()
        request.connect.graph_id = graph_id
        response = self._send_request(request)

        if response.connect.status != messages_pb2.ConnectResponse.OK:
            raise ConnectError(f"Failed to connect to graph '{graph_id}'")

        return response.connect.created

    def apply_updates(self, updates: list[Update]) -> None:
        """Apply a sequence of updates to the graph.

        Args:
            updates: list of updates to apply

        Raises:
            UpdateError: If updates fail to apply
        """
        request = messages_pb2.Request()
        updates_proto = request.apply_updates.updates
        for u in updates:
            self._converter.update_to_proto(u, updates_proto.add())
        request_v1 = request.SerializeToString()

        # request_v2 = self._converter_v2.serialize_apply_updates_request(updates)

        response = self._send_request(request_v1)

        if response.apply_updates.status != messages_pb2.ApplyUpdatesResponse.OK:
            raise UpdateError("Failed to apply updates")

    def commit(self) -> CommitResponse:
        """Commit pending changes.

        Returns:
            CommitResponse containing status and observations

        Raises:
            CommitError: If commit fails
        """
        request = messages_pb2.Request()
        request.commit.CopyFrom(messages_pb2.Commit())
        response = self._send_request(request)

        if response.commit.status not in (
            messages_pb2.CommitResponse.OK,
            messages_pb2.CommitResponse.REQUIRES_REPEATED_COMMIT,
        ):
            raise CommitError("Failed to commit changes")

        return self._converter.commit_response_from_proto(response.commit)

    def rollback(self) -> None:
        """Rollback pending changes.

        Raises:
            RollbackError: If rollback fails
        """
        request = messages_pb2.Request()
        request.rollback.CopyFrom(messages_pb2.Rollback())
        response = self._send_request(request)

        if response.rollback.status != messages_pb2.RollbackResponse.OK:
            raise RollbackError("Failed to rollback changes")

    def observe(self, observers: Iterable[Observer]) -> None:
        """Register observers for expressions.

        Args:
            observers: list of observers to register

        Raises:
            ObserveError: If registration fails
        """
        request = messages_pb2.Request()
        proto_observers = request.observe.observers
        for o in observers:
            self._converter.observer_to_proto(o, proto_observers.add())
        response = self._send_request(request)

        if response.observe.status != messages_pb2.ObserveResponse.OK:
            raise ObserveError("Failed to register observers")

    def find(
        self,
        vertices: bool = False,
        edges: bool = False,
        with_type_name: str | None = None,
        with_label: str | None = None,
        from_vertices: list[Primitive] | tuple[Primitive, ...] | None = None,
        to_vertices: list[Primitive] | tuple[Primitive, ...] | None = None,
    ) -> FindResponse:
        """Find vertices and/or edges in the graph.

        Args:
            vertices: If True, return all vertices (or filtered by type_name if provided)
            edges: If True, return all edges
            with_type_name: Optional filter to only return vertices of this type
            with_label: Optional filter to only return edges with this label
            from_vertices: Optional filter to only return edges from these vertices
            to_vertices: Optional filter to only return edges to these vertices

        Returns:
            FindResponse with lists of vertices and edges

        Raises:
            FindError: If find operation fails
        """
        request = messages_pb2.Request()
        request.find.vertices = vertices
        request.find.edges = edges
        if with_type_name is not None:
            request.find.with_type_name = with_type_name
        if with_label is not None:
            request.find.with_label = with_label
        for vertex in from_vertices or []:
            proto_vertex = getattr(request.find, "from").add()
            self._converter.vertex_to_proto(vertex, proto_vertex)
        for vertex in to_vertices or []:
            proto_vertex = request.find.to.add()
            self._converter.vertex_to_proto(vertex, proto_vertex)

        response = self._send_request(request)

        if response.find.status != messages_pb2.FindResponse.OK:
            raise FindError("Failed to find vertices/edges")

        return self._converter.find_response_from_proto(response.find)

    def match(
        self,
        expression: str,
        placeholder_values: list[Primitive] | tuple[Primitive, ...] | None = None,
    ) -> MatchResponse:
        """Match graph patterns using an expression.

        Args:
            expression: Pattern expression to match
            placeholder_values: Optional list of vertex values for placeholders in the expression

        Returns:
            MatchResponse with list of matching subgraphs

        Raises:
            MatchError: If match operation fails
        """
        request = messages_pb2.Request()
        request.match.expression = expression
        if placeholder_values:
            proto_placeholder_values = request.match.placeholder_values
            for v in placeholder_values:
                self._converter.vertex_to_proto(v, proto_placeholder_values.add())

        response = self._send_request(request)

        if response.match.status != messages_pb2.MatchResponse.OK:
            raise MatchError("Failed to match pattern")

        return self._converter.match_response_from_proto(response.match)
