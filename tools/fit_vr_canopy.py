"""Fit named canopy apertures and perspective cameras to annotated photographs.

The photographs are local evidence files, not game assets. The resulting header
contains only symmetric planar geometry. Absolute aircraft scale is not recovered.
"""
import argparse
import json
from pathlib import Path

import numpy as np
from scipy.optimize import least_squares

ROOT = Path(__file__).resolve().parent.parent
PARAMS = [
    ('facet_apex_y',.88,.60,1.0), ('facet_apex_z',-.15,-.60,.35),
    ('side_header_x',.25,.05,.60), ('side_header_y',.75,.55,.95), ('side_header_z',.25,.05,.65),
    ('front_width',.35,.15,.80), ('front_z',-1.50,-2.6,-.70),
    ('sill_y',-.15,-.80,.10), ('sill_z',-.65,-2.0,-.20),
    ('rear_width',1.05,.70,1.30), ('rear_y',.10,-.50,.40), ('rear_z',2.10,1.40,2.8),
    ('rear_shoulder_y',.55,.20,.75), ('rear_crown_y',.70,.40,.85),
    ('front_top_y',.78,.50,.98), ('front_top_width',.15,.02,.80),
    ('front_bottom_y',.25,.05,.50), ('front_bottom_width',.80,.40,.98),
    ('quarter_top_f',.25,.02,.40), ('quarter_top_s',.02,.005,.20),
    ('quarter_front_c',.18,.04,.45), ('quarter_front_s',.04,.005,.25),
    ('quarter_back_c',.10,.02,.60), ('quarter_back_f',.04,.005,.60),
    ('side_ft_u',.08,.015,.25), ('side_ft_v',.67,.45,.95),
    ('side_fb_u',.08,.015,.25), ('side_fb_v',.12,.025,.35),
    ('side_at_u',.72,.50,.96), ('side_at_v',.72,.50,.96),
    ('side_ab_u',.78,.55,.97), ('side_ab_v',.15,.025,.40),
]


def geometry(values):
    g = dict(zip((p[0] for p in PARAMS), values))
    c = np.array([0.,1.,0.])
    a = np.array([0.,g['facet_apex_y'],g['facet_apex_z']])
    h = np.array([-g['side_header_x'],g['side_header_y'],g['side_header_z']])
    f = np.array([-g['front_width'],0,g['front_z']])
    s = np.array([-1.,g['sill_y'],g['sill_z']])
    r = np.array([-g['rear_width'],g['rear_y'],g['rear_z']])
    normal = np.cross(s-h,r-h)
    q = np.array([0.,g['rear_shoulder_y'],g['rear_z']])
    q[0] = h[0]-(normal[1]*(q[1]-h[1])+normal[2]*(q[2]-h[2]))/normal[0]
    k = np.array([0.,g['rear_crown_y'],g['rear_z']])
    points = {'CROWN':c, 'FACET_APEX':a, 'REAR_CROWN':k}
    for side, reflection in [('L',np.array([1,1,1])),('R',np.array([-1,1,1]))]:
        def put(name, p): points[name.format(side=side) if '{side}' in name else name+'_'+side] = p*reflection
        for name,p in [('SILL_F',f),('SILL_S',s),('SILL_R',r),('REAR_SHOULDER',q)]: put(name,p)
        put('SIDE_HEADER',h)
        put('SILL_KNEE',.25*f+.75*s)
        top = g['front_top_y']; bottom = g['front_bottom_y']
        put('FG_T',np.array([-g['front_width']*(1-top)*g['front_top_width'],top*a[1],g['front_z']*(1-top)+a[2]*top]))
        put('FG_B',np.array([-g['front_width']*(1-bottom)*g['front_bottom_width'],bottom*a[1],g['front_z']*(1-bottom)+a[2]*bottom]))
        put('QG_{side}_T',g['quarter_top_f']*f+g['quarter_top_s']*s+(1-g['quarter_top_f']-g['quarter_top_s'])*a)
        put('QG_{side}_F',g['quarter_front_c']*a+g['quarter_front_s']*s+(1-g['quarter_front_c']-g['quarter_front_s'])*f)
        put('QG_{side}_B',g['quarter_back_c']*a+g['quarter_back_f']*f+(1-g['quarter_back_c']-g['quarter_back_f'])*s)
        for tag in ('FT','FB','AT','AB'):
            u=g['side_'+tag.lower()+'_u'];v=g['side_'+tag.lower()+'_v']
            put('SG_{side}_'+tag, (1-u)*((1-v)*s+v*h)+u*((1-v)*r+v*q))
    return points


