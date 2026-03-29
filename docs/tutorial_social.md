# Tutorial: a social network

In this tutorial we will build some algorithms which may be used in creating
a social network.

!!! warning
    Disclaimer: Saegnra currently is not designed for massive loads, nor it is
    for any kind of concurrent access. The theme for this tutorial has been chosen
    based on familiarity of average developer with concepts and functionality
    required for such a project.

## Declaring primitives

Saengra operates as a graph database, containing **vertices** and **edges**.
Vertices are arbitrary Python objects that are hashable and pickleable — such
objects are called **primitives**. **Entity** is a higher-level wrapper around
a primitive, capable of sending updates into the graph transparently for the
developer (think of an SQLAlchemy model).

A primitive must uniquely identify an object in the program, and be immutable.
So in terms of traditional databases it serves as a primary key for an entity.

For a social network, we will need users, posts and comments, which are best
identified by any kind of unique ID. For simplicity, let's use integer IDs.

```python
from saengra import primitive

@primitive
class user:
    id: int

@primitive
class post:
    id: int

@primitive
class comment:
    id: int
```

Other ways to identify an object are also possible. For example, composite keys.
Suppose we store authentication methods for our users and there can be only one of
each kind:

```python
@primitive
class auth_method:
    user_id: int
    provider: Literal["email", "google", "github"]
```

Under the hood, primitives are essentially frozen dataclasses with some tweaks and optimizations,
and can be used accordingly:

```python
u = user(id=1)
auth_method_1 = auth_method(1, "email")
auth_method_2 = replace(auth_method_1, provider="github")
auth_methods = {auth_method_1, auth_method_2}
```

## Declaring entities

Now we can move to declaring entities. Entities inherit primitives for simpler acesss
to primitive fields, and add fields based on graph edges.

```python
from saengra import Entity, frozendict

class User(Entity, user):
    name: str
    auth_methods: set["AuthMethod"]
    added_to_friends: set["User"]
    author_of: set["Post"]
    number_of_comments: int

class AuthMethod(Entity, auth_method):
    user: User
    metadata: frozendict[str, Any]

class Post(Entity, post):
    authored_by: User
    text: str
    comments: set["Comment"]

class Comment(Entity, comment):
    authored_by: User
    comments_on: Post
    replies_to: Optional["Comment"]
    text: str
```

All fields in entities work by creating edges between entity's primitive and the field
value. For example, `User.name` is an edge between a `user()` instance and a string.

## Populating the graph

Now we can create `Environment` (including graph storage and other things) and use
ORM-like methods to populate the graph.

```python
from saengra import Environment

env = Environment(entity_types=[User, AuthMethod, Post, Comment])

mark = User.create(env, id=1, name="Mark")
sergey = User.create(env, id=2, name="Sergey")
jack = User.create(env, id=3, name="Jack")

mark.added_to_friends.add(sergey)

post = Post.create(env, id=1, authored_by=jack, text="just setting up my sngr")
comment = Comment.create(
    env, id=1, authored_by=sergey, comments_on=post, text="Great job!"
)
reply = Comment.create(
    env, id=2, authored_by=jack, comments_on=post, replies_to=comment, text="Thanks!"
)
```

We can also query objects by their primitives, update and delete them:
```python
mark = User.get(env, id=1)
mark.name = "Incognito"
mark.delete()
```

There's a bunch of methods available for `Entity`, see API reference for details.

## Adding observers

Now, there's going to be a bunch of logic that operates on the graph. And Saengra
is designed to decouple independent pieces of logic, using state structure as the
backbone.

For example, we have `User.number_of_comments`. Like in regular relational databases,
it would be most efficient for us to compute it incrementally. But there's a lot of
scenarios to take into account:

* user writes a comment;
* user deletes a comment;
* moderator deletes a comment;
* a post the user commented on is deleted;
* comment can change ownership (you never know what future has in stock for us);
* etc.

We can write an observer which tracks any comment to an existing post authored by a specific user
and updates the number of comments for the corresponding user — independently of how and why
the comment was created or deleted.

```python
from saengra import observer

reachable_comment = observer(
    "comment (?= -authored_by> user as u) -comments_on> post as p"
)

@reachable_comment.on_create
def increment_number_of_comments(u: User) -> None:
    u.number_of_comments += 1


@reachable_comment.on_delete
def decrement_number_of_comments(u: User) -> None:
    u.number_of_comments -= 1

env.register_observers([reachable_comment])
```

Now we can write a function to submit a comment.

```python

def add_comment(
    env: Environment, 
    author_id: int, 
    post_id: int, 
    text: str, 
    replies_to_id: int | None = None
) -> None:
    post = Post.get(env, id=post_id)
    comment = Comment.create(
        id=next_id(),  # integer generator declared somewhere
        authored_by=User.get(env, id=author_id),
        comments_on=post,
        replies_to=Comment.get(env, id=replies_to_id) if replies_to_id is not None else None,
        text=text,
    )
    post.comments.add(comment)
    env.commit()
```

`env.commit()` is crucial here. Once the graph state is committed, Saengra invokes
incremental matching of patterns for all registered observers. It then invokes all
necessary callbacks, and after that attempts to commit again. If new observers come up,
they are invoked too. 

Technically it is possible to enter endless loop of incremental matching and invoking
callbacks; there's a hard limit of 100 iterations after which an exception will be thrown.
But with proper entity & observer design that is easily avoidable.

Let's add one more observer, with less trivial logic.

```python
friend_of_a_friend = observer(
    "user as u1"
    "    <added_to_friends> user"
    "        <added_to_friends> (?! u1) user as u2 (?! <added_to_friends> u1)"
)

@friend_of_a_friend.on_create
def suggest_adding_friend_of_a_friend(u1: User, u2: User) -> None:
    send_push_notification(...)

env.register_observers([friend_of_a_friend])
```

And just like that, a simple algorithm for recommending new friends is implemented.
