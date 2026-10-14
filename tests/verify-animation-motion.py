"""Checks actual decoded HKX poses/motion, not merely file presence. Not game QA."""
import argparse, json, sys
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser();p.add_argument('--codec',required=True);args=p.parse_args()
sys.path.insert(0,args.codec)
from anim_skyrim import load_skyrim_animation,load_skyrim_skeleton
r=Path(__file__).resolve().parents[1];w=r/'build/animations'
report=json.loads((r/'reports/animations.json').read_text())
s=load_skyrim_skeleton(str(w/'source/meshes/actors/character/character assets/skeleton.hkx'))
def rot(q):
 x,y,z,v=np.array(q)/np.linalg.norm(q)
 return np.array([[1-2*(y*y+z*z),2*(x*y-z*v),2*(x*z+y*v)],[2*(x*y+z*v),1-2*(x*x+z*z),2*(y*z-x*v)],[2*(x*z-y*v),2*(y*z+x*v),1-2*(x*x+y*y)]])
def positions(a,frame):
 ps=[];rs=[]
 for i,parent in enumerate(s.parents):
  tr=a.tracks[i];t=np.array(tr.translations[min(frame,len(tr.translations)-1)]);q=rot(tr.rotations[min(frame,len(tr.rotations)-1)])
  ps.append(t if parent<0 else ps[parent]+rs[parent]@t)
  rs.append(q if parent<0 else rs[parent]@q)
 return np.array(ps)
metrics={}
for name in ['idle','walk','run','sprint','back','left','right','takeoff','air','land']:
 a=load_skyrim_animation(str(w/(name+'.hkx')))
 maximum=0.
 for tr in a.tracks:
  q=np.array(tr.rotations);delta=2*np.arccos(np.clip(np.abs(q@q[0]),0,1))
  maximum=max(maximum,float(delta.max()))
 assert maximum>1e-4,(name,'no animation motion')
 assert all(np.isfinite(x) for tr in a.tracks for sample in tr.translations+tr.rotations+tr.scales for x in sample)
 metrics[name]={'maxRotationRadians':maximum,'frames':a.num_frames}
idle=load_skyrim_animation(str(w/'idle.hkx'))
pelvis=s.bones.index('NPC Pelvis [Pelv]');head=s.bones.index('NPC Head [Head]')
hands=[s.bones.index(n) for n in ['NPC L Hand [LHnd]','NPC R Hand [RHnd]']]
for frame in range(idle.num_frames):
 ps=positions(idle,frame)
 assert max(ps[h,2] for h in hands)<ps[pelvis,2],('idle hands not below pelvis',frame)
 assert ps[head,2]<90,('idle returned to standing head height',frame)
rider=load_skyrim_animation(str(w/'rider.hkx'))
assert np.allclose(positions(rider,0)[pelvis],report['bakedRiderSeat'],atol=1e-3)
camera=s.bones.index('Camera3rd [Cam3]')
for name in ['rider','rider-jumpstart','rider-jumpfall','rider-jumpland','rider-jumpstart_sprint','rider-jumpfall_sprint','rider-jumpland_sprint']:
 a=load_skyrim_animation(str(w/(name+'.hkx')))
 ps=positions(a,0)
 assert abs(ps[camera,2]-121)<5,('camera below normal height',name,ps[camera])
 assert np.allclose(ps[pelvis],report['bakedRiderSeat'],atol=1e-3)
metrics['bakedRiderSeat']=report['bakedRiderSeat'];metrics['gameTested']=False
(r/'reports/animation-motion-0.1.2.json').write_text(json.dumps(metrics,indent=2))
print('PASS: 10 moving NPC clips; idle remains quadruped across all frames; 7 rider poses share baked seat and normal camera height. Not game tested.')
