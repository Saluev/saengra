from enum import Enum
from typing import Optional

from saengra import primitive, Entity, observer, Environment, DirectAdapter


@primitive
class player:
    id: int


class Player(Entity, player):
    owned_cities: set["City"]
    owned_tiles: set["Tile"]


class Biome(str, Enum):
    GRASSLAND = "grassland"
    LAKE = "lake"
    STEPPE = "steppe"


@primitive
class tile:
    row: int
    col: int


class Tile(Entity, tile):
    biome: Biome
    adjacent: set["Tile"]
    has_city: Optional["City"]
    has_unit: Optional["Unit"]
    owned_by: Optional[Player]


@primitive
class city:
    id: int


class City(Entity, city):
    built_on: Tile
    owned_by: Player


@primitive
class unit:
    id: int


class Unit(Entity, unit):
    hp: int
    stands_on: Tile
    can_move_to: set[Tile]
    owned_by: Player


unit_on_tile = observer(
    "unit as u -stands_on> tile as t (maybe: (all: <adjacent> tile -has_unit>))"
)


@unit_on_tile.on_create
def update_possible_movements(u: Unit, t: Tile) -> None:
    u.can_move_to = {
        adj_t
        for adj_t in t.adjacent
        if not adj_t.has_unit and adj_t.biome != Biome.LAKE
    }


captured_city = observer(
    "city as c (?= -owned_by> player as p1) -built_on> tile -has_unit> unit -owned_by> (?! p1) player as p2"
)


@captured_city.on_create
def change_city_owner(c: City, p1: Player, p2: Player) -> None:
    p1.owned_cities.discard(c)
    c.owned_by = p2
    p2.owned_cities.add(c)


captured_tile = observer(
    "city (?= -owned_by> player as p) -built_on> tile as t (?! -owned_by> p)"
)


@captured_tile.on_create
def change_tile_owner(t: Tile, p: Player) -> None:
    if t.owned_by:
        t.owned_by.owned_tiles.discard(t)
    t.owned_by = p
    p.owned_tiles.add(t)


def make_game_environment() -> Environment:
    env = Environment(
        adapter=DirectAdapter(),
        entity_types=[Player, Tile, City, Unit],
    )
    env.register_observers([unit_on_tile, captured_city, captured_tile])
    return env
