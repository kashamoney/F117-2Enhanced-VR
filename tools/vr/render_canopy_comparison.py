"""Render the exported C canopy at photo cameras and report landmark residuals.

This is a geometry inspection tool, not an image generator. Photographs are
displayed unwarped beside the actual compiled mesh. Requires NumPy and Pillow.
"""
import argparse
import csv
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
INK = '#dce5ee'
BG = '#18232e'
WINDOWS = [
    ['FG_T_L','FG_T_R','FG_B_R','FG_B_L'],
    ['QG_L_T','QG_L_F','QG_L_B'], ['QG_R_T','QG_R_F','QG_R_B'],
    ['SG_L_FT','SG_L_FB','SG_L_AB','SG_L_AT'],
    ['SG_R_FT','SG_R_FB','SG_R_AB','SG_R_AT'],
]


def landmark_name(name):
    if name=='CROWN':return 'Crown'
    for side,word in [('L','Port'),('R','Starboard')]:
        names={f'SILL_F_{side}':f'{word} fore sill corner',f'SILL_S_{side}':f'{word} sill shoulder',
               f'SILL_R_{side}':f'{word} rear sill corner',f'SILL_KNEE_{side}':f'{word} fore sill edge sample',
               f'FG_T_{side}':f'Windshield {word.lower()} upper corner',f'FG_B_{side}':f'Windshield {word.lower()} lower corner',
               f'QG_{side}_T':f'{word} quarter window apex',f'QG_{side}_F':f'{word} quarter fore lower corner',
               f'QG_{side}_B':f'{word} quarter aft lower corner'}
        for tag,location in [('FT','fore upper'),('FB','fore lower'),('AT','aft upper'),('AB','aft lower')]:
            names[f'SG_{side}_{tag}']=f'{word} side window {location} corner'
        if name in names:return names[name]
    return name


def font(size):
    return ImageFont.truetype('C:/Windows/Fonts/arial.ttf', size)


def project(points, view):
    camera = np.asarray(points) @ np.array(view['rotation']).T + view['translation']
    return camera[:,:2] / camera[:,2:3] * view['focal'] + view['principal'], camera[:,2]


def raster(mesh, view, size):
    width,height = size
    canvas = np.full((height,width,3), [211,216,220], dtype=np.uint8)
    depth = np.full((height,width), np.inf)
    light = np.array([-.35,.82,-.45]);light /= np.linalg.norm(light)
    for layer in ('opaque','glass','base'):
        for tri in np.array(mesh[layer]):
            xy,z = project(tri,view)
            if z.min() <= 0:
                raise ValueError('Camera intersects canopy; near clipping required')
            lo=np.maximum(np.floor(xy.min(0)).astype(int),0)
            hi=np.minimum(np.ceil(xy.max(0)).astype(int),[width-1,height-1])
            if np.any(hi<lo):continue
            x,y=np.meshgrid(np.arange(lo[0],hi[0]+1)+.5,np.arange(lo[1],hi[1]+1)+.5)
            a,b,c=xy
            denominator=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
            if abs(denominator)<1e-10:continue
            u=((b[1]-c[1])*(x-c[0])+(c[0]-b[0])*(y-c[1]))/denominator
            v=((c[1]-a[1])*(x-c[0])+(a[0]-c[0])*(y-c[1]))/denominator
            w=1-u-v
            inside=(u>=-1e-7)&(v>=-1e-7)&(w>=-1e-7)
            dz=1/(u/z[0]+v/z[1]+w/z[2])
            region=depth[lo[1]:hi[1]+1,lo[0]:hi[0]+1]
            visible=inside&(dz<region)
            normal=np.cross(tri[1]-tri[0],tri[2]-tri[0]);normal/=np.linalg.norm(normal)
            shade=.65+.35*abs(normal@light)
            # Exterior openings are contrasting solid fills, so the interior
            # cannot confuse the aperture outline. No dashboard or HUD is drawn.
            colour=(np.array([211,216,220]) if layer=='base' else
                    np.array([29,76,100]) if layer=='glass' else np.array([137,143,149])*shade)
            canvas[lo[1]:hi[1]+1,lo[0]:hi[0]+1][visible]=colour.astype(np.uint8)
            region[visible]=dz[visible]
    return Image.fromarray(canvas)


