"""Encode a live stereo burst using its original submission timestamps."""
import argparse
import csv
import os
from pathlib import Path
import shutil
import statistics
import subprocess


def encode(directory, slow=1):
    directory = Path(directory).resolve()
    with (directory/'vr-timing.csv').open(encoding='utf-8') as f:
        frames = [r for r in csv.DictReader(f) if r['eye']=='0' and int(r['capture'])>=0]
    if len(frames)<2:
        raise ValueError('No complete live burst in this directory')
    clock = 'display_time' if int(frames[0]['display_time']) else 'clock_ns'
    times = [int(r[clock]) for r in frames]
    intervals = [b-a for a,b in zip(times,times[1:])]
    if min(intervals)<=0:
        raise ValueError('Capture timestamps must advance')
    lines = ['ffconcat version 1.0']
    for i, r in enumerate(frames):
        path = directory/f"vr-live-{int(r['capture']):03}.png"
        if not path.is_file():
            raise FileNotFoundError(path)
        duration = intervals[i] if i<len(intervals) else statistics.median(intervals)
        lines += [f"file '{path.name}'", 'option framerate 1000000',
                  f'duration {duration*slow/1e9:.9f}']
    # Repeat the last PNG so the concat demuxer respects its final duration.
    lines += [f"file 'vr-live-{int(frames[-1]['capture']):03}.png'", 'option framerate 1000000']
    concat = directory/'vr-live.ffconcat'
    concat.write_text('\n'.join(lines)+'\n',encoding='utf-8')
    ffmpeg = shutil.which('ffmpeg')
    if not ffmpeg and os.name=='nt':
        candidate = Path(os.environ['LOCALAPPDATA'])/'Microsoft/WinGet/Links/ffmpeg.exe'
        if candidate.is_file():
            ffmpeg = str(candidate)
    if not ffmpeg:
        raise RuntimeError('Install FFmpeg and put ffmpeg on PATH')
    output = directory/('vr-live.mp4' if slow==1 else 'vr-live-slow.mp4')
    subprocess.run([ffmpeg,'-hide_banner','-loglevel','error','-y','-safe','0','-f','concat',
                    '-i',str(concat),'-fps_mode','vfr','-enc_time_base','1/1000000',
                    '-c:v','libx264','-crf','12','-pix_fmt','yuv420p',str(output)],check=True)
    print(f'{len(frames)} live stereo frames, original duration '
          f'{(times[-1]-times[0])/1e9:.3f} s, slowdown {slow:g}: {output}')


if __name__=='__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory',type=Path)
    p.add_argument('--slow',type=float,default=1)
    a = p.parse_args()
    if not 1<=a.slow<=20:
        p.error('Slowdown must be 1..20')
    encode(a.directory,a.slow)
