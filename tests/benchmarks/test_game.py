import os
from random import choice, seed

from tests.scenarios.game import make_game_environment, Tile, Biome, Unit, City, Player


def test_game():
    if os.environ.get("SKIP_BENCHMARKS"):
        return

    seed(42)

    env = make_game_environment()

    for row in range(10):
        for col in range(10):
            biome = choice([Biome.GRASSLAND] * 30 + [Biome.STEPPE] * 19 + [Biome.LAKE])
            Tile.create(env, row=row, col=col, biome=biome)
    for row in range(1, 9):
        for col in range(1, 9):
            t = Tile.get(env, row=row, col=col)
            t.adjacent = [
                Tile.get(env, row=row - 1, col=col),
                Tile.get(env, row=row + 1, col=col),
                Tile.get(env, row=row, col=col - 1),
                Tile.get(env, row=row, col=col + 1),
            ]
            for adj_t in t.adjacent:
                adj_t.adjacent.add(t)

    env.commit()
    tiles = [*env.find_all_of_type(Tile)]

    p1 = Player.create(env, id=1)
    p2 = Player.create(env, id=2)

    units: list[Unit] = []
    cities: list[City] = []

    def spawn_unit() -> None:
        t = choice(tiles)
        if t.has_unit:
            return
        unit = Unit.create(
            env,
            id=len(units),
            hp=100,
            stands_on=t,
            owned_by=choice([p1, p2]),
        )
        units.append(unit)

    def move_unit() -> None:
        if not units:
            return
        u = choice(units)
        if not u.alive or not u.can_move_to:
            return
        t = choice([*u.can_move_to])
        u.stands_on.has_unit = None
        u.stands_on = t
        t.has_unit = u

    def spawn_city() -> None:
        t = choice(tiles)
        if t.has_city or t.biome == Biome.LAKE:
            return
        c = City.create(env, id=len(cities), built_on=t, owned_by=choice([p1, p2]))
        t.has_city = c
        cities.append(c)

    for _ in range(10000):
        action = choice([spawn_unit, move_unit, spawn_city])
        action()
        env.commit()