def pair(left,right,title,subtitle,credit,left_title='ACTUAL REFERENCE',right_title='COMPILED GEOMETRY / MATCHED PERSPECTIVE'):
    if left.width<600:
        left=left.resize((left.width*3,left.height*3))
        right=right.resize((right.width*3,right.height*3))
    width=max(left.width,right.width);height=max(left.height,right.height)
    out=Image.new('RGB',(width*2+48,height+166),BG);d=ImageDraw.Draw(out)
    d.text((16,12),title,font=font(26),fill=INK)
    d.text((16,48),subtitle,font=font(19),fill=INK)
    d.text((16,79),left_title,font=font(19),fill='#9fd6ed')
    d.text((width+32,79),right_title,font=font(19),fill='#9fd6ed')
    out.paste(left,(16,111));out.paste(right,(width+32,111))
    d.text((16,height+124),credit,font=font(16),fill=INK)
    return out


def annotated(image, points, labels, projected=False):
    out=image.copy();draw=ImageDraw.Draw(out)
    lookup=dict(zip(labels,points));colour='#d62f37' if projected else '#19a163'
    for pane in WINDOWS:
        for first,second in zip(pane,pane[1:]+pane[:1]):
            if first in lookup and second in lookup:draw.line([tuple(lookup[first]),tuple(lookup[second])],fill=colour,width=2)
    for i,(name,point) in enumerate(zip(labels,points)):
        x,y=point
        draw.ellipse((x-5,y-5,x+5,y+5),fill=colour,outline='white',width=1)
        # Numbers preserve the photograph; the full named legend is beside the
        # report table. Closely spaced apex corners use alternating label sides.
        label=str(i+1);tx=x+7;ty=y-23 if i%2 else y+7
        width=draw.textlength(label,font=font(18))
        tx=max(3,min(tx,image.width-width-3));ty=max(1,min(ty,image.height-26))
        box=draw.textbbox((tx,ty),label,font=font(18))
        draw.rectangle((box[0]-2,box[1]-1,box[2]+2,box[3]+1),fill=BG)
        draw.text((tx,ty),label,font=font(18),fill='white')
    return out


