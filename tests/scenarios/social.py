from typing import Optional

from saengra import primitive, Entity


@primitive
class user:
    id: int


class User(Entity, user):
    ip: str
    friends: set["User"]
    suggested_friends: set["User"]


@primitive
class message:
    id: int


class Message(Entity, message):
    written_by: User
    text: str
    replies_to: Optional["Message"]
