"""
Loads TTC 2018 models and change sequences (the CSV versions provided by the contest
repository) as sequences of elementary graph operations, plus the official expected results.

Users are mapped to negative IDs, so that they can't collide with post/comment IDs.
Every comment gets a `post` edge to its root post: the metamodel has this reference
(`Comment.post`), but the CSV files only list the direct parent.
"""

import csv
import subprocess
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from tests.benchmarks.train_2017.workload import Op

REPO_URL = "https://github.com/TransformationToolContest/ttc2018liveContest.git"
SEQUENCES = 20


@dataclass
class Workload:
    size: int
    load: list[Op]
    changes: list[list[Op]]
    expected: dict[str, list[str]]
    """ Query -> expected result string after load and after each change set. """

    @property
    def num_elements(self) -> int:
        return sum(1 for op in self.load if op[0] in ("N", "E"))


def ensure_repository(path: Path) -> Path:
    if not (path / "models").exists():
        print(f"Cloning {REPO_URL} into {path} (~350 MB)...", flush=True)
        path.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["git", "clone", "--depth", "1", REPO_URL, str(path)], check=True)
    return path


class _Converter:
    def __init__(self) -> None:
        self.post_of: dict[int, int] = {}

    def user(self, id_: str, ops: list[Op]) -> None:
        ops.append(("N", "User", -int(id_)))

    def post(self, id_: str, timestamp: str, ops: list[Op]) -> None:
        post = int(id_)
        assert post not in self.post_of, f"duplicate submission ID {post}"
        self.post_of[post] = post
        ops.append(("N", "Post", post))
        ops.append(("A", "timestamp", post, timestamp.replace(" ", "T")))

    def comment(self, id_: str, timestamp: str, parent: str, ops: list[Op]) -> None:
        comment, parent_ = int(id_), int(parent)
        assert comment not in self.post_of, f"duplicate submission ID {comment}"
        post = self.post_of[comment] = self.post_of[parent_]
        ops.append(("N", "Comment", comment))
        ops.append(("A", "timestamp", comment, timestamp.replace(" ", "T")))
        ops.append(("E", "commented", comment, parent_))
        ops.append(("E", "post", comment, post))

    @staticmethod
    def friends(u1: str, u2: str, ops: list[Op]) -> None:
        # Friendship is symmetric; the data lists both directions, but don't rely on it.
        ops.append(("E", "friends", -int(u1), -int(u2)))
        ops.append(("E", "friends", -int(u2), -int(u1)))

    @staticmethod
    def likes(user: str, comment: str, ops: list[Op]) -> None:
        ops.append(("E", "likes", -int(user), int(comment)))


def _rows(path: Path) -> Iterable[list[str]]:
    with path.open(newline="", encoding="utf-8-sig") as f:
        yield from csv.reader(f, delimiter="|", quoting=csv.QUOTE_NONE)


def load_workload(repository: Path, size: int) -> Workload:
    models = repository / "models" / str(size)
    conv = _Converter()

    load: list[Op] = []
    for row in _rows(models / "csv-users-initial.csv"):
        conv.user(row[0], load)
    for row in _rows(models / "csv-posts-initial.csv"):
        conv.post(row[0], row[1], load)
    # Comments may reference comments listed later, so resolve parents iteratively.
    pending = list(_rows(models / "csv-comments-initial.csv"))
    while pending:
        postponed = []
        for row in pending:
            if int(row[4]) in conv.post_of:
                conv.comment(row[0], row[1], row[4], load)
            else:
                postponed.append(row)
        assert len(postponed) < len(pending), "comments with unknown parents"
        pending = postponed
    for row in _rows(models / "csv-friends-initial.csv"):
        conv.friends(row[0], row[1], load)
    for row in _rows(models / "csv-likes-initial.csv"):
        conv.likes(row[0], row[1], load)

    changes: list[list[Op]] = []
    for i in range(1, SEQUENCES + 1):
        ops: list[Op] = []
        for row in _rows(models / f"change{i:02d}.csv"):
            match row[0]:
                case "Users":
                    conv.user(row[1], ops)
                case "Posts":
                    conv.post(row[1], row[2], ops)
                case "Comments":
                    conv.comment(row[1], row[2], row[5], ops)
                case "Friends":
                    conv.friends(row[1], row[2], ops)
                case "Likes":
                    conv.likes(row[1], row[2], ops)
                case _:
                    raise ValueError(f"unknown change: {row}")
        changes.append(ops)

    expected: dict[str, list[str]] = defaultdict(lambda: [""] * (SEQUENCES + 1))
    for row in csv.DictReader((repository / "expected-results" / "results.csv").open(), delimiter=";"):
        if row["ChangeSet"] == str(size):
            expected[row["View"]][int(row["Iteration"])] = row["MetricValue"]

    return Workload(size, load, changes, dict(expected))
