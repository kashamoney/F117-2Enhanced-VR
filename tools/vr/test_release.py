"""Verify an extracted VR download without scripts or build tools on its PATH."""
import argparse
import configparser
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import uuid
import zipfile

from test_vr import ROOT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--check-dialog', action='store_true', help='open and cancel only the test process first-run folder picker')
    args = parser.parse_args()
    evidence = ROOT / 'work/vr-release-test' / uuid.uuid4().hex[:10]
    extracted = evidence / 'download with spaces and \u00e9'
    extracted.mkdir(parents=True)
    with zipfile.ZipFile(args.package.resolve()) as archive:
        for entry in archive.infolist():
            assert (extracted / entry.filename).resolve().is_relative_to(extracted.resolve()), entry.filename
        archive.extractall(extracted)
    exe = next(extracted.rglob('f117-enhanced-vr.exe'))
    application = exe.parent
    manifest = json.loads((application / 'RELEASE.json').read_text())
    for name, digest in manifest['files'].items():
        assert hashlib.sha256((application / name).read_bytes()).hexdigest() == digest, name
    assert not list(application.rglob('*.ps1')) and not list(application.rglob('*.cmd'))
    assert not (application / 'Game').exists()
    env = {key: value for key, value in os.environ.items() if not key.startswith('F117_')}
    env['PATH'] = os.pathsep.join([str(Path(os.environ['SystemRoot']) / 'System32'), os.environ['SystemRoot']])
    cwd = evidence / 'unrelated working directory'
    cwd.mkdir()
    original = args.game_dir.resolve()
    game = evidence / 'original game with \u00e9 and spaces'
    game.mkdir()
    for path in original.iterdir():
        if path.is_file(): shutil.copy2(path, game / path.name)
    before = hashlib.sha256((original / 'ROSTER.FIL').read_bytes()).hexdigest()
    profile = evidence / 'user settings/settings.ini'
    results = []

    def run(name, arguments, expected=0, contains=None, environment=None):
        process = subprocess.run([str(exe), *map(str, arguments)], cwd=cwd, env=environment or env,
                                 capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=120)
        output = process.stdout + process.stderr
        (evidence / (name + '.txt')).write_text(output, encoding='utf-8')
        assert (process.returncode == 0) == (expected == 0), (name, process.returncode, output)
        if contains: assert contains in output, (name, output)
        results.append(name)
        print('PASS:', name, flush=True)

    run('version-with-clean-path', ['--version'], contains=manifest['version'])
    run('native-selftest-with-clean-path', ['--vr-selftest'], contains='vr-selftest OK')
    run('first-use-without-dialog', ['--headless', '--check', '--vr-config', profile], expected=1, contains='Use --game-dir')
    run('remember-game-folder', ['--headless', '--check', '--vr-config', profile, '--game-dir', game], contains='supported release')
    settings = configparser.ConfigParser()
    settings.read(profile, encoding='utf-16')
    assert Path(settings['VR']['GameDirectory']) == game
    run('remembered-game-folder', ['--headless', '--check', '--vr-config', profile], contains=str(game))
    alternate = dict(env, F117_GAME_DIR=str(game))
    run('environment-game-folder', ['--headless', '--check', '--vr-config', evidence / 'environment.ini'], environment=alternate)
    run('bad-game-folder', ['--headless', '--check', '--vr-config', profile, '--game-dir', cwd], expected=1, contains='Missing F117.COM')
    # Damage only a disposable copy; an unsupported build must fail before rendering.
    vgame = game / 'VGAME.EXE'
    saved = vgame.read_bytes()
    vgame.write_bytes(b'not an original DOS executable')
    run('reject-unsupported-game-build', ['--headless', '--check', '--vr-config', profile], expected=1, contains='not a readable original DOS program')
    vgame.write_bytes(saved)
    # Remove the remembered setting and validate Game beside the EXE from an unrelated cwd.
    portable_game = application / 'Game'
    shutil.copytree(game, portable_game)
    run('game-beside-executable', ['--headless', '--check', '--vr-config', evidence / 'portable.ini'])
    # This process executes the actual no-argument double-click path, but its private
    # profile is empty and we cancel its folder picker before OpenXR can start.
    if args.check_dialog:
        moved_game = evidence / 'preserved portable game'
        assert portable_game.resolve().is_relative_to(evidence.resolve())
        assert moved_game.resolve().is_relative_to(evidence.resolve())
        shutil.move(str(portable_game), str(moved_game))
        gui_env = dict(env, F117_VR_CONFIG=str(evidence / 'first-run/settings.ini'))
        child = subprocess.Popen([str(exe)], cwd=cwd, env=gui_env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        user32 = ctypes.WinDLL('user32', use_last_error=True)
        callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        user32.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
        user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
        user32.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
        user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
        found = []

        @callback_type
        def visit(window, unused):
            pid = wintypes.DWORD()
            user32.GetWindowThreadProcessId(window, ctypes.byref(pid))
            name = ctypes.create_unicode_buffer(128)
            user32.GetClassNameW(window, name, len(name))
            if pid.value == child.pid and name.value == '#32770': found.append(window)
            return True

        try:
            deadline = time.monotonic() + 20
            while not found and child.poll() is None and time.monotonic() < deadline:
                user32.EnumWindows(visit, 0)
                time.sleep(.1)
            assert found, 'No native first-run folder picker appeared'
            user32.PostMessageW(found[0], 0x111, 2, 0)  # IDCANCEL in this process only.
            output, error = child.communicate(timeout=20)
            assert child.returncode == 0, (output, error)
        finally:
            if child.poll() is None:
                child.terminate()
                child.communicate(timeout=10)
        results.append('double-click-native-picker-cancel')
        print('PASS: double-click-native-picker-cancel', flush=True)
    # A real D3D11 stereo preview with remembered native settings and no runtime scripts.
    preview_env = dict(env, F117_KEYS='1:31,2:03,11:39', F117_VCLOCK='1', F117_WINDOW='1024x1024',
                       F117_SHELL_SCRIPT=str(ROOT / 'port/notes/s54/shell_chain_libya.txt'),
                       F117_END_SCRIPT=str(ROOT / 'port/notes/s58/end_chain.txt'),
                       F117_VGAME_FRAMES='400', F117_VR_BENCHMARK='1',
                       F117_VR_PREVIEW_HIDDEN='1', F117_VR_BENCH_FRAMES='10', F117_VR_BENCH_WARMUP='2')
    from test_vr import roster
    (game / 'ROSTER.FIL').write_bytes(roster())
    run('native-remembered-stereo-preview', ['--vr-preview', '--vr-config', profile, '--vr-resolution', '1024', '--fps', 'off'],
        environment=preview_env, contains='VR game folder')
    log = (application / 'f117-vr.log').read_text(encoding='utf-8')
    assert 'VR benchmark: 10 stereo frames' in log, log
    assert hashlib.sha256((original / 'ROSTER.FIL').read_bytes()).hexdigest() == before
    (evidence / 'results.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
    print(f'Standalone release checks: {len(results)} passed. Evidence: {evidence}')


if __name__ == '__main__':
    main()
