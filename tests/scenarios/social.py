from typing import Optional

from saengra import primitive, Entity, observer, Environment, DirectAdapter


@primitive
class user:
    id: int


class User(Entity, user):
    ip: str
    friends: set["User"]
    suggested_friends: set["User"]
    number_of_messages: int


@primitive
class message:
    id: int


class Message(Entity, message):
    written_by: User
    text: str
    replies_to: Optional["Message"]


new_message = observer("message (?= -written_by> user as u)")


@new_message.on_create
def increment_number_of_messages(u: User) -> None:
    u.number_of_messages += 1


@new_message.on_delete
def decrement_number_of_messages(u: User) -> None:
    u.number_of_messages -= 1


friend_of_a_friend = observer(
    "user as u1 <friends> user <friends> (?! u1) user as u2 (?! <friends> u1)"
)


@friend_of_a_friend.on_create
def suggest_befriending_friend_of_a_friend(u1: User, u2: User) -> None:
    u1.suggested_friends.add(u2)


befriended_suggested_friend = observer(
    "user as u1 -suggested_friends> user as u2 <friends> u1"
)


@befriended_suggested_friend.on_create
def remove_suggestion(u1: User, u2: User) -> None:
    u1.suggested_friends.discard(u2)


def make_social_environment() -> Environment:
    env = Environment(
        adapter=DirectAdapter(),
        entity_types=[User, Message],
    )
    env.register_observers(
        [new_message, friend_of_a_friend, befriended_suggested_friend]
    )
    return env
