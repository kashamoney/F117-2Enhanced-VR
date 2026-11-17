"""Build annotated, equal-FOV contact sheets from native D3D11 seat experiments."""
import argparse
import json
from pathlib import Path
from PIL import Image,ImageDraw
from render_canopy_comparison import BG,INK,font,pair
from check_canopy_seating import transformed,probe


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--capture',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--previous',type=Path,help='optional previous production capture for a coloured comparison')
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    raw=json.loads((a.capture/'vr-canopy-geometry.json').read_text())
    placements=json.loads((a.capture/'vr-canopy-seat-placements.json').read_text())
    by_name={v['name']:v for v in placements};metrics={}
    captions={'baseline':'Original eye / canopy relationship',
              'lower-eye':'Eye 10 cm lower relative to canopy',
              'forward-eye':'Eye 60 cm forward relative to canopy',
              'forward-lower':'Eye 60 cm forward and 10 cm lower',
              'far-forward-lower':'Eye 80 cm forward and 10 cm lower',
              'steeper-rake':'Front depth halved; eye unchanged',
              'steeper-mounted':'Front depth halved; shell +8 cm up, +15 cm aft'}
    for name,v in by_name.items():
        metrics[name]=probe(transformed(raw,v['offset'],v['nose_scale']),[0,0,0],True)
    groups={'viewpoint-only':['baseline','lower-eye','forward-eye','forward-lower'],
            'viewpoint-versus-shape':['baseline','far-forward-lower','steeper-rake','steeper-mounted']}
    for name,keys in groups.items():
        board=Image.new('RGB',(2104,2290),BG);d=ImageDraw.Draw(board)
        d.text((16,12),'Canopy seating study: '+name,font=font(28),fill=INK)
        d.text((16,51),'Same frozen world, 90 degree FOV; HUD / dashboard absent. No simulation-camera changes.',font=font(22),fill=INK)
        for i,key in enumerate(keys):
            x=16+(i%2)*1044;y=100+(i//2)*1090
            r=metrics[key]
            d.text((x,y),captions[key],font=font(23),fill=INK)
            label=f"Glazed view {r['glass_coverage_percent']:.1f}% of central region; {sum(r['targets'].values())}/4 sightlines clear"
            d.text((x,y+31),label,font=font(21),fill=INK)
            im=Image.open(a.capture/f'vr-canopy-seat-{key}.png').convert('RGB')
            board.paste(im,(x,y+61))
            im.save(a.out/f'{key}.png')
        board.save(a.out/f'{name}.png')
    pilot=json.loads((a.capture/'vr-canopy-pilot-geometry.json').read_text())
    metrics['seated']=probe(transformed(pilot),[0,0,0],True)
    pair(Image.open(a.capture/'vr-canopy-pilot-hud-hidden.png'),Image.open(a.capture/'vr-canopy-fitted-hud-hidden.png'),
         'Canopy before / after: same seated eye, world and 90 degree FOV',
         'HUD assembly and instrument textures hidden. The camera and game-world height are unchanged.',
         'Left: rejected photo-fitted model. Right: seated dimensions used by the normal VR build.',
         left_title='PREVIOUS PHOTO-FITTED INTERIOR',right_title='SEATED CANOPY AND MATCHING DASH JOINS').save(a.out/'geometry-before-after.png')
    pair(Image.open(a.capture/'vr-cockpit-pilot-hud-hidden.png'),Image.open(a.capture/'vr-cockpit-pilot-hud-visible.png'),
         'Actual game cockpit: HUD hidden / visible',
         'Live original instruments; new five-pane canopy. Same frozen scene, eye and field of view.',
         'Normal Carrier-VR.cmd now uses this cockpit.',left_title='HUD ASSEMBLY HIDDEN',right_title='HUD ASSEMBLY AND SYMBOLS VISIBLE').save(a.out/'pilot-hud-toggle.png')
    for source,target in [('vr-cockpit-front.png','pilot-front.png'),('vr-cockpit-left.png','pilot-left.png'),
                          ('vr-cockpit-rear.png','pilot-rear.png'),('vr-cockpit-exterior-front-left.png','exterior-front-quarter.png')]:
        Image.open(a.capture/source).save(a.out/target)
    if a.previous:
        pair(Image.open(a.previous/'vr-cockpit-front.png'),Image.open(a.capture/'vr-cockpit-front.png'),
             'Normal flight cockpit before / after',
             'Same seated eye, frozen carrier scene and 90 degree field of view.',
             'Previous flight cockpit at left; updated flight cockpit at right.',
             left_title='PREVIOUS FLIGHT COCKPIT',right_title='UPDATED FLIGHT COCKPIT').save(a.out/'production-before-after.png')
    (a.out/'metrics.json').write_text(json.dumps(metrics,indent=2)+'\n')
    print(json.dumps({n:{'glass':round(r['glass_coverage_percent'],2),'clear':sum(r['targets'].values()),'helmet_inside':r['inside_helmet']} for n,r in metrics.items()},indent=2))


if __name__=='__main__':main()
