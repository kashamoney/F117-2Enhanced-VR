"""Build and package the standalone Windows VR ZIP (never bundles game files)."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
RUNTIME = ('SDL3.dll', 'libopenxr_loader.dll', 'libgcc_s_seh-1.dll',
           'libstdc++-6.dll', 'libwinpthread-1.dll', 'libiconv-2.dll')
NOTICES = ('SDL3-LICENSE.txt', 'OpenXR-LICENSE.txt', 'gcc-libs-COPYING3.txt',
           'gcc-libs-COPYING.RUNTIME.txt', 'libwinpthread-COPYING.txt',
           'libiconv-COPYING.LIB.txt', 'Nuked-OPL3-LICENSE.txt')


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def version():
    text = (ROOT / 'f117port/src/enhanced/vr_release.h').read_text(encoding='utf-8')
    return re.search(r'#define F117_VR_VERSION "([^"]+)"', text)[1]


def run(command, **kwargs):
    subprocess.run([str(item) for item in command], check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True,
                        help='read-only original game folder used to exclude game data from the ZIP')
    parser.add_argument('--msys-root', type=Path, default=Path('C:/msys64'))
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'release')
    parser.add_argument('--allow-dirty', action='store_true', help='internal candidate builds only')
    args = parser.parse_args()
    dirty = bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True).strip())
    if dirty and not args.allow_dirty:
        parser.error('Commit source changes before packaging a public release.')
    if not (args.game_dir / 'VGAME.EXE').is_file():
        parser.error('--game-dir must contain the complete original game files')
    tools = args.msys_root / 'mingw64/bin'
    env = dict(os.environ, PATH=str(tools) + os.pathsep + os.environ.get('PATH', ''))
    build = ROOT / 'f117port/build-vr-release'
    run([tools / 'cmake.exe', '-S', ROOT / 'f117port', '-B', build, '-G', 'Ninja',
         '-DCMAKE_C_COMPILER=gcc', '-DCMAKE_CXX_COMPILER=g++', '-DCMAKE_BUILD_TYPE=Release',
         '-DF117_LAUNCHER=OFF', '-DF117_OPENXR=ON', f'-DPython3_EXECUTABLE={sys.executable}'], env=env)
    run([tools / 'cmake.exe', '--build', build, '--parallel', '12'], env=env)
    run([tools / 'ctest.exe', '--test-dir', build, '--output-on-failure', '-R',
         '^(chain|keymap|tick_rate|check_missing_files|platform|video_selftest|compose|enh_interp_rules|enh_wide_geometry|enh_vr_math)$'], env=env)
    name = f'F117-Enhanced-VR-v{version()}-win64'
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    stage, archive = output / name, output / (name + '.zip')
    if stage.exists() or archive.exists():
        parser.error('Output already exists; choose a fresh --output-dir for a new candidate.')
    stage.mkdir()
    files = [build / 'f117-enhanced-vr.exe', *(build / dll for dll in RUNTIME)]
    actual = {p.name.lower() for p in build.glob('*.dll')}
    if actual != {name.lower() for name in RUNTIME}:
        parser.error(f'Runtime DLL set changed; review redistribution notices before packaging: {actual}')
    for source in files:
        shutil.copy2(source, stage / source.name)
    shutil.copy2(ROOT / 'LICENSE', stage / 'LICENSE')
    (stage / 'licenses').mkdir()
    for name in NOTICES:
        shutil.copy2(ROOT / 'licenses' / name, stage / 'licenses' / name)
    (stage / 'README.txt').write_text(f'''F-117A Enhanced VR {version()} - Windows x64 preview

1. Extract this entire ZIP to a writable folder. Keep the EXE and DLLs together.
2. Start SteamVR and make sure your headset is tracking. The tested runtime is
   SteamVR/OpenXR with an Odyssey+ through Oasis; other setups are unverified.
3. Double-click f117-enhanced-vr.exe.
4. Choose your own original DOS game folder when asked. Your choice is remembered
   under %APPDATA%/kashamoney/F117-Enhanced-VR/settings.ini across upgrades.

No Git, Python, PowerShell scripts, MSYS2 or compiler is required to play.
You must own the complete supported MicroProse DOS release of F-117A 2.0.
No original game data, artwork or pilot roster is included. The GOG 473.04
VGAME.EXE build is not supported; unsupported releases stop with a message.
Saves remain in your selected game folder. Back up your existing ROSTER.FIL.

F12: recenter while seated, looking straight ahead. F11: cockpit visibility.
Use original keyboard/gamepad flight controls; keep the mirror window focused.
Alt+P / Enter pauses/resumes. Hold Shift while opening the EXE to choose another
game folder. You can also run f117-enhanced-vr.exe --vr-setup to choose it again.

Optional terminal commands:
  f117-enhanced-vr.exe --vr-preview       desktop stereo preview, no headset
  f117-enhanced-vr.exe --vr-diagnostic    OpenXR/runtime diagnostic
  f117-enhanced-vr.exe --check --game-dir "D:/Games/F117"

This unsigned experimental preview is not an installer. Automated checks cover
game state, rendering and startup; headset comfort/pacing need hardware review.
Logs (f117.log / f117-vr.log) are useful when reporting a problem.
Source, controls and issue reports: https://github.com/kashamoney/F117-2Enhanced
''', encoding='utf-8')
    (stage / 'licenses/THIRD-PARTY.txt').write_text('''The game source is MIT licensed (../LICENSE). This package contains:
SDL3.dll: SDL, zlib license, https://libsdl.org
libopenxr_loader.dll: OpenXR SDK loader, Apache-2.0, https://github.com/KhronosGroup/OpenXR-SDK
libgcc_s_seh-1.dll / libstdc++-6.dll: GCC, GPLv3 with Runtime Library Exception, https://gcc.gnu.org
libwinpthread-1.dll: mingw-w64 winpthreads, https://www.mingw-w64.org
libiconv-2.dll: GNU libiconv, LGPL, https://www.gnu.org/software/libiconv/
Nuked-OPL3: statically linked emulator, LGPL-2.1-or-later, https://github.com/nukeykt/Nuked-OPL3
Full notices are included in this directory. DLLs are unmodified MSYS2 mingw64 builds.
''', encoding='utf-8')
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    manifest = {'version': version(), 'source_commit': commit, 'dirty_source': dirty,
                'files': {p.relative_to(stage).as_posix(): sha256(p) for p in sorted(stage.rglob('*')) if p.is_file()}}
    (stage / 'RELEASE.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    spec = importlib.util.spec_from_file_location('upstream_release_guard', ROOT / 'tools/make_release.py')
    guard_module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(guard_module)
    guard_module.GAME_DIR = args.game_dir.resolve()
    guard = guard_module.GameGuard()
    for path in stage.rglob('*'):
        if path.is_file():
            guard.check_file(path, path.relative_to(stage).as_posix())
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as zipped:
        for path in sorted(stage.rglob('*')):
            if path.is_file():
                zipped.write(path, stage.name + '/' + path.relative_to(stage).as_posix())
    guard_module.NAME = stage.name
    guard_module.check_zip(guard, archive)
    sums = output / 'SHA256SUMS.txt'
    sums.write_text(f'{sha256(archive)}  {archive.name}\n', encoding='utf-8')
    print(f'Package: {archive}')
    print(f'Checksum: {sums}')
    print('Game-data exclusion: all staged files and final ZIP checked by name and content.')


if __name__ == '__main__':
    main()
