"""Detect aircraft jumps as a continuously moving camera crosses legacy cells."""
import argparse
import csv
from pathlib import Path
import statistics


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('trace',type=Path)
    p.add_argument('--original-size',type=Path,help='compare silhouette widths against a physical-size visibility CSV')
    a=p.parse_args()
    rows=list(csv.DictReader(a.trace.open(encoding='utf-8')))
    distances=sorted({r.get('distance_units','4096') for r in rows})
    assert len(rows)==128*len(distances),'Need both complete camera sweeps'
    for distance,axis in ((d,axis) for d in distances for axis in (0,1)):
        samples=[r for r in rows if int(r['axis'])==axis and r.get('distance_units','4096')==distance]
        assert all(int(r['vertices'])>0 for r in samples),'Aircraft disappeared during motion'
        changes=[abs(float(b['centre_x'])-float(v['centre_x'])) for v,b in zip(samples,samples[1:])]
        typical=statistics.median(changes)
        ratio=max(changes)/max(typical,1e-9)
        print(f'Distance {distance}, camera axis {axis}: maximum/median aircraft motion {ratio:.2f}x')
        assert ratio<4,f'Aircraft jumps at a cell boundary on axis {axis}: {ratio:.2f}x normal motion'
    print('PASS: aircraft moves continuously across legacy camera-cell boundaries')
    if a.original_size:
        before={r['distance_units']:r for r in csv.DictReader(a.original_size.open(encoding='utf-8'))}
        after=list(csv.DictReader(a.trace.with_name('vr-visibility.csv').open(encoding='utf-8')))
        ratios={r['distance_units']:float(r['model_width'])/float(before[r['distance_units']]['model_width'])
                for r in after if float(before[r['distance_units']]['model_width'])>0}
        assert all(.99<=v<=2.05 for v in ratios.values()),'Unbounded aircraft enlargement'
        assert abs(ratios['256']-1)<.001,'Close aircraft enlarged'
        assert ratios['12288']>1.9,'Distant model silhouette did not grow'
        print('PASS: close size unchanged; distant silhouettes grow gradually up to 2x')


if __name__=='__main__':main()
