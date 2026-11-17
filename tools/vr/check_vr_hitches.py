"""Measure real-clock stereo submission gaps while holding a throttle key.

Uses the actual BIOS key/input/instrument/render path in disposable game copies.
The control and repeated-key scenarios share the same startup and airborne pose.
"""
import argparse
import csv
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import uuid
from test_vr import ROOT, roster


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', type=Path, required=True)
    p.add_argument('--game-dir', type=Path, required=True)
    p.add_argument('--case', choices=('control', 'held', 'both'), default='both')
    p.add_argument('--headset',action='store_true',help='use the actual OpenXR runtime instead of the 90 Hz preview wait')
    p.add_argument('--reversals',action='store_true',help='repeat the original ramp/decrease/ramp reproduction')
    p.add_argument('--draw-distance',type=int,choices=(1,2,4,8),default=8)
    p.add_argument('--budget-ms', type=float, default=1000/90)
    a = p.parse_args()
    directory = ROOT/'work/vr-hitches'/uuid.uuid4().hex[:10]
    directory.mkdir(parents=True)
    results = {}
    for case in (('control', 'held') if a.case == 'both' else (a.case,)):
        dest = directory/case
        game = dest/'game'
        game.mkdir(parents=True)
        for source in a.game_dir.resolve().iterdir():
            if source.is_file():
                shutil.copy2(source, game/source.name)
        (game/'ROSTER.FIL').write_bytes(roster())
        keys = ['1:31', '2:03', '11:39']
        env = {k: v for k, v in os.environ.items() if not k.startswith('F117_')}
        env.update(F117_KEYS=','.join(keys), F117_VR_TIMING='1',
                   F117_SHELL_SCRIPT=str(ROOT/'port/notes/s54/shell_chain_libya.txt'),
                   F117_END_SCRIPT=str(ROOT/'port/notes/s58/end_chain.txt'),
                   F117_VGAME_FRAMES='400', F117_VR_BENCHMARK='1',
                   F117_VR_BENCH_FRAMES='900', F117_VR_BENCH_WARMUP='0',
                   F117_VR_PREVIEW_HIDDEN='1', F117_TEST_POSE='6,0,3000,0',
                   F117_VR_TEST_INPUT=case,F117_VR_TEST_HZ='90')
        if a.reversals and case=='held':env['F117_VR_TEST_INPUT']='held-reverse'
        if a.headset:env.pop('F117_VR_TEST_HZ')
        command = [str(a.exe.resolve()), '--vr' if a.headset else '--vr-preview', '--game-dir', str(game),
                   '--fps', 'vsync', '--dither', 'fine','--vr-draw-distance',str(a.draw_distance)]
        proc = subprocess.run(command, cwd=dest, env=env, capture_output=True, text=True, timeout=90)
        (dest/'stdout.txt').write_text(proc.stdout, encoding='utf-8')
        (dest/'stderr.txt').write_text(proc.stderr, encoding='utf-8')
        if proc.returncode:
            raise RuntimeError(f'{case}: game failed ({proc.returncode}): {proc.stderr[-1200:]}')
        with (dest/'vr-timing.csv').open(encoding='utf-8') as f:
            rows = [r for r in csv.DictReader(f) if r['eye'] == '0']
        intervals = [(int(b['clock_ns'])-int(v['clock_ns']))/1e6
                     for v, b in zip(rows[20:-1], rows[21:])]
        assert len(intervals) > 500, 'Too few stereo submissions'
        ordered = sorted(intervals)
        median = statistics.median(intervals)
        spikes = [round(v, 3) for v in intervals if v > max(a.budget_ms, median*3)]
        results[case] = dict(median_ms=median, p99_ms=ordered[int(len(ordered)*.99)],
                             max_ms=max(intervals), spikes=spikes, submissions=len(rows))
        changes=[r for i,r in enumerate(rows) if i==0 or rows[i-1]['frame']!=r['frame']]
        steps=sorted((int(b['clock_ns'])-int(v['clock_ns']))/1e6 for v,b in zip(changes[6:],changes[7:]))
        results[case].update(world_step_p95_ms=steps[int(len(steps)*.95)],world_step_max_ms=max(steps),
                             throttle_values=sorted({int(r['throttle']) for r in rows}))
        print(f'{case}: median {median:.3f} ms, p99 {results[case]["p99_ms"]:.3f} ms, '
              f'max {max(intervals):.3f} ms; {len(spikes)} hitches > {max(a.budget_ms, median*3):.2f} ms', flush=True)
        print(f'  world step p95/max: {results[case]["world_step_p95_ms"]:.2f}/{max(steps):.2f} ms; throttle {results[case]["throttle_values"]}',flush=True)
    (directory/'results.json').write_text(json.dumps(results, indent=2)+'\n', encoding='utf-8')
    print(f'Evidence: {directory}', flush=True)
    if 'held' in results:
        assert len(results['held']['throttle_values'])>=5, 'Throttle input scenario did not reach the flight controls'
        reference=results.get('control',{}).get('world_step_p95_ms',110)
        assert results['held']['world_step_p95_ms']<=reference+a.budget_ms, 'Held throttle delays world updates by more than one headset frame'


if __name__ == '__main__':
    main()
