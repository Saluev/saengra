from typing import Optional

import pytest

from saengra import Entity, Environment, primitive
from saengra.utilities.properties import (
    RelatedEntityProperty,
    RelatedOptionalEntityProperty,
)


class Named:
    __slots__ = ()
    name: str | None

    def greeting(self) -> str:
        return f"Hello, {self.name}!"


class Owned:
    __slots__ = ()
    owner: Optional["Player"]  # defined below


class Tagged(Named):
    __slots__ = ()
    tags: set[str]


@primitive
class player:
    id: int


class Player(Entity, player):
    pass


@primitive
class unit:
    id: int


class Unit(Entity, Owned, Tagged, unit):
    hp: int | None


@primitive
class city:
    id: int


class City(Entity, Named, city):
    name: str  # redeclared: no longer optional
    population: int | None


def make_env() -> Environment:
    return Environment(entity_types=[Player, Unit, City])


def test_relations_are_inherited_from_mixins_in_order() -> None:
    # The most distant mixins first, the class's own annotations last.
    assert Unit._relation_labels == ("name", "tags", "owner", "hp")
    # Redeclared annotations keep their place.
    assert City._relation_labels == ("name", "population")


def test_mixin_relations_work() -> None:
    env = make_env()
    owner = Player.create(env, id=1)
    u = Unit.create(env, id=1, name="Archer", owner=owner, hp=10)
    u.tags.add("ranged")
    env.commit()

    assert (u.name, u.owner, u.hp, u.tags) == ("Archer", owner, 10, {"ranged"})
    assert u.greeting() == "Hello, Archer!"
    assert [refs["u"] for refs in env.match("unit as u -owner> ?", player(1))] == [u]


def test_redeclared_relation_takes_the_latest_type() -> None:
    assert isinstance(Unit.name, RelatedOptionalEntityProperty)
    assert isinstance(City.name, RelatedEntityProperty)

    env = make_env()
    c = City.create(env, id=1, name="Rome")
    env.commit()
    assert c.name == "Rome"


def test_mixin_relations_are_cleared_on_removal() -> None:
    env = make_env()
    owner = Player.create(env, id=1)
    u = Unit.create(env, id=1, name="Archer", owner=owner)
    env.commit()

    owner.remove()
    env.commit()
    assert u.owner is None

    u.remove()
    env.commit()
    assert u.name is None


def test_mixin_relations_are_restored_on_rollback() -> None:
    env = make_env()
    owner = Player.create(env, id=1)
    Unit.create(env, id=1, name="Archer", owner=owner).tags.add("ranged")
    env.commit()

    Unit(env, id=1).update(name="Knight", owner=None)
    env.rollback()

    u = Unit(env, id=1)
    assert (u.name, u.owner, u.tags) == ("Archer", Player(env, id=1), {"ranged"})


def test_entities_dont_have_dict() -> None:
    assert not hasattr(Unit(make_env(), id=1), "__dict__")


class WithoutSlots:
    level: int | None


class WithoutSlotsInSubclass(Named):
    level: int | None


@pytest.mark.parametrize("mixin", [WithoutSlots, WithoutSlotsInSubclass])
def test_mixins_must_define_slots(mixin: type) -> None:
    with pytest.raises(
        TypeError, match=f"mixin {mixin.__qualname__} must define __slots__"
    ):

        class Hero(Entity, mixin, unit):
            pass
