"""Reject world interpolation restarting while the same simulation pair is shown."""
import argparse
import csv
from pathlib import Path


def check(path):
    with Path(path).open(encoding='utf-8') as f:
        rows = list(csv.DictReader(f))
    frames = {r['frame'] for r in rows}
    if len(frames) < 10:
        raise AssertionError('Need a moving mission: fewer than ten simulation frames captured')
    if rows and 'submission' in rows[0]:
        if len(rows)%2:
            raise AssertionError('Incomplete stereo submission in timing trace')
        for left, right in zip(rows[::2],rows[1::2]):
            fields=('submission','display_time','clock_ns','frame','alpha','x','y','heading','pitch','roll')
            if left['eye']!='0' or right['eye']!='1' or any(left[k]!=right[k] for k in fields):
                raise AssertionError(f'Eyes use different world state or predicted time: {left}, {right}')
        print(f'{len(rows)//2} stereo submissions: both eyes use the same world pose, alpha and display time')
    resets = []
    for before, after in zip(rows, rows[1:]):
        if before['frame'] != after['frame']:
            continue
        drop = float(before['alpha']) - float(after['alpha'])
        if drop > .25:
            resets.append((after['frame'], round(float(before['alpha']), 3),
                           round(float(after['alpha']), 3)))
    print(f'{len(rows)} samples across {len(frames)} simulation frames; '
          f'{len(resets)} interpolation restarts within the same simulation pair')
    if resets:
        raise AssertionError(f'World moves backward within a simulation step: {resets[:8]}')
    print('PASS: world interpolation advances without restarting the current pair')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('trace', type=Path)
    check(p.parse_args().trace)
