"""Check frozen real-world renders under known camera rotations, without a headset."""
from pathlib import Path
import json
import numpy as np
from PIL import Image


def read(path):
    return np.asarray(Image.open(path).convert('RGB')).astype(float)


def sample(image, u, v):
    x=np.floor(u).astype(int).clip(0,image.shape[1]-2)
    y=np.floor(v).astype(int).clip(0,image.shape[0]-2)
    a=(u-x)[...,None];b=(v-y)[...,None]
    return image[y,x]*(1-a)*(1-b)+image[y,x+1]*a*(1-b)+image[y+1,x]*(1-a)*b+image[y+1,x+1]*a*b


def edge_quality(directory, steps):
    """Compare moving edge coverage with a supersampled frozen-scene reference.

    Normalize by local contrast so thin dark edges count as well as bright ones.
    This is a regression bound for this deterministic scene, not a universal
    perceptual score. All averaging is in linear light, matching the renderer.
    """
    lut=np.arange(256,dtype=np.float32)/255
    lut=np.where(lut<=.04045,lut/12.92,((lut+.055)/1.055)**2.4)
    worst=0
    frames=sorted(set((1,steps//9,steps//3,steps//2,2*steps//3,steps-1,steps)))
    for k in frames:
        actual=lut[np.asarray(Image.open(directory/f'roll-{k:03}.png').convert('RGB'))]
        reference=lut[np.asarray(Image.open(directory/f'roll-reference-{k:03}.png').convert('RGB'))]
        h,w=actual.shape[:2];rh,rw=reference.shape[:2]
        factor=rh//h
        if rh!=factor*h or rw!=factor*w:raise AssertionError('Reference must be an integer multiple of output size')
        expected=reference.reshape(h,factor,w,factor,3).mean(axis=(1,3))
        low=actual.copy();high=actual.copy()
        for dy in (-1,0,1):
            for dx in (-1,0,1):
                neighbor=np.roll(actual,(dy,dx),axis=(0,1))
                low=np.minimum(low,neighbor);high=np.maximum(high,neighbor)
        contrast=np.max(high-low,axis=2)
        valid=contrast>.05
        valid[:4]=valid[-4:]=False;valid[:,:4]=valid[:,-4:]=False
        y,x=np.where(valid);take=np.arange(0,len(x),max(1,len(x)//20000));x=x[take];y=y[take]
        expected=expected[y,x];scale=contrast[y,x]
        mixed=np.any((expected>low[y,x]+.08*scale[:,None])&(expected<high[y,x]-.08*scale[:,None]),axis=1)
        errors=(np.max(abs(actual[y,x]-expected),axis=1)/scale)[mixed]
        if len(errors)<100:raise AssertionError('Too few real world edges in temporal fixture')
        rms=float(np.sqrt(np.mean(errors**2)));worst=max(worst,rms)
        print(f'Edge coverage error at {k*90/steps:g} degrees: {rms:.3f} ({len(errors)} samples)')
    if worst>.14:raise AssertionError(f'Unstable subpixel edge coverage: {worst:.3f} exceeds 0.140 regression bound')
    return worst


def check(directory):
    directory=Path(directory);reference=read(directory/'sky-000.png')
    metadata=json.loads((directory/'vr-sweep.json').read_text())
    steps=metadata['steps']
    h,w=reference.shape[:2]
    stride=max(1,w//256);yy,xx=np.mgrid[:h:stride,:w:stride]
    rx=2*(xx+.5)/w-1;ry=1-2*(yy+.5)/h
    failures=[];worst=0
    # Build the conservative horizon-edge exclusion once, then sample probes.
    sky=reference[:,:,0]>reference[:,:,2]
    certain=np.ones((h,w),bool)
    for dy in range(-2,3):
        for dx in range(-2,3):certain &= sky==np.roll(sky,(dy,dx),axis=(0,1))
    certainty=np.repeat(certain[:,:,None],3,axis=2).astype(float)
    for k in range(1,steps+1):
        angle=k*np.pi/(2*steps);c=np.cos(angle);s=np.sin(angle)
        u=(c*rx-s*ry+1)*w/2-.5;v=(1-s*rx-c*ry)*h/2-.5
        valid=(u>4)&(u<w-5)&(v>4)&(v<h-5)
        expected=sample(reference,u,v);actual=read(directory/f'sky-{k:03}.png')[::stride,::stride]
        # A two-pixel band allows normal coverage/AA changes at a moving edge.
        interior=sample(certainty,u,v)[:,:,0]>.99
        mismatch=np.max(abs(expected-actual),axis=2)>8
        fraction=float(np.mean(mismatch[valid & interior]))
        if fraction>worst:worst=fraction;worst_frame=k
        if fraction>.001:failures.append((k,fraction))
    repeat=read(directory/'roll-repeat.png');first=read(directory/'roll-000.png')
    if not np.array_equal(repeat,first):raise AssertionError('Identical frozen poses retained another frame')
    print(f'Frozen sky roll: worst unexpected change {worst:.2%} at frame {worst_frame if worst else 0} ({worst_frame*90/steps if worst else 0:g} degrees)')
    if failures:raise AssertionError(f'Horizon changes discontinuously: {failures[:4]}')
    edge_quality(directory,steps)
    print('PASS: continuous horizon through the roll sweep; repeated pose exact')


if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path)
    check(p.parse_args().directory)
