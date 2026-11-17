"""Exercise portable Windows launchers with real game-file checks, without a headset.

Copies scripts, built binaries and game data into an isolated checkout-shaped
folder containing spaces. Keeps fixtures and process output under work/.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import uuid

from test_vr import ROOT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--game-dir', type=Path, required=True)
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('These launcher checks require Windows PowerShell.')
    exe, game = args.exe.resolve(), args.game_dir.resolve()
    if not exe.is_file() or not (game / 'VGAME.EXE').is_file():
        parser.error('Supply a built VR executable and the complete original game folder.')
    evidence = ROOT / 'work' / 'vr-launcher-test' / uuid.uuid4().hex[:10]
    checkout = evidence / 'standalone fork with spaces'
    scripts = checkout / 'tools' / 'vr'
    binaries = checkout / 'f117port' / 'build-vr'
    scripts.mkdir(parents=True)
    binaries.mkdir(parents=True)
    for name in ('Start-VR.ps1', 'Build-VR.ps1', 'Carrier-VR.cmd', 'Record-VR.cmd'):
        shutil.copy2(ROOT / 'tools' / 'vr' / name, scripts / name)
    for name in ('Play-VR.cmd', 'Preview-VR.cmd'):
        shutil.copy2(ROOT / name, checkout / name)
    shutil.copy2(exe, binaries / 'f117-enhanced-vr.exe')
    for dll in exe.parent.glob('*.dll'):
        shutil.copy2(dll, binaries / dll.name)

    def copy_game(destination):
        destination.mkdir()
        for source in game.iterdir():
            if source.is_file():
                shutil.copy2(source, destination / source.name)
        return destination

    selected = copy_game(evidence / 'player game with spaces')
    alternate = copy_game(evidence / 'alternate game')
    settings = checkout / '.vr-local.json'
    env = {key: value for key, value in os.environ.items() if not key.startswith('F117_')}
    results = []

    def run(name, extra=(), *, environment=None, expected=0, message=None, launcher=None):
        if launcher:
            command = f'cmd.exe /d /s /c ""{checkout / launcher}" -Check -NoPrompt"'
        else:
            command = ['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                       '-File', str(scripts / 'Start-VR.ps1'), '-Check', '-NoPrompt', *extra]
        proc = subprocess.run(command, cwd=evidence, env=environment or env,
                              input='\n', capture_output=True, text=True, timeout=45)
        output = proc.stdout + proc.stderr
        (evidence / f'{name}.txt').write_text(output, encoding='utf-8')
        if expected == 0:
            assert proc.returncode == 0 and 'game folder ' in output, f'{name}: {output}'
        else:
            assert proc.returncode != 0 and message in output, f'{name}: {output}'
        results.append(name)
        print(f'PASS: {name}', flush=True)

    def remembered(path):
        actual = Path(json.loads(settings.read_text(encoding='utf-8'))['GameDirectory'])
        assert actual == path, f'Wrong remembered game folder: {actual}'

    run('missing-folder', expected=1, message='Choose your original game folder')
    assert not settings.exists(), 'Missing input wrote settings'
    run('explicit-folder', ['-GameDirectory', str(selected)])
    remembered(selected)
    run('remembered-folder')
    remembered(selected)
    overrides = dict(env, F117_GAME_DIR=str(alternate))
    run('environment-overrides-settings', environment=overrides)
    remembered(alternate)
    run('argument-overrides-environment', ['-GameDirectory', str(selected)], environment=overrides)
    remembered(selected)
    run('play-shortcut', launcher=Path('Play-VR.cmd'))
    run('preview-shortcut', launcher=Path('Preview-VR.cmd'))
    run('practice-shortcut-check', launcher=Path('tools/vr/Carrier-VR.cmd'))
    run('record-shortcut-check', launcher=Path('tools/vr/Record-VR.cmd'))
    settings.unlink()
    local_game = copy_game(checkout / 'Game')
    run('repository-game-folder')
    remembered(local_game)
    run('invalid-explicit-folder', ['-GameDirectory', str(evidence)], expected=1,
        message='does not contain VGAME.EXE')
    settings.write_text('{broken json', encoding='utf-8')
    run('invalid-settings', expected=1, message='Cannot read .vr-local.json')
    run('explicit-folder-repairs-settings', ['-GameDirectory', str(selected)])
    remembered(selected)
    (evidence / 'results.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
    print(f'Portable launcher checks: {len(results)} passed. Evidence: {evidence}')


if __name__ == '__main__':
    main()
