# Usage: graphs

Every Saengra application starts with creating an `Environment`. Under the hood,
a new environment creates a new in-memory graph controlled by C++-based low-level engine.
Graph, as one would expect, consists of vertices and edges.

## Vertices and edges

**Vertices** within Saengra graphs are pairs `(type, value)`, where `type` is an identifier
(alphanumeric, starting with a letter) and `value` is an arbitrary string. Python wrapper operates on
a higher level abstraction, **primitive**, which is a Python object that is hashable and pickleable.
Primitive converts into vertex as `(type(primitive).__name__, pickle.dumps(primitive))`.

**Edges** within Saengra graphs are triplets `(from, label, to)`, where `from` and `to` are vertices, and `label`
is an identifier (alphanumeric, starting with a letter). There can be only one edge from particular
`from` vertex to particular `to` vertex with particular `label`. Edges with `from == to` are allowed.

Saengra provides API to manipulate graph directly from Python code:

```python
from saengra import Environment
from saengra.graph import AddVertex, AddEdge

env = Environment()
env.update(
    AddVertex("foo"),
    AddVertex("bar"),
    AddEdge("foo", "label", "bar"),
)
```

The following graph updates are supported:

::: saengra.graph.AddVertex
    handler: python
    options:
      show_root_heading: true
      show_source: true

::: saengra.graph.AddEdge
    handler: python
    options:
      show_root_heading: true
      show_source: true

::: saengra.graph.RemoveVertex
    handler: python
    options:
      show_root_heading: true
      show_source: true

::: saengra.graph.RemoveEdge
    handler: python
    options:
      show_root_heading: true
      show_source: true

::: saengra.graph.RemoveEdgesToAll
    handler: python
    options:
      show_root_heading: true
      show_source: true

## Graph traversal

Like common libraries like networkx, Saengra provides API for low-level graph traversal,  like fetching
outgoing/incoming vertex edges. However, Saengra is designed to be used from a higher level of abstraction,
so this should not normally be necessary.
```python
# Get all vertices and edges currently present in the graph:
primitives, edges = env.find_all()
# Iterate outgoing edges from a particular vertex:
edges = env.find_edges(from_=v)
# Iterate outgoing edges with particular label:
edges = env.find_edges(from_=v, with_label="label")
# Iterate incoming edges to a particular vertex:
edges = env.find_edges(to=v)
# Iterate incoming edges with particular label:
edges = env.find_edges(to=v, with_label="label")
```

## Transactions

Despite not supporting concurrency at the time, Saengra implements transactions to avoid
irreparable graph corruption in case of errors in complex applications. Therefore all data
in the graph is stored in three layers: 1) **committed** vertices/edges; 2) **added/removed**,
but not yet committed vertices/edges; 3) **just added/just removed** vertices/edges (see the section
on commit cycle below). The API is straightforward:
```python
from saengra import Environment

env = Environment()
try:
    env.update(...)
    env.commit()
except Exception:
    env.rollback()
    raise
```

## Commit cycle

Because of deeply rooted support for observers and callbacks, committing updates in Saengra is less
straightforward than might be expected. Internally, when commit command is executed, Saengra performs
incremental pattern matching, moves just added/just removed vertices/edges to added/removed, and invokes
observer callbacks, if any are fired. Once all the callbacks are performed, Saengra attempts to commit
again. This cycle may continue indefinitely if callbacks modify graph in a way triggering more callbacks,
but there's a hard limit of 100 iterations, after which commit is deemed impossible and fails.

That said, the call tree for typical application looks like this:
```
Environment.update
└─ invoking graph update command
   └─ adding data to just added/just removed layer
Environment.commit
├─ invoking graph commit command
│  ├─ incremental matching of observer patterns
│  ├─ moving data from just added/just removed layer to added/removed layer
│  └─ returning list of triggered observers
├─ invoking triggered observers' callbacks
│  ├─ Python observer callback 1
│  ├─ Python observer callback 2
│  ├─ ...
│  └─ Python observer callback N
├─ invoking graph commit command
│  ...
└─ invoking graph commit command
   ├─ incremental matching of observer patterns
   ├─ moving data from just added/just removed layer to added/removed layer
   └─ no triggered observers — moving data from added/removed layer to committed layer
```

The fundamental difference of low-level Saengra graph API is that commit command doesn't just
return OK or error, but may also return the list of triggered observers which should be invoked
and the commit command reissued again after that.

## Performance

Saengra stores graph vertices in a simple hash set, and edges in a hash map-based dual-indexed 
triple layer  adjacency list (all edges → edges from particular vertex → edges from particular 
edges with  particular label, and the same for reverse edges). Optimizations are present for the
frequent case of single edge with particular label.

All and all, Saengra is a young project, and many more optimizations are possible. Because of
the incredibly high level of abstraction, it is advised to avoid specific optimizations on user
side (in favor of submitting benchmarks and optimization pull requests).
