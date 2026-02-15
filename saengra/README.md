# Saengra

Python wrapper for Saengra graph database.

## Quickstart: primitives and edges

Saengra is a graph database. It supports hashable Python objects (**primitives**) as graph
vertices. Built-in types like `int` or `str` can be used directly; also Saengra provides `@primitive`
decorator to declare dataclass-like types to be used as graph vertices.

Directed edges between primitives are always labelled with a string (**edge label**). It can
be an arbitrary string, but the engine is optimized to support limited number of different labels per graph.
There can't be two edges between the same two primitives with the same label.

We can construct a graph directly from primitives and edges by using elementary graph operations: 

```python
from datetime import datetime
from saengra import primitive, Environment
from saengra.graph import AddVertex, AddEdge

@primitive
class user:
    id: int

u1 = user(id=1)
u2 = user(id=2)
u1_registered_at = datetime(2022, 1, 1, 12, 0, 0)
u2_registered_at = datetime(2023, 2, 3, 15, 0, 0)

env = Environment()
env.update(
    AddVertex(u1),
    AddVertex(u2),
    AddVertex(u1_registered_at),
    AddVertex(u2_registered_at),
    AddEdge(u1, "follows", u2),
    AddEdge(u1, "registered_at", u1_registered_at),
    AddEdge(u2, "registered_at", u2_registered_at),
)
env.commit()
```

## Quickstart: entities and environment

Operating with vertices and edges is tedious and slow. A higher-level abstraction, **entities**,
is provided to make working with graph more like your normal object-oriented programming.

Let's declare some entity classes and rewrite the code above:

```python
from datetime import datetime
from saengra import primitive, Entity, Environment

@primitive
class user:
    id: int

class User(Entity, user):
    registered_at: datetime
    follows: set["User"]

env = Environment(entity_types=[User])

u1 = User.create(env, id=1, registered_at=datetime(2022, 1, 1, 12, 0, 0))
u2 = User.create(env, id=2, registered_at=datetime(2023, 2, 3, 15, 0, 0))
u1.follows.add(u2)

env.commit()
```

## Quickstart: expressions and observers

Saengra introduces a domain-specific language to describe subgraphs of the graph, i.e. subset of
vertices and edges. These expressions are quite similar to queries in SQL.

```python
# Find all subscriptions, i.e. pairs (u1, u2) where u1 follows u2:
env.match("user as u1 -follows> user as u2")
# -> [{"u1": User(id=1), "u2": User(id=2)}]

# Find all mutual subscriptions:
env.match("user as u1 <follows> user as u2")
# -> []
```

But the most powerful aspect of Saengra is its observation capability. Saengra can match
expressions incrementally after processing graph updates, and notify the program about created,
changed and deleted subgraphs after each commit.

```python
from saengra import observer

mutual_follow = observer("user as u1 <follows> user as u2")


@mutual_follow.on_create
def notify_mutuals(u1: User, u2: User):
    print(f"{u1} is now mutuals with {u2}!")


env.register_observers([mutual_follow])

u2.follows.add(u1)
env.commit()
# -> User(id=1) is now mutuals with User(id=2)!
# -> User(id=2) is now mutuals with User(id=1)!
```

## Generating Protobuf Code

The `messages_pb2.py` file is generated from the protobuf definitions in `saengra-server/proto/messages.proto`.

To regenerate:

```bash
protoc --python_out=saengra --proto_path=saengra-server/proto saengra-server/proto/messages.proto
```

Requirements:
- `protoc` (Protocol Buffers compiler) must be installed
- Python protobuf library: `pip install protobuf>=4.21.0`

## Usage

### Option 1: Automatically start server

```python
from saengra.client import SaengraClient

# Client automatically starts saengra-server in background
with SaengraClient() as client:
    # Connect to a graph
    created = client.connect("my_graph")

    # Add vertices and edges
    client.apply_updates([
        # Your updates here
    ])

    # Commit changes
    response = client.commit()
```

The client expects `saengra-server` binary to be available in PATH.

### Option 2: Connect to existing server

```python
from saengra.client import SaengraClient

# Connect to an existing server socket
with SaengraClient(socket_path="/path/to/server.sock") as client:
    # Connect to a graph
    created = client.connect("my_graph")

    # Work with the graph...
```

When using an existing socket, the client will not start or stop the server process, and will not clean up the socket file.
