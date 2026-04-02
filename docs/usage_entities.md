# Usage: entities

To avoid tedious low-level graph manipulation, Saengra provides ORM-like
wrapper to manipulate vertices and their outgoing edges as normal Python
objects with attributes. These objects are called **entities**.

First step is to define **primitive**, the object that is going to be the
vertex in the graph (see [Graphs](usage_graphs.md)):

```python
from saengra import primitive

@primitive
class user:
    id: int
```

By convention primitive classes are named in lower case.

Second step is to define the entity class. It has to inherit `Entity` and
a primitive class (in that order) and nothing else. Attribute annotations
within the entity class instruct Saengra what kinds of edges are expected
to start in the corresponding vertex and how many for each label:

```python
from saengra import Entity

class User(Entity, user):
    name: str
    added_to_friends: set["User"]
```

In this example, `user()` vertices are expected to have one edge with label `"name"`
linking them to a `str()` vertex, and arbitrary number of edges with label `"added_to_friends"`
linking them to other `user()` vertices.

Third step would be to create `Environment` aware of the entity class:

```python
from saengra import Environment

env = Environment(entity_types=[User])
```

Now entity class can be used in a fashion familiar to Python ORM users.

```python
john = User.create(env, id=1, name="John")
sarah = User.create(env, id=2, name="Sarah", added_to_friends={john})
john.added_to_friends.add(sarah)
```

## `@primitive` decorator

`@primitive` decorator is roughly equivalent to
`@dataclass(frozen=True, slots=True, order=True)`, with some important distinctions
making them non-interchangeable. In particular, its `__getstate__()` and `__setstate__()`
methods are optimized compared to rather naive standard implementation for dataclasses
(as of 2026).

Primitives are supposed to be hashable and pickleable, therefore mutable
types like lists or non-serializable types like file descriptors will not work
for class fields.

## `Entity` class

::: saengra.entity.Entity
    handler: python
    options:
      members:
        - primitive
        - __new__
        - alive
        - create
        - get
        - get_or_none
        - get_or_create
        - update
        - discard
        - remove
      show_root_heading: true
      show_source: false
      heading_level: 3

## Entity lifecycle

If an entity object is constructed using `Entity` constructor:
```python
user = User(env, id=3)
```
no changes in the graph are immediately made. We can check whether the
created object corresponds to a primitive in the graph or not:
```pycon
>>> user.alive
False
```

Such an object is what is called **temporary entity** in Saengra. Once you
lose it out of sight, it will be garbage collected and the graph will remain
as it was before the object was created.

We can command Saengra to add it to graph, or fetch an existing one, to receive
**persistent entity**:
```pycon
>>> env.add(user)
>>> user.alive
True
>>> user = User.get(env, id=1)
```

Persistent entities are cached within `Environment`. Fetching the same entity
multiple times will return the same object. (This behavior is not guaranteed to
persist in the future versions of Saengra.)

If we delete the persistent entity, it turns into a temporary entity in-place, and
can be immediately used again:
```pycon
>>> user.remove()
>>> user.alive
False
>>> env.add(user)
```

Accessing fields of a temporary entity will raise an exception.

## Entity fields

Saengra provides limited support for fields, compared to Python dataclasses.
Basic annotations of types that are not mutable collections will work as many-to-one
relations, marked by edges with field name as the label.

```python
class User(Entity, user):
    name: str
    date_of_birth: datetime
    locale: tuple[str, str]
```

`user.name = "John"` will delete existing edges matching `(user, "name", *)` and create
the edge `(user, "name", "John")`.

Nullable fields are also supported; `None` is handled as the absence of an edge.

```python
class User(Entity, user):
    subscribed_at: datetime | None
```

`user.subscribed_at = None` will delete existing edges matching `(user, "subscribed_at", *)`.

Sets are treated as collections of vertices that are linked by an edge with the
specified label.

```python
class User(Entity, user):
    added_to_friends: set["User"]
```

Instead of an actual `set()`, `User().added_to_friends` will be a `RelatedEntitiesSet`,
a proxy type which accompanies its changes with appropriate graph updates. For example,
`john.added_to_friends.add(sarah)` will issue graph updates `AddVertex(sarah.primitive)` and
`AddEdge(john.primitive, "added_to_friends", sarah.primitive)`. `RelatedEntitiesSet`'s API
is identical to that of a regular `set()`.

Sets can consist of various types of items, but do not support mixing of entities and non-entities.

```python

class Post(Entity, post):
    pass

class Comment(Entity, comment):
    pass

class User(Entity, user):
    added_to_friends: set["User"]  # double quotes to reference self
    auth_methods: set["AuthMethod"]  # double quotes to reference forward declaration
    meta: set[tuple[str, Any]]  # arbitrary primitive types are supported
    subscribed_to_comments: set[Post]  # other entities are supported
    is_author_of: set[Post | Comment]  # unions of entity types are supported
    comments_and_drafts: set[Comment | str]  # ❌ not supported

class AuthMethod(Entity, auth_method):
    pass
```
