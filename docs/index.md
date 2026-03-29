# Saengra

Saengra is a reactive pattern-matching in-memory graph database. Current
capabilities include:
* in-memory vertex/edge storage
* support for (currently non-concurrent) transactions
* subgraph pattern matching based on regex-inspired pattern language
* create/update/delete observables for subgraphs matching a pattern
* ORM-like Python wrapper for vertices and edges

It is intended to be used as an engine for a particular subset of applications,
where a large number of various entities and relations between them are present,
the state is more conservative than behavior, and observer/observable pattern is
used extensively to maximize decoupling. A good example would be a turn-based game.

## In-memory graph storage

Saengra graphs are directed labeled graphs. Saengra stores vertices as pairs
`(type, value)`, where `type` is an identifier (alphanumeric, starting with a letter)
and `value` is an arbitrary string. Edges are labeled with string labels which are also
identifiers. There can be multiple edges between the same pair of vertices with different
labels, but only one with a specific label.

Python wrapper allows storing arbitrary Python objects as vertices as long as they
are hashable and pickleable. Such objects are called **primitives**.

```python
from dataclasses import dataclass

from saengra import Environment
from saengra.graph import AddVertex, AddEdge

@dataclass(frozen=True)
class person:
    name: str

env = Environment()
env.update(
    AddVertex(primitive=person("alice")), 
    AddVertex(person("bob")), 
    AddEdge(person("alice"), "corresponds_with", person("bob")),
)

# Getting list of all vertices and edges:
primitives, edges = env.find_all()

# Getting vertices of particular Python type:
persons = env.find_all_of_type(person)

# Getting edges from particular vertex:
correspondents = [
    match["c"]
    for match in env.match("? --corresponds_with-> * as c", person("alice"))
]
```

## Transactions

To avoid irreversible corruption of graph state, Saengra supports transactions.
Ongoing transaction can be rolled back any time.

Currently no concurrency is supported, so there's just one ("current") transaction.

```python
try:
    env.update(
        ...
    )
    ... # do some work
    env.commit()
except Exception:
    env.rollback()
    raise
```

## Subgraph pattern matching

Once the graph is populated with various primitives and edges between them, an
advanced pattern matching language can be used to find matching subgraphs of the graph.

```python
# Find all pending friendship requests:
env.match("person as a (--added_to_friends-> & (?! <-added_to_friends--)) person as b")
# Match every person and all their confirmed friends as a single subgraph:
env.match("person as a (all: <-added_to_friends-> person)")
# etc.
```

Supported operators include matching arbitrary vertices, vertices of particular type,
arbitrary edges, edges with particular labels, edges in particular direction, sequences
of vertex/edge operators, lookahead assertions, joins, etc.

## Observable patterns

The most powerful feature of Saengra is the ability to create observers which follow
particular subgraph patterns. Callbacks can be invoked upon creation, modification, or
deletion of any subgraph matching a particular pattern.

```python
from saengra import observer

confirmed_friendship_request = observer(
    "person as a <-added_to_friends-> person as b"
)

@confirmed_friendship_request.on_create
def send_friendship_push_notification(a: person, b: person) -> None:
    ...

env.register_observers([pending_friendship_request])
```

This example is trivial, but observers work for patterns of arbitrary complexity,
providing immense opportunities for decoupling. No need to subscribe to dozens of
event buses every time you add a feature — just declare what it depends on with
a pattern and subscribe to updates.

## ORM-like Python wrapper

To abstract from low-level graph operations like adding/removing vertices and edges,
Saengra provides a Python wrapper which allows declaring a high-level models (similar
to those in Python ORMs) that hide all the dirty work. 

```python
from saengra import primitive, Entity, Environment

@primitive
class person:
    id: int
    
class Person(Entity, person):
    name: str
    added_to_friends: set["Person"]
    corresponds_with: set["Person"]

env = Environment(entity_types=[Person])
alice = Person.create(env, id=1, name="Alice")
bob = Person.create(env, primitive=person(id=2), name="Bob")
alice.corresponds_with.add(bob)
env.commit()

...

alice = Person.get(env, id=1)
alice.remove()
```
