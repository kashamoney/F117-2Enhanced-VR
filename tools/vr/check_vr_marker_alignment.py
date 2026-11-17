"""Compare the distant point's actual draw projection with the 3D model origin."""
import argparse
import csv
import math
from pathlib import Path


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('trace',type=Path)
    a=p.parse_args()
    rows=list(csv.DictReader(a.trace.open(encoding='utf-8')))
    assert len(rows)==12 and all(int(r['valid']) for r in rows),'Missing head pose/reference'
    errors=[math.hypot(float(r['marker_x'])-float(r['model_x']),float(r['marker_y'])-float(r['model_y'])) for r in rows]
    print(f'Maximum point/model angular-ray difference: {max(errors):.6f}')
    assert max(errors)<.001,'Distant points move on a different projection from the world'
    print('PASS: point/model alignment through yaw, pitch, roll and tracked translation')


if __name__=='__main__':main()
