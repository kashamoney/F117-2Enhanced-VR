"""Diagnostic ray probes of the compiled canopy; isolate mount and shape changes.

The sightline targets are gameplay visibility requirements, not aircraft specs.
Use --check to fail on a blocked horizon/upper-forward view or helmet collision.
"""
import argparse
import json
from pathlib import Path
import numpy as np


def hits(triangles, eye, directions):
    nearest=np.full(len(directions),np.inf)
    for a,b,c in triangles:
        e1=b-a;e2=c-a;h=np.cross(directions,e2);det=h@e1
        safe=np.abs(det)>1e-10
        inverse=np.divide(1,det,out=np.zeros_like(det),where=safe)
        s=eye-a;u=inverse*(h@s);q=np.cross(s,e1)
        v=inverse*(directions@q);distance=inverse*(e2@q)
        valid=safe&(u>=-1e-7)&(v>=-1e-7)&(u+v<=1+1e-7)&(distance>1e-5)
        nearest=np.minimum(nearest,np.where(valid,distance,np.inf))
    return nearest


def rays(yaw,pitch):
    yaw,pitch=np.deg2rad(yaw),np.deg2rad(pitch)
    return np.column_stack([np.sin(yaw)*np.cos(pitch),np.sin(pitch),-np.cos(yaw)*np.cos(pitch)])


def transformed(mesh, offset=(0,0,0), nose_scale=1):
    out={}
    for name in ('opaque','glass','base'):
        tri=np.array(mesh[name],dtype=float)
        z=tri[:,:,2];z[:]=np.where(z<-.4,-.4+(z+.4)*nose_scale,z)
        out[name]=tri+np.array(offset)
    return out


def probe(mesh, eye, dense=False):
    eye=np.array(eye,dtype=float)
    targets=[('horizon-left',-5,0),('horizon',0,0),('horizon-right',5,0),('upper-forward',0,20)]
    result={'eye':eye.tolist(),'targets':{},'inside_helmet':True}
    closed=np.concatenate([mesh[k] for k in ('opaque','glass','base')])
    # A small helmet envelope must remain inside the shell. Parity is evaluated
    # with a non-axis direction to avoid counting shared edges twice.
    direction=np.array([.283,.127,.951]);direction/=np.linalg.norm(direction)
    for relative in ([0,0,0],[.11,.04,0],[-.11,.04,0],[0,.16,0],[0,.04,.11],[0,.04,-.11]):
        point=eye+relative;count=0
        for triangle in closed:
            if np.isfinite(hits([triangle],point,direction[None,:])[0]):count+=1
        if count%2==0:result['inside_helmet']=False
    for name,yaw,pitch in targets:
        d=rays([yaw],[pitch]);blocked=False
        for ipd in (-.032,.032):
            e=eye+[ipd,0,0]
            opaque=hits(mesh['opaque'],e,d)[0];glass=hits(mesh['glass'],e,d)[0]
            if np.isfinite(opaque) or not np.isfinite(glass):blocked=True
        result['targets'][name]=not blocked
    if dense:
        yaw,pitch=np.meshgrid(np.linspace(-35,35,141),np.linspace(-25,25,101))
        d=rays(yaw.ravel(),pitch.ravel())
        opaque=hits(mesh['opaque'],eye,d);glass=hits(mesh['glass'],eye,d)
        result['glass_coverage_percent']=float(((~np.isfinite(opaque))&np.isfinite(glass)).mean()*100)
    result['pass']=result['inside_helmet'] and all(result['targets'].values())
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--mesh',type=Path,required=True)
    p.add_argument('--eye',type=float,nargs=3,default=[0,0,0])
    p.add_argument('--offset',type=float,nargs=3,default=[0,0,0])
    p.add_argument('--nose-scale',type=float,default=1)
    p.add_argument('--check',action='store_true')
    p.add_argument('--sweep',action='store_true')
    p.add_argument('--out',type=Path)
    a=p.parse_args();raw=json.loads(a.mesh.read_text())
    if a.sweep and a.check:p.error('Use --check for one pose, or --sweep to compare poses')
    mesh=transformed(raw,a.offset,a.nose_scale)
    if a.sweep:
        result=[probe(mesh,[0,y,-forward]) for forward in (0,.2,.4,.6,.8) for y in (.05,0,-.05,-.1,-.15)]
    else:result=probe(mesh,a.eye,True)
    print(json.dumps(result,indent=2))
    if a.out:
        a.out.parent.mkdir(parents=True,exist_ok=True)
        a.out.write_text(json.dumps(result,indent=2)+'\n')
    if a.check and (isinstance(result,list) or not result['pass']):raise SystemExit(1)


if __name__=='__main__':main()