def rotation(yaw, elevation, roll):
    eye=np.array([-np.sin(yaw)*np.cos(elevation),np.sin(elevation),-np.cos(yaw)*np.cos(elevation)])
    forward=-eye
    right=np.cross(forward,[0,1,0]);right/=np.linalg.norm(right)
    down=np.cross(forward,right)
    rr=np.array([[np.cos(roll),-np.sin(roll),0],[np.sin(roll),np.cos(roll),0],[0,0,1]])
    return rr@np.stack([right,down,forward])


def project(points, camera, principal):
    rot=rotation(*camera[:3]);t=np.asarray(camera[3:6]);f=np.exp(camera[6])
    p=np.asarray(points)@rot.T+t
    uv=f*p[:,:2]/np.maximum(p[:,2:3],.1)+principal+camera[7:9]
    return uv,p[:,2]


def initial_camera(view, points):
    yaw=view['yaw'];elev=max(view['elevation'],-.24);f=view['focal'];distance=view['distance']
    names=list(view['points']);obs=np.array([view['points'][n][:2] for n in names])
    p=np.array([points[n] for n in names]);mean=(p@rotation(yaw,elev,0).T).mean(0)
    txy=(obs.mean(0)-view['principal'])*distance/f-mean[:2]
    return np.array([yaw,elev,0,*txy,distance-mean[2],np.log(f),0.,0.])


