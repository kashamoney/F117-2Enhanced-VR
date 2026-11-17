"""Reject aircraft dot/model gaps and a range setting that fails to extend meshes."""
import argparse
import csv
from pathlib import Path
import re
import subprocess
import sys
from test_vr import ROOT


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe',type=Path)
    p.add_argument('--game-dir',type=Path)
    p.add_argument('--draw-distance',type=int,choices=(1,2,4,8),default=8)
    p.add_argument('--evidence',type=Path,help='check a previously recorded, unmodified sweep')
    a=p.parse_args()
    path=a.evidence
    if path is None:
        if not a.exe or not a.game_dir:p.error('Use --exe and --game-dir, or --evidence')
        command=[sys.executable,'-u',str(ROOT/'tools/vr/benchmark_vr.py'),'--exe',str(a.exe.resolve()),
                 '--game-dir',str(a.game_dir.resolve()),'--visibility-test','--frames','10',
                 '--draw-distance',str(a.draw_distance)]
        proc=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=180)
        print(proc.stdout,flush=True)
        if proc.returncode:raise RuntimeError(proc.stderr[-2000:])
        path=Path(re.search(r'Evidence: (.+)',proc.stdout)[1].strip())/'vr-visibility.csv'
    with path.open(encoding='utf-8') as f:rows=list(csv.DictReader(f))
    gaps=[r for r in rows if int(r['model_triangles'])==0 and int(r['dots'])==0]
    meshes=[float(r['metres']) for r in rows if int(r['model_triangles'])>0]
    print(f'{len(rows)} distance steps; {len(gaps)} missing representations; furthest mesh {max(meshes):.1f} m',flush=True)
    assert not any(int(r['invalid']) for r in rows),'Invalid captured faces'
    assert len([r for r in rows if int(r.get('rendered_pixels',-1))>=0])>=7,'Missing actual GPU coverage samples'
    assert not any(int(r.get('rendered_pixels',-1))==0 for r in rows),'Aircraft geometry/marker produces no actual pixels'
    assert not gaps,f'Aircraft disappears between dot and model: {gaps[0]["metres"]}..{gaps[-1]["metres"]} m'
    assert max(meshes)>=1400*a.draw_distance,'Model meshes still stop at the old projection/depth limit'
    print(f'PASS: continuous aircraft representation and extended {a.draw_distance}x model range. {path}')


if __name__=='__main__':main()
