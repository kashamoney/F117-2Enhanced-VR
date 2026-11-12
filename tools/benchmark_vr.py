"""Benchmark the real stereo render/upload/GPU path in a hidden preview.

Uses a disposable game copy and deterministic carrier mission; never changes the
player's pilot. Excludes headset pacing, includes both eyes and the cockpit.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import uuid
from test_vr import ROOT, roster


def check_carrier_deck(actual, pose):
    # The software near clipper leaves gaps in very close deck polygons. Check
    # actual GPU deck coverage rather than treating those gaps as an oracle.
    deck=(actual.max(axis=2)-actual.min(axis=2)<=5)&(actual.max(axis=2)>45)&(actual.max(axis=2)<200)
    coverage=float(deck.mean())
    print(f'Carrier deck coverage ({pose}): {coverage:.1%}')
    if coverage<.8:
        raise RuntimeError(f'Carrier deck missing in {pose} view')
    return coverage


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', type=Path, required=True)
    p.add_argument('--game-dir', type=Path, required=True)
    p.add_argument('--resolution', type=int, default=0)
    p.add_argument('--software', action='store_true')
    p.add_argument('--samples', type=int, choices=(1,2,4,8), help='limit GPU MSAA for before/after comparisons (default 8)')
    p.add_argument('--headset', action='store_true')
    p.add_argument('--validate', action='store_true', help='also compare GPU/software image interiors (requires numpy and Pillow)')
    p.add_argument('--temporal', action='store_true', help='capture and check a frozen airborne roll sweep')
    p.add_argument('--pacing', action='store_true', help='use the real simulation clock and trace both submitted eye poses')
    p.add_argument('--capture-live', action='store_true', help='with --pacing, save 180 live stereo mirror frames at shutdown')
    p.add_argument('--capture-size', type=int, default=512)
    p.add_argument('--capture-steps', type=int, default=40)
    p.add_argument('--hz', type=float, default=90)
    p.add_argument('--frames', type=int, help='default 900 for pacing checks, otherwise 30')
    p.add_argument('--warmup', type=int, help='default 5 preview frames or 120 headset frames, allowing runtime pacing to settle')
    a = p.parse_args()
    if a.frames is None:
        a.frames = 900 if a.pacing else 30
    if not 1 <= a.frames <= 900 or a.hz <= 0 or (a.warmup is not None and not 0 <= a.warmup <= 900):
        p.error('Use 1..900 frames, a positive refresh rate, and 0..900 warmup frames')
    if (a.validate or a.temporal or a.samples is not None) and a.software:
        p.error('--validate, --temporal and --samples require the GPU renderer')
    if a.validate and a.temporal:
        p.error('Run carrier validation and the airborne temporal capture separately')
    if (a.pacing and (a.temporal or a.validate)) or (a.capture_live and not a.pacing):
        p.error('Use --pacing separately from frozen/image checks; --capture-live requires --pacing')
    if a.capture_size not in (256,512,1024) or not 16 <= a.capture_steps <= 360:
        p.error('Capture size must be 256, 512 or 1024; use 16..360 capture steps')
    directory = ROOT / 'work/vr-benchmark' / uuid.uuid4().hex[:10]
    game = directory / 'game'
    game.mkdir(parents=True)
    for source in a.game_dir.resolve().iterdir():
        if source.is_file():
            shutil.copy2(source, game / source.name)
    (game / 'ROSTER.FIL').write_bytes(roster())
    env = {k: v for k, v in os.environ.items() if not k.startswith('F117_')}
    env.update(F117_KEYS='1:31,2:03,11:39', F117_VCLOCK='1',
               F117_SHELL_SCRIPT=str(ROOT / 'port/notes/s54/shell_chain_libya.txt'),
               F117_END_SCRIPT=str(ROOT / 'port/notes/s58/end_chain.txt'),
               F117_VGAME_FRAMES='400', F117_VR_BENCHMARK='1', F117_VR_PREVIEW_HIDDEN='1',
               F117_VR_BENCH_FRAMES=str(a.frames),
               F117_VR_BENCH_WARMUP=str(a.warmup if a.warmup is not None else 120 if a.headset else 5))
    if a.validate or a.temporal:
        env['F117_VR_VALIDATE'] = '1'
    if a.samples is not None:
        env['F117_VR_SAMPLES'] = str(a.samples)
    if a.temporal:
        env.update(F117_VR_TEMPORAL='1',F117_TEST_POSE='6,0,1500,0',
                   F117_VR_CAPTURE_SIZE=str(a.capture_size),F117_VR_CAPTURE_STEPS=str(a.capture_steps))
    if a.pacing:
        env.pop('F117_VCLOCK',None)
        env.update(F117_VR_TIMING='1',F117_TEST_POSE='6,0,1500,8192')
    if a.capture_live:
        env['F117_VR_CAPTURE_LIVE']='1'
    cmd = [str(a.exe.resolve()), '--vr' if a.headset else '--vr-preview', '--vr-resolution', str(a.resolution),
           '--game-dir', str(game), '--fps', 'vsync' if a.pacing else 'off', '--dither', 'fine']
    if a.software:
        cmd += ['--vr-software']
    if a.validate or a.temporal:
        cmd += ['--vr-dump']
    result = subprocess.run(cmd, cwd=directory, env=env, capture_output=True, text=True, timeout=120)
    (directory / 'stdout.txt').write_text(result.stdout, encoding='utf-8')
    (directory / 'stderr.txt').write_text(result.stderr, encoding='utf-8')
    log = (directory / 'f117-vr.log').read_text(encoding='utf-8')
    print(log)
    print(f'Evidence: {directory}')
    match = re.search(r'VR benchmark: \d+ stereo frames, ([\d.]+) ms/frame', log)
    if result.returncode or not match:
        raise RuntimeError(f'Benchmark failed ({result.returncode}): {result.stderr[-2000:]}')
    if a.temporal:
        from check_vr_temporal import check
        check(directory)
    if a.pacing:
        from check_vr_pacing import check
        check(directory/'vr-timing.csv')
    if a.validate:
        import numpy as np
        from PIL import Image
        scene=json.loads((directory/'vr-scene.json').read_text())
        if not (scene['carrier'] and scene['deck']==128 and scene['aircraft']>=scene['deck']):
            raise RuntimeError(f'Validation fixture is not a settled carrier start: {scene}')
        for k, pose in enumerate(('forward', 'yaw', 'pitch', 'roll', 'deck', 'port-deck', 'starboard-deck')):
            reference = np.array(Image.open(directory / f'vr-software-reference-{k}.png').convert('RGB')).astype(int)
            actual = np.array(Image.open(directory / f'vr-gpu-reference-{k}.png').convert('RGB')).astype(int)
            interior = np.ones(reference.shape[:2], dtype=bool)
            for axis in (0, 1):
                for offset in (-2, -1, 1, 2):
                    interior &= np.all(reference == np.roll(reference, offset, axis=axis), axis=2)
            difference = np.max(abs(reference - actual), axis=2)
            agreement = np.mean(difference[interior] <= 3)
            print(f'Matched-projection {pose} interiors: {agreement:.3%} agree within 3/255 RGB')
            if k>=4:
                check_carrier_deck(actual,pose)
            elif agreement < .98:
                raise RuntimeError('GPU image differs materially from the original software scene')
    ms = float(match[1])
    if ms > 1000 / a.hz:
        print(f'FAIL: {ms:.2f} ms exceeds {a.hz:g} Hz budget of {1000/a.hz:.2f} ms')
        raise SystemExit(1)
    print(f'PASS: {ms:.2f} ms within {a.hz:g} Hz rendering budget')


if __name__ == '__main__':
    main()
