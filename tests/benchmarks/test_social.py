import os
from random import choice, sample, seed

from tests.scenarios.social import make_social_environment, User, Message


def test_social() -> None:
    """
    Test on naive implementation of social network friend recommendations.

    The algorithm is quadratic in nature, so this is not supposed to be very fast.
    """

    if os.environ.get("SKIP_BENCHMARKS"):
        return
    env = make_social_environment()

    users: list[User] = []
    messages: list[Message] = []

    def register_new_user() -> None:
        user = User.create(env, id=len(users), ip="127.0.0.1", number_of_messages=0)
        users.append(user)

    def write_new_message() -> None:
        if not users:
            return
        author = choice(users)
        message = Message.create(
            env, id=len(messages), written_by=author, text="Hello!"
        )
        messages.append(message)

    def write_new_reply() -> None:
        if not users or not messages:
            return
        author = choice(users)
        message = choice(messages)
        reply = Message.create(
            env, id=len(messages), written_by=author, text="Hi!", replies_to=message
        )
        messages.append(reply)

    def befriend_a_user() -> None:
        if len(users) < 2:
            return
        user1, user2 = sample(users, 2)
        user1.friends.add(user2)
        user2.friends.add(user1)

    def befriend_suggested_friend() -> None:
        if not users:
            return
        user1 = choice(users)
        if not user1.suggested_friends:
            return
        user2 = choice([*user1.suggested_friends])
        user1.friends.add(user2)
        user2.friends.add(user1)

    seed(42)

    for _ in range(100):
        register_new_user()
    env.commit()

    for _ in range(4000):
        action = choice(
            [
                register_new_user,
                write_new_message,
                write_new_reply,
                befriend_a_user,
                befriend_suggested_friend,
            ]
        )
        action()
        env.commit()
