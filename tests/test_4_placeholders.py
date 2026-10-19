import pytest

from saengra import primitive, Entity, Environment, observer


@primitive
class user:
    id: int


class User(Entity, user):
    friends: set["User"]


def test_observer_with_too_few_placeholder_values(empty_env: Environment) -> None:
    friend_of = observer("user as u -friends> ?", dont_register=True)
    with pytest.raises(ValueError, match="1 placeholder"):
        empty_env.register_observers([friend_of])


def test_observer_with_too_many_placeholder_values(empty_env: Environment) -> None:
    friend_of = observer("user as u -friends> ?", user(1), user(2), dont_register=True)
    with pytest.raises(ValueError, match="2 placeholder value"):
        empty_env.register_observers([friend_of])


def test_match_with_wrong_number_of_placeholder_values(empty_env: Environment) -> None:
    with pytest.raises(ValueError):
        empty_env.match("user as u -friends> ?")
    with pytest.raises(ValueError):
        empty_env.match("user as u", user(1))


def test_match_with_correct_placeholder_values(empty_env: Environment) -> None:
    u1 = User.create(empty_env, id=1)
    u2 = User.create(empty_env, id=2)
    u1.friends.add(u2)
    empty_env.commit()
    assert empty_env.match("user as u -friends> ?", user(2)) == [{"u": u1}]
