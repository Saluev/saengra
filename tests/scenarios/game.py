from enum import Enum
from typing import Optional

from saengra import primitive, Entity


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
    has_unit: set["Unit"]


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
    owned_by: Player
