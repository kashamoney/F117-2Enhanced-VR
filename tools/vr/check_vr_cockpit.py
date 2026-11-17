"""Check live instrument coverage and rendered HUD bearings in cockpit dumps."""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


def rgb(path):
    return np.asarray(Image.open(path).convert('RGB')).astype(np.int16)


def check(directory):
    directory = Path(directory)
    source = rgb(directory / 'vr-panel-source.png')
    height, width = source.shape[:2]
    scale = width // 320
    if scale < 1 or width != 320 * scale or height != 200 * scale:
        raise RuntimeError('Unexpected instrument source dimensions')
    layout = json.loads((directory / 'vr-cockpit-layout.json').read_text())
    covered = np.zeros((height, width), dtype=bool)
    for instrument in layout['instruments']:
        x0, y0, x1, y1 = instrument['source']
        if not (0 <= x0 < x1 <= 320 and 91 <= y0 < y1 <= 200):
            raise RuntimeError(f'Invalid instrument crop: {instrument["name"]}')
        covered[y0*scale:y1*scale, x0*scale:x1*scale] = True
    # Retain every coloured control/readout, including inactive coloured lamps.
    # Neutral painted trim is replaced by the physical panel/coaming geometry.
    ink = (source.max(2) - source.min(2) > 35) & (source.max(2) > 45)
    ink[:91*scale] = False
    missing = int(np.count_nonzero(ink & ~covered))
    print(f'Instrument texture coverage: {np.count_nonzero(ink)} coloured pixels; {missing} missing')
    if missing or np.count_nonzero(ink) < 1000:
        raise RuntimeError('Instrument mapping discarded live coloured pixels')

    center = rgb(directory / 'vr-hud-optics-center.png')
    bright = (center[:, :, 1] > 200)
    bright &= center[:, :, 1] - np.maximum(center[:, :, 0], center[:, :, 2]) > 90
    if np.count_nonzero(bright) < 1000:
        raise RuntimeError('HUD optics image lacks the original bright symbology')
    for pose in ('left', 'right', 'offset'):
        actual = rgb(directory / f'vr-hud-optics-{pose}.png')
        difference = np.max(np.abs(center - actual), axis=2)
        maximum = int(difference.max())
        print(f'Collimated HUD {pose}: maximum image difference {maximum}/255')
        if maximum > 3:
            raise RuntimeError(f'HUD moves or clips with {pose} eye/head translation')

    # An isolated HUD can pass while its mounting geometry blocks the final view.
    # Check the actual pilot-view render at the same pose and field of view.
    cockpit = rgb(directory / 'vr-cockpit-front.png')
    visible = (cockpit[:, :, 1] >= center[:, :, 1] - 3)
    visible &= cockpit[:, :, 1] - np.maximum(cockpit[:, :, 0], cockpit[:, :, 2]) > 80
    survived = int(np.count_nonzero(bright & visible))
    total = int(np.count_nonzero(bright))
    print(f'HUD ink visible in final cockpit: {survived}/{total}')
    if survived / total < .995:
        raise RuntimeError('Cockpit geometry obscures the rendered HUD')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    check(parser.parse_args().directory)
