"""Controlled native-GPU feature comparisons, in disposable game copies.

Checks effect coverage and unchanged surfaces rather than using old software
pixels as an oracle for new styling. Does not start SteamVR.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import uuid

import numpy as np
from PIL import Image
from test_vr import ROOT


def pixels(path):
    return np.asarray(Image.open(path).convert('RGB')).astype(np.int16)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--game-dir', type=Path, required=True)
    a = parser.parse_args()
    directory = ROOT/'work/vr-presentation'/uuid.uuid4().hex[:10]
    directory.mkdir(parents=True)
    results = {}

    def run(name, *extra):
        command = [sys.executable, '-u', str(ROOT/'tools/benchmark_vr.py'),
                   '--exe', str(a.exe.resolve()), '--game-dir', str(a.game_dir.resolve()),
                   '--capture-world', '--frames', '30' if name == 'enabled' else '10', *extra]
        p = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=180)
        (directory/f'{name}.txt').write_text(p.stdout+p.stderr, encoding='utf-8')
        if p.returncode:
            raise RuntimeError(f'{name} failed: {p.stdout[-1000:]} {p.stderr[-1000:]}')
        path = Path(re.search(r'Evidence: (.+)', p.stdout)[1].strip())
        ms = float(re.search(r'VR benchmark: \d+ stereo frames, ([\d.]+) ms/frame', p.stdout)[1])
        assert '0 invalid faces' in p.stdout
        results[name] = {'directory': str(path), 'ms': ms}
        print(f'{name}: {ms:.3f} ms/stereo frame; {path}', flush=True)
        return path

    enabled = run('enabled', '--hud-check')
    meta = [json.loads((enabled/f'vr-presentation-{k}.json').read_text()) for k in range(11)]
    assert all(m['effects'] == 31 and m['distance'] == 8 for m in meta)
    assert max(m['water_faces'] for m in meta) > 0 and max(m['ships'] for m in meta) > 0
    # Head rotation must not choose a different rounded terrain LOD sea plane.
    assert max(m['sea'][3] for m in meta[:10])-min(m['sea'][3] for m in meta[:10]) < 1e-4

    errors = []
    for feature in ('water-detail', 'shorelines', 'wakes', 'atmosphere', 'landing-detail'):
        base_path = enabled
        fixture = []
        if feature == 'shorelines':
            fixture = ['--coast-fixture']
            base_path = run('coast-enabled', *fixture)
        other = run('without-'+feature, '--without', feature, *fixture)
        changes = outside = 0
        for k in range(11):
            om = json.loads((other/f'vr-presentation-{k}.json').read_text())
            bm = json.loads((base_path/f'vr-presentation-{k}.json').read_text())
            assert om['origin'] == bm['origin'] and om['view'] == bm['view'], 'Comparison pose/time changed'
            base = pixels(base_path/f'vr-gpu-reference-{k}.png')
            off = pixels(other/f'vr-gpu-reference-{k}.png')
            changed = np.any(base != off, axis=2)
            mask = pixels(base_path/f'vr-surfaces-{k}.png')
            changes += int(changed.sum())
            outside += int((changed & (mask[:, :, 1] == 0)).sum())
        if not changes:
            errors.append(f'{feature} never affected a visible surface')
        if feature in ('water-detail', 'shorelines', 'wakes'):
            if outside:
                errors.append(f'{feature} affected {outside} non-water pixels')
        results['without-'+feature].update(changed_pixels=changes, outside_water=outside)
        print(f'  {feature}: {changes} changed pixels; outside water: {outside}', flush=True)

    extended = run('extended-range', '--pose', '6,0,1500,0')
    original = run('original-range', '--pose', '6,0,1500,0', '--original-distance')
    extended_meta = [json.loads((extended/f'vr-presentation-{k}.json').read_text()) for k in range(11)]
    original_meta = [json.loads((original/f'vr-presentation-{k}.json').read_text()) for k in range(11)]
    assert max(m['overflow_records'] for m in extended_meta) > 0
    assert all(m['overflow_records'] == 0 and m['distance'] == 1 for m in original_meta)
    assert any(not np.array_equal(pixels(extended/f'vr-gpu-reference-{k}.png'), pixels(original/f'vr-gpu-reference-{k}.png')) for k in range(11))
    results['extended-range']['max_overflow_records'] = max(m['overflow_records'] for m in extended_meta)
    (directory/'results.json').write_text(json.dumps(results, indent=2)+'\n', encoding='utf-8')
    assert not errors, '; '.join(errors)
    print(f'Presentation checks PASS. Evidence: {directory}')


if __name__ == '__main__':
    main()