def camera_bounds(view):
    if view['name']=='front': yaw=(-.5,.5)
    elif view['name'] in ('side','user-side'): yaw=(.55,1.55)
    elif view.get('validation_only'): yaw=(-1.2,.15)
    else: yaw=(.10,1.3)
    elevation=(-.50,.60) if view['name']=='front' else (-.9,.60)
    shift=1e-5 if view['name']=='front' else (600 if view['name']=='quarter' else 100)
    return ([yaw[0],elevation[0],-.3,-15,-15,2,np.log(200),-shift,-shift],
            [yaw[1],elevation[1],.3,15,15,100,np.log(50000),shift,shift])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--header',type=Path)
    parser.add_argument('--manifest',type=Path,help='save compact geometry/camera inputs for repeatable rendering')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    data=json.loads((ROOT/'tools/canopy_landmarks.json').read_text())
    views=[v for v in data['views'] if not v.get('validation_only')]
    base=np.array([p[1] for p in PARAMS]);ng=len(base);p0=geometry(base)
    x=np.concatenate([base]+[initial_camera(v,p0) for v in views])
    lower=np.concatenate([[p[2] for p in PARAMS]]+[camera_bounds(v)[0] for v in views])
    upper=np.concatenate([[p[3] for p in PARAMS]]+[camera_bounds(v)[1] for v in views])
    geo_scale=np.array([p[3]-p[2] for p in PARAMS])
    def residual(x):
        points=geometry(x[:ng]);res=[]
        for i,v in enumerate(views):
            names=list(v['points']);obs=np.array([v['points'][n] for n in names])
            cam=x[ng+9*i:ng+9*(i+1)]
            uv,depth=project([points[n] for n in names],cam,v['principal'])
            res.extend(((uv-obs[:,:2])/obs[:,2:3]).ravel())
            res.extend(np.minimum(depth-.3,0)*10)
            res.extend(cam[7:9]/400)
            if 'roof_envelope' in v:
                # The photographed crown is also the silhouette apex. A point
                # fit alone can put an unobserved rear roof above that apex.
                env=v['roof_envelope'];outline,_=project([points[n] for n in env['landmarks']],cam,v['principal'])
                dx=outline[:,0]-env['apex'][0]
                top=env['apex'][1]+np.where(dx<0,env['left_slope']*dx,env['right_slope']*dx)
                res.extend(np.minimum(outline[:,1]-top,0)/env['tolerance_px'])
        res.extend(.25*(x[:ng]-base)/geo_scale)
        g=dict(zip((p[0] for p in PARAMS),x[:ng]))
        for name,mean,sigma in [('front_z',-1.5,.4),('sill_z',-.65,.3),('sill_y',-.15,.2),
                                ('rear_z',2.1,.35),('rear_width',1.05,.2),('rear_y',.1,.2),
                                ('rear_crown_y',.70,.15)]:res.append((g[name]-mean)/sigma)
        res.extend([10*min(g['rear_crown_y']-g['rear_shoulder_y']-.03,0),
                    10*min(g['sill_z']-g['front_z']-.25,0),
                    100*min(.96-g['quarter_back_c']-g['quarter_back_f'],0)])
        return np.asarray(res)
    best=None
    for attempt in range(3):
        trial=x.copy()
        if attempt:
            rng=np.random.default_rng(71+attempt)
            trial[ng:]+=rng.normal(0,.025,len(trial)-ng)
        trial=np.clip(trial,lower+1e-7,upper-1e-7)
        fit=least_squares(residual,trial,bounds=(lower,upper),loss='soft_l1',f_scale=2,
                          x_scale='jac',max_nfev=700,ftol=1e-9,xtol=1e-9,gtol=1e-8)
        print('Fit',attempt,'cost',fit.cost,'evaluations',fit.nfev,flush=True)
        if best is None or fit.cost<best.cost:best=fit
    x=best.x;points=geometry(x[:ng]);scale=.62;offset=np.array([0.,-.29,-.40])
    result={'note':data['annotation_note'],'scale':scale,'offset':offset.tolist(),
            'parameters':dict(zip((p[0] for p in PARAMS),map(float,x[:ng]))),
            'points':{n:(p*scale+offset).tolist() for n,p in points.items()},'views':[]}
    for v in data['views']:
        names=list(v['points']);obs=np.array([v['points'][n] for n in names])
        if v.get('validation_only'):
            start=initial_camera(v,points);lo,hi=camera_bounds(v)
            def error(c):return ((project([points[n] for n in names],c,v['principal'])[0]-obs[:,:2])/obs[:,2:3]).ravel()
            cam=least_squares(error,np.clip(start,np.array(lo)+1e-7,np.array(hi)-1e-7),bounds=(lo,hi),max_nfev=1000).x
        else:cam=x[ng+9*views.index(v):ng+9*(views.index(v)+1)]
        uv,_=project([points[n] for n in names],cam,v['principal']);error_px=np.linalg.norm(uv-obs[:,:2],axis=1)
        R=rotation(*cam[:3]);t=scale*cam[3:6]-R@offset
        entry=dict(v,principal=(np.array(v['principal'])+cam[7:9]).tolist(),rotation=R.tolist(),translation=t.tolist(),focal=float(np.exp(cam[6])),
                   camera_parameters=cam.tolist(),rms_px=float(np.sqrt(np.mean(error_px**2))),
                   landmarks={n:{'reference':obs[i,:2].tolist(),'projected':uv[i].tolist(),'error_px':float(error_px[i]),'sigma_px':float(obs[i,2])} for i,n in enumerate(names)})
        result['views'].append(entry)
        print(v['name'],'RMS',entry['rms_px'],'max',float(error_px.max()),flush=True)
    (args.out/'fit.json').write_text(json.dumps(result,indent=2)+'\n')
    if args.manifest:
        manifest={k:result[k] for k in ('note','scale','offset','parameters','points')}
        manifest['views']=[{k:v[k] for k in ('name','rotation','translation','focal','principal','camera_parameters')} for v in result['views']]
        args.manifest.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    if args.header:
        text='/* Generated by tools/fit_vr_canopy.py; photo-derived proportions, not measured aircraft dimensions. */\n'
        text+='enum {\n'+''.join('    CAN_'+n+',\n' for n in points)+'    CAN_POINT_COUNT\n};\n'
        text+='static const VrCockpitLandmark canopy_landmarks[CAN_POINT_COUNT]={\n'
        for n,p in result['points'].items():text+='    {"'+n+'",{'+','.join(f'{v:.8f}f' for v in p)+'}},\n'
        text+='};\n';args.header.write_text(text,encoding='utf-8')


if __name__=='__main__':main()
