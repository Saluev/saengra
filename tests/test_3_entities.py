from saengra import primitive, Entity, Environment


@primitive
class foo:
    id: int


class Foo(Entity, foo):
    pass


@primitive
class bar:
    id: int


class Bar(Entity, bar):
    pass


@primitive
class container:
    id: int


class Container(Entity, container):
    first: Foo | Bar | None
    contains: set[Foo | Bar]


def test_optional_union():
    env = Environment(entity_types=[Foo, Bar, Container])

    c = Container.create(env, id=1)
    assert c.first is None
    assert c.contains == set()

    c.first = Foo.create(env, id=1)
    c.contains.add(c.first)
    c.contains.add(Bar.create(env, id=1))

    assert c.first == Foo.get(env, id=1)
    assert c.contains == {Foo.get(env, id=1), Bar.get(env, id=1)}
