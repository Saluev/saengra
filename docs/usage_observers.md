# Usage: observers

While ad hoc pattern matching is surely fun, the main purpose of Saengra is
to enable incremental matching and invoking callbacks.

An observer can be created for any pattern:

```python
from saengra import observer, Environment

friend = observer("user as u <added_to_friends> user as f")

@friend.on_create
def handle_new_friend(u: User, f: User) -> None:
    """
    Invoked every time user `u` gets a new mutual friend `f`.
    
    Note that since subgraphs with different start positions are considered
    distinct, this observer will be invoked twice, with (u, f) and (f, u).
    """

all_friends = observer("user as u (all: <added_to_friends> user)")

@all_friends.on_create
@all_friends.on_change
@all_friends.on_delete
def handle_friends_list_change(u: User) -> None:
    """ Invoked every time user `u`'s set of friends changes."""

env = Environment(entity_types=[User])
env.register_observers([friend, all_friends])
...
```

Observer patterns can use placeholders as well:

```python
female_friend = observer("user as u <added_to_friends> user as f -gender> ?", Gender.FEMALE) 
```

## Event types

Saengra observers support three events: `on_create`, `on_change` and `on_delete`.

**Subgraph creation callback** `on_create` is called when the corresponding pattern
matches a new subgraph, which was not there
until recent graph updates. A trivial usecase would be tracking entity creation:

```python
a_user = observer("user as u")

@a_user.on_create
def handle_new_user(u: User) -> None:
    """ Invoked every time a new `user()` primitive is added to the graph. """
    # Send welcome email, etc.
```

A more useful application would be to track some kind of complex condition requiring additional actions:
```python
banned_user = observer("user as u <includes- blocklist")

@banned_user.on_create
def handle_user_ban(u: User) -> None:
    """
    Rather than being invoked every time a new user is created, this
    callback will be invoked every time a new user is added to a blocklist.
    """
```

**Subgraph modification callback** `on_change` is called when a subgraph has changed. Here, subgraphs are
identified by their start position and refs only. So, adding/removing vertices or edges without changing
refs would lead to a `on_change` event. You should design your patterns in such a way that all important
vertices/edges that you want to trigger the callback are included in the resulting subgraph. For example,
let's consider code updating a user's profile summary:

```python
user_summary = observer(
    "user as u (all:"
    "    -name|bio|location> |"
    "    <added_to_friends> user"
    ")"
)

@user_summary.on_change
def handle_user_summary_change(u: User) -> None:
    """
    Invoked every time user changes their name, bio, or location,
    or adds/removes a friend.
    
    Note that this only works because every user has a name. If all edges listed in
    the expression are optional, we should add a (maybe: ) expression to catch
    the case when user has no friends and clears its name, bio, and location.
    Or, alternatively, add an `on_delete` callback with `u.alive` check.
    """
    u.summary = compose_summary(u.name, u.bio, u.location, u.added_to_friends)
```

**Subgraph removal callback** `on_delete` is called when a subgraph that previously matched the pattern doesn't match it anymore. Note
that it does not mean all its vertices and edges vanished from existence; it's enough that one of the necessary
vertices was deleted, making the pattern no longer match; or maybe a named reference started pointing
to another vertex. A good example would be tracking user's name change:

```python
user_name = observer("user as u -name> str as n")

@user_name.on_delete
def handle_user_name_deletion(u: User, n: str) -> None:
    if not u.alive:
        # The subgraph is considered deleted because
        # the corresponding user has been deleted.
        return
    # The subgraph is considered deleted because user's name
    # started pointing to another string. But in this callback
    # we receive the previous value and can use it however we want.
    print(f"User #{u.id} no longer goes under the name of {n}; they are {u.name} now.")
```

## Primitives vs entities in callback arguments

To make life easier, Saengra will convert all primitives to their corresponding entity classes
before invoking observer callbacks. But that will only work for entity classes registered
when creating the environment:

```python
from saengra import observer, primitive, Entity, Environment

@primitive
class user:
    id: int

@primitive
class comment:
    id: int

class User(Entity, user):
    ...

class Comment(Entity, comment):
    ...

env = Environment(entity_types=[User])

a_user = observer("user as u")

@a_user.on_create
def handle_new_user(u) -> None:
    """ In this method `u` will be an instance of `User`. """

a_comment = observer("comment as c")
    
@a_comment.on_create
def handle_new_comment(c) -> None:
    """
    In this method `c` will be an instance of `comment`, not `Comment` — 
    because `Comment` isn't one of the registered entity classes.
    """

env.register_observers([a_user, a_comment])
```
That said, observer callbacks work just fine with primitive types that are not
supposed to be entities at all:
```python

@observer("user as u -name> str as n").on_change
def handle_user_name_change(u: User, n: str) -> None:
    pass
```

## Callback invocation and entity lifecycle

Observer callbacks are invoked during commit phase. Therefore you shouldn't (and don't have to)
call `env.commit()` within observer callbacks — the commit is already in progress and will be
finished unless there's an exception. See [Commit cycle](usage_graphs.md#commit-cycle) for details.

Important thing to know is that when execution reaches observer callback, the subgraph
that was created/changed isn't guaranteed to still exist — its vertices or edges might be
removed by your other observer callbacks preceding current one. Hence, the entities passed as
observer arguments are not guaranteed to be alive. If some of your observers can delete vertices
from the graph, it's better to make sure they are still alive when they are needed:
```python
@all_friends.on_change
def handle_friends_list_change(u: User) -> None:
    # If for some reason a user can be deleted, you should
    # check whether it's still alive before working with it.
    if not u.alive:
        return
    
    # This loop would raise an exception if `u` wasn't alive: 
    for f in u.added_to_friends:
        ...
```
This is particularly true for `on_delete` callbacks. If you track deletion of individual
entities, you will get an entity which is not alive all the time — unless there's a
possibility that you recreated the same entity in some of the other observers:

```python
@observer("user as u").on_delete
def handle_user_deletion(u: User) -> None:
    if u.alive:
        # Another observer callback recreated this user after it has been deleted.
        return
    ...
```

## Collecting observers from modules

Saengra-based application may include hundreds of observers, and the whole point is advanced code
decomposition; so naturally we would like to have separate modules for separate business logic:

```text
.
├── entities.py
└── observers
    ├── friends.py
    ├── moderation.py
    └── profile.py
```

To help with that, Saengra provides helper function `collect_all()` which can be used
to collect all observers in a module:

```python
from saengra import collect_all, Environment

from entities import User, Post, Comment, BlockList
import observers.friends
import observers.moderation
import observers.profile

env = Environment(entity_types=[User, Post, Comment, BlockList])
env.register_observers(
    collect_all(
        observers.friends,
        observers.moderation,
        observers.profile,
    )
)
```

## Performance

To track which callbacks should be called, Saengra **does not** perform complete pattern matching
every time you commit. Instead, incremental matching is implemented. Saengra tracks changes to the
graph and figures out what start positions might need a rematch. For big subgraphs, small local changes
will invoke local incremental pattern matching only. You only pay for rematching where it's unavoidable.