def legend(sheet,names,errors):
    rows=(len(names)+1)//2;out=Image.new('RGB',(sheet.width,sheet.height+rows*29+28),BG)
    out.paste(sheet,(0,0));draw=ImageDraw.Draw(out)
    for i,(name,error) in enumerate(zip(names,errors)):
        x=16+(i//rows)*(sheet.width//2);y=sheet.height+8+(i%rows)*29
        draw.text((x,y),f'{i+1}. {landmark_name(name)} - {error:.1f} px',font=font(20),fill=INK)
    return out


def pilot_evidence(source,out):
    names=['shell-only','hud-hidden','hud-hardware','hud-visible']
    labels=['Canopy only; HUD and dashboard absent',
            'Canopy + plain dashboard; HUD absent',
            'HUD hardware and glass added; symbols hidden',
            'Same pose and world; HUD symbols visible']
    for name in names:
        (out/f'pilot-{name}.png').write_bytes((source/f'vr-canopy-pilot-{name}.png').read_bytes())
    board=Image.new('RGB',(2104,2240),BG);d=ImageDraw.Draw(board)
    d.text((18,12),'PILOT LAYER CHECK - frozen world, centred eye, 90 degree FOV',font=font(28),fill=INK)
    d.text((18,49),'Instrument textures hidden throughout. Photo-fitted inspection canopy; existing dashboard / HUD dimensions.',font=font(21),fill=INK)
    for i,(name,label) in enumerate(zip(names,labels)):
        x=16+(i%2)*1044;y=96+(i//2)*1070
        d.text((x,y),label,font=font(22),fill=INK)
        image=Image.open(source/f'vr-canopy-pilot-{name}.png').convert('RGB')
        board.paste(image,(x,y+32))
    board.save(out/'pilot-layers.png')
    # Direct toggle is the same image size, camera, world, depth and crop.
    pair(Image.open(source/'vr-canopy-pilot-hud-hidden.png'),
         Image.open(source/'vr-canopy-pilot-hud-visible.png'),
         'Pilot view: HUD absent / HUD visible',
         'Same frozen world, eye position and 90 degree FOV; instrument textures hidden.',
         'Left: canopy + plain dashboard, no HUD assembly. Right: HUD assembly + symbols.',
         left_title='HUD HIDDEN (assembly and symbols)',right_title='HUD VISIBLE (assembly and symbols)').save(out/'pilot-hud-toggle.png')
    hidden=np.array(Image.open(source/'vr-canopy-mask-hud-hidden.png').convert('RGB'))
    visible=np.array(Image.open(source/'vr-canopy-mask-hud-visible.png').convert('RGB'))
    pair(Image.fromarray(hidden),Image.fromarray(visible),
         'Opaque obstruction IDs: same pilot pose and world',
         'Red: canopy. Green: existing dashboard / lower interior. Blue: HUD hardware. Black: open view.',
         'Transparent glass and HUD light are excluded from opaque obstruction counts.',
         left_title='HUD ASSEMBLY ABSENT',right_title='HUD ASSEMBLY PRESENT').save(out/'pilot-obstructions.png')
    masks={}
    for title,im in [('without_hud',hidden),('with_hud',visible)]:
        y,x=np.indices(im.shape[:2]);rayx=(x+.5-512)/512;rayy=(512-y-.5)/512
        roi=(abs(rayx)<np.tan(np.deg2rad(35)))&(abs(rayy)<np.tan(np.deg2rad(25)))
        masks[title]={name:float(((im[:,:,channel]>240)&roi).sum()/roi.sum()*100)
                      for name,channel in [('canopy',0),('dashboard_body',1),('hud_hardware',2)]}
        masks[title]['open']=100-sum(masks[title].values())
    (out/'obstruction-masks.json').write_text(json.dumps(masks,indent=2)+'\n')
    return masks


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--fit',type=Path,required=True)
    p.add_argument('--capture',type=Path,required=True)
    p.add_argument('--references',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--skip-pilot',action='store_true',help='iterate exterior geometry before recapturing the game')
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    fit=json.loads(a.fit.read_text());mesh=json.loads((a.capture/'vr-canopy-geometry.json').read_text())
    annotations=json.loads((ROOT/'tools/vr/canopy_landmarks.json').read_text())
    by_name={v['name']:v for v in annotations['views']}
    fit['views']=[dict(by_name[v['name']],**v) for v in fit['views']]
    if set(fit['points'])!=set(mesh['landmarks']):raise ValueError('Fit and exported mesh landmark names differ')
    delta=max(np.max(np.abs(np.array(point)-mesh['landmarks'][name])) for name,point in fit['points'].items())
    if delta>2e-7:raise ValueError(f'Stale compiled geometry: fit/export differ by {delta:.8f}')
    rows=[];report=['# Canopy geometry evidence','',
        'Inspection candidate, separate from the flight canopy. Opaque grey shell; five blue aperture fills. No HUD, instruments or interior in exterior renders.', '',
        'Photographs are cropped, never stretched or warped. Each render uses one pinhole camera (rotation, translation, focal length and principal point). The USAF frontal crop retains the principal point of the complete photograph. The small supplied side image is displayed at 3x uniform magnification; errors remain in native source pixels.', '',
        'The underside at the sill is a depth mask in the background colour: the unmodelled airframe normally hides the far-side windows there. It is exported separately from the canopy and is absent in the pilot captures. Dogtooth joints are represented by mean straight edges in this pass.', '',
        'These are approximate photo proportions, not measured aircraft dimensions. Photographs have unknown lens calibration. Mean dogtooth edges are used. Hidden or cropped corners are unscored. RMS below measures reprojected manually annotated landmarks from the compiled C mesh; it is not an independent certification of shape.', '',
        '| Reference | Scored points | RMS / max error (native px) | Role |',
        '|---|---:|---:|---|']
    sections=[]
    for view in fit['views']:
        reference=Image.open(a.references/view['file']).convert('RGB').crop(view['crop'])
        render=raster(mesh,view,reference.size)
        names=list(view['points']);observed=np.array([view['points'][n][:2] for n in names])
        projected,_=project([mesh['landmarks'][n] for n in names],view)
        errors=np.linalg.norm(projected-observed,axis=1);rms=np.sqrt(np.mean(errors**2))
        name=view['name'];role='camera-only check; geometry frozen' if view.get('validation_only') else 'geometry + camera fit'
        subtitle=f'{reference.width} x {reference.height} native pixels; RMS {rms:.2f} px; max {errors.max():.2f} px. {role}.'
        credit=view['credit']+'; see report for source and crop.'
        pair(reference,render,name.upper()+' - plain geometry',subtitle,credit).save(a.out/f'{name}-comparison.png')
        sheet=pair(annotated(reference,observed,names),annotated(render,projected,names,True),
                   name.upper()+' - named landmark check',subtitle,credit)
        legend(sheet,names,errors).save(a.out/f'{name}-landmarks.png')
        render.save(a.out/f'{name}-render.png');reference.save(a.out/f'{name}-reference.png')
        report.append(f'| {name} | {len(names)} | {rms:.2f} / {errors.max():.2f} | {role} |')
        sections.extend(['',f'## {name}', '',f'[Plain reference/render pair]({name}-comparison.png) · [Numbered landmarks]({name}-landmarks.png)', '',
                         f"Source: {view['source']} — {view['credit']}. Crop in source pixels: {view['crop']}. No nonuniform scaling.",'',
                         '| Marker | Named landmark | Ref x,y | Mesh x,y | Error px | Annotation uncertainty px |',
                         '|---:|---|---|---|---:|---:|'])
        for i,(n,ref,uv,e) in enumerate(zip(names,observed,projected,errors)):
            row={'view':name,'marker':i+1,'landmark':n,'reference_x':ref[0],'reference_y':ref[1],
                 'render_x':uv[0],'render_y':uv[1],'error_px':e,'uncertainty_px':view['points'][n][2]}
            rows.append(row)
            sections.append(f'| {i+1} | {landmark_name(n)} (`{n}`) | {ref[0]:.1f}, {ref[1]:.1f} | {uv[0]:.1f}, {uv[1]:.1f} | {e:.2f} | {row["uncertainty_px"]} |')
        (a.out/f'{name}-camera.json').write_text(json.dumps({k:view[k] for k in ['rotation','translation','focal','principal','crop']},indent=2)+'\n')
        print(f'{name}: {len(names)} points, RMS {rms:.3f}px, max {errors.max():.3f}px')
    with (a.out/'landmarks.csv').open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=rows[0].keys());writer.writeheader();writer.writerows(rows)
    masks={} if a.skip_pilot else pilot_evidence(a.capture,a.out)
    if masks:report.extend(['','## Pilot obstruction check','',
                   '[Four isolated stages](pilot-layers.png) · [HUD hidden / visible](pilot-hud-toggle.png)','',
                   'The canopy is the fitted candidate. Dashboard and HUD dimensions are the existing flight cockpit. Instrument textures are absent. Glass does not count as an opaque obstruction. Pixel ID masks identify the frontmost opaque surface in a +/-35 degree horizontal, +/-25 degree vertical region.', '',
                   '| Layer | Canopy | Dashboard / body | HUD hardware | Open |','|---|---:|---:|---:|---:|'])
    for title,values in masks.items():report.append('| '+title+' | '+' | '.join(f'{v:.2f}%' for v in values.values())+' |')
    report.extend(sections)
    (a.out/'landmark-report.md').write_text('\n'.join(report)+'\n',encoding='utf-8')
    print(json.dumps(masks,indent=2))


if __name__=='__main__':main()
