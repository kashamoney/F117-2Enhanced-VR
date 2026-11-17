"""Prepare a separate carrier-practice game copy without changing the player's roster."""
import argparse
from pathlib import Path
import shutil
from test_vr import roster


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", type=Path, required=True)
    parser.add_argument("--destination", type=Path, required=True)
    args = parser.parse_args()
    source, destination = args.game_dir.resolve(), args.destination.resolve()
    if not (source / "VGAME.EXE").is_file():
        parser.error("Original game files are missing")
    if destination.exists():
        parser.error("Practice destination must be new")
    destination.mkdir(parents=True)
    for path in source.iterdir():
        if path.is_file():
            shutil.copy2(path, destination / path.name)
    (destination / "ROSTER.FIL").write_bytes(roster())
    print(destination)


if __name__ == "__main__":
    main()
