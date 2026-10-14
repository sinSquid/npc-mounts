"""Build native 99-bone dismount animation samples; source assets remain read-only."""
import argparse,json,math,struct,sys
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser();p.add_argument('--codec',required=True);p.add_argument('--output',required=True);args=p.parse_args()
sys.path.insert(0,args.codec)
from anim_skyrim import load_skyrim_animation,load_skyrim_skeleton
r=Path(__file__).resolve().parents[1];w=r/'build/animations'
s=load_skyrim_skeleton(str(w/'source/meshes/actors/character/character assets/skeleton.hkx'))
ride=load_skyrim_animation(str(w/'rider.hkx'))
stand=load_skyrim_animation(str(w/'source/meshes/actors/character/animations/female/mt_idle.hkx'))
assert len(s.bones)==99 and ride.num_tracks==99
def pose(a):
 out=[]
 for b in s.reference_pose:out.append([*b.translation,0,*b.rotation,*b.scale,0])
 for n,tr in enumerate(a.tracks):
  i=a.track_to_bone_indices[n] if a.track_to_bone_indices else n
  if i>=0:out[i]=[*tr.translations[0],0,*tr.rotations[0],*tr.scales[0],0]
 out=np.array(out,dtype=np.float64);out[:,4:8]/=np.linalg.norm(out[:,4:8],axis=1)[:,None];return out
start=pose(ride);end=pose(stand);middle=start.copy()
def multiply(a,b):
 av,bv=a[:3],b[:3];return np.r_[a[3]*bv+b[3]*av+np.cross(av,bv),a[3]*b[3]-np.dot(av,bv)]
# Lift/open one leg before unfolding to stand; translation is supplied by the
# runtime back-to-ground pelvis path, scaled to both actors.
for name,angle in [('NPC R Thigh [RThg]',-55),('NPC R Calf [RClf]',30),('NPC Spine [Spn0]',-12)]:
 i=s.bones.index(name);a=math.radians(angle)/2;delta=np.array([math.sin(a),0,0,math.cos(a)])
 middle[i,4:8]=multiply(start[i,4:8],delta)
def interpolate(a,b,t):
 out=a*(1-t)+b*t
 for i in range(99):
  q=b[i,4:8] if np.dot(a[i,4:8],b[i,4:8])>=0 else -b[i,4:8]
  out[i,4:8]=a[i,4:8]*(1-t)+q*t;out[i,4:8]/=np.linalg.norm(out[i,4:8])
 return out
samples=[]
for f in range(31):
 t=f/30
 if t<.3:a,b,u=start,middle,t/.3
 else:a,b,u=middle,end,(t-.3)/.7
 samples.append(interpolate(a,b,u*u*(3-2*u)))
samples=np.array(samples,dtype='<f4');assert np.isfinite(samples).all()
for actual,expected in [(samples[0],start),(samples[-1],end)]:
 assert np.allclose(actual[:,:4],expected[:,:4],atol=1e-5) and np.allclose(actual[:,8:],expected[:,8:],atol=1e-5)
 assert np.allclose(np.abs(np.sum(actual[:,4:8]*expected[:,4:8],axis=1)),1,atol=1e-5)
assert np.allclose(np.linalg.norm(samples[:,:,4:8],axis=2),1,atol=1e-5)
assert np.max(np.ptp(samples[:,:,4:8],axis=0))>.2
data=struct.pack('<4I',0x504d504e,1,99,31)
for name in s.bones:
 b=name.encode('ascii');assert len(b)<64;data+=b+b'\0'*(64-len(b))
data+=struct.pack('<99h',*s.parents)+samples.tobytes()
dest=Path(args.output)/'SKSE/Plugins/NPCMounts/Dismount.nmp';dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
(r/'reports/dismount-motion.json').write_text(json.dumps({'frames':31,'bones':99,'bytes':len(data),'startMatchesRider':True,'endMatchesStanding':True,'normalizedRotations':True,'gameTested':False},indent=2))
print('Built and verified 31-frame human dismount motion:',dest)
