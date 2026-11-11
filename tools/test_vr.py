"""Exercise stereo rendering on the real carrier scene without a headset.

Copies game data into disposable test folders. The supplied game folder is read only.
Compares the entire scripted mission's engine hashes and pilot roster with VR off/on.
Usage: python tools/test_vr.py --exe PATH --game-dir PATH [--work-dir PATH]
"""
from __future__ import annotations
import argparse
import os
from pathlib import Path
import shutil
import struct
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[1]


def roster() -> bytes:
    # Same deterministic fixture as port/notes/s54/shell_emu.py, without emulator dependencies.
    result = bytearray(struct.pack("<H", 3))
    for i in range(10):
        r = bytearray(80)
        struct.pack_into("<H", r, 0, i)
        name = f"Test Pilot {i}".encode()
        r[2:2 + len(name)] = name
        struct.pack_into("<H", r, 0x20, i % 7)
        struct.pack_into("<6H", r, 0x22, i % 3, (i + 1) % 4, 0, i % 2, 0, 0)
        struct.pack_into("<HHIHHHHHHH", r, 0x2E, 100 + 10 * i, 200 + i,
                         12345 * (i + 1), 3 * i, 0, 1, 1, 1, 1, 0)
        struct.pack_into("<H", r, 0x4E, 1 if i == 8 else 2 if i == 9 else 0)
        result.extend(r)
    return bytes(result)


def run(exe: Path, game: Path, directory: Path, stereo: bool, preview: bool = False) -> tuple[list[str], bytes]:
    directory.mkdir(parents=True)
    scratch = directory / "game"
    scratch.mkdir()
    for path in game.iterdir():
        if path.is_file():
            shutil.copy2(path, scratch / path.name)
    (scratch / "ROSTER.FIL").write_bytes(roster())
    env = dict(os.environ)
    for key in list(env):
        if key.startswith("F117_"):
            env.pop(key)
    env.update(F117_KEYS="1:31,2:03,11:39", F117_VCLOCK="1", F117_WINDOW="1024x1024",
               F117_SHELL_SCRIPT=str(ROOT / "port/notes/s54/shell_chain_libya.txt"),
               F117_END_SCRIPT=str(ROOT / "port/notes/s58/end_chain.txt"),
               F117_VGAME_DIVE="40", F117_VGAME_FRAMES="400")
    command = [str(exe), "--headless", "--trace", "--game-dir", str(scratch),
               "--fps", "off", "--aspect", "1:1", "--render-scale", "1",
               "--sandbox-hash", str(directory / "hash.txt")]
    if stereo:
        command += ["--vr-scene-test", str(directory)]
    if preview:
        command.remove("--headless")
        command += ["--vr-preview", "--vr-dump"]
    proc = subprocess.run(command, cwd=directory, env=env, capture_output=True, text=True, timeout=180)
    (directory / "stdout.txt").write_text(proc.stdout, encoding="utf-8")
    (directory / "stderr.txt").write_text(proc.stderr, encoding="utf-8")
    if proc.returncode:
        raise RuntimeError(f"Game returned {proc.returncode}\n{proc.stdout[-1200:]}\n{proc.stderr[-2000:]}")
    if "quit: START returned 0" not in proc.stdout:
        raise RuntimeError("Scripted mission did not finish")
    if stereo and "vr-scene-test OK" not in proc.stdout:
        raise RuntimeError("Stereo scene was not exercised")
    if preview and not all((directory / name).is_file() for name in ("vr-left.png", "vr-right.png", "vr-mirror.png")):
        raise RuntimeError("D3D11 preview never rendered a flight scene")
    lines = (directory / "hash.txt").read_text().splitlines()
    hashes = [line for line in lines if line[:1].isdigit() or line.startswith("final ")]
    if len(hashes) < 20:
        raise RuntimeError("Too few simulation frames checked")
    if stereo:
        print(next(line for line in proc.stdout.splitlines() if "vr-scene-test OK" in line))
    return hashes, (scratch / "ROSTER.FIL").read_bytes()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--game-dir", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, default=ROOT / "work/vr-check")
    parser.add_argument("--preview-smoke", action="store_true", help="also run the native D3D11 preview window")
    args = parser.parse_args()
    directory = args.work_dir.resolve() / uuid.uuid4().hex[:10]
    exe, game = args.exe.resolve(), args.game_dir.resolve()
    if not (game / "VGAME.EXE").is_file():
        parser.error("--game-dir must contain the original game files")
    baseline = run(exe, game, directory / "desktop", False)
    stereo = run(exe, game, directory / "stereo", True)
    if baseline != stereo:
        raise RuntimeError(f"Stereo rendering changed simulation hashes or pilot roster. Inspect {directory}")
    print(f"VR integration OK: {len(baseline[0])} engine hashes and final roster match exactly.")
    print(f"Scene images and logs: {directory / 'stereo'}")
    if args.preview_smoke:
        run(exe, game, directory / "preview", False, preview=True)
        print(f"D3D11 stereo mirror and live cockpit smoke test OK: {directory / 'preview'}")


if __name__ == "__main__":
    main()
