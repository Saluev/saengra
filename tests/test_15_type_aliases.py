# The `type` statement needs Python 3.12+, so conftest.py doesn't collect this module before that.
from typing import Optional

from saengra import Entity, Environment, primitive


@primitive
class a:
    id: int


@primitive
class b:
    id: int


class A(Entity, a):
    parent: Optional["AorB"]
    children: set["AorB"]


class B(Entity, b):
    parent: Optional["AorB"]
    children: set["AorB"]


type AorB = A | B


def make_env() -> Environment:
    return Environment(entity_types=[A, B])


def test_relations_to_type_alias_of_union() -> None:
    env = make_env()
    root = A.create(env, id=1)
    child_a = A.create(env, id=2, parent=root)
    child_b = B.create(env, id=1, parent=root)
    root.children.update([child_a, child_b])
    child_b.children.add(A.create(env, id=3, parent=child_b))
    env.commit()

    assert child_a.parent is root
    assert child_b.parent is root
    assert root.children == {child_a, child_b}
    assert [child.id for child in child_b.children] == [3]


def test_relations_to_type_alias_of_union_are_restored_on_rollback() -> None:
    env = make_env()
    root = B.create(env, id=1)
    root.children.add(A.create(env, id=1, parent=root))
    env.commit()

    B.create(env, id=2, parent=root)
    env.rollback()

    root = B(env, id=1)
    (child,) = root.children
    assert isinstance(child, A)
    assert child.parent is root
