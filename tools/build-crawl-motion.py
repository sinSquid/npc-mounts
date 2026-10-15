"""Encode verified local human crawl poses for native animation-output playback."""
import argparse,struct,sys
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser();p.add_argument('--codec',required=True);p.add_argument('--output',required=True);args=p.parse_args()
sys.path.insert(0,args.codec)
from anim_skyrim import load_skyrim_animation,load_skyrim_skeleton
w=Path(__file__).resolve().parents[1]/'build/animations'
s=load_skyrim_skeleton(str(w/'source/meshes/actors/character/character assets/skeleton.hkx'))
assert len(s.bones)==99
data=struct.pack('<4I',0x434d504e,1,99,7)
for name in s.bones:
    b=name.encode('ascii');assert len(b)<64;data+=b+b'\0'*(64-len(b))
data+=struct.pack('<99h',*s.parents)
for name in ['idle','walk','run','sprint','back','left','right']:
    a=load_skyrim_animation(str(w/(name+'.hkx')))
    assert a.num_tracks==99 and a.track_to_bone_indices==list(range(99)) and a.num_frames>=2 and a.duration>0
    frames=[]
    for f in range(a.num_frames):
        frame=[]
        for tr in a.tracks:
            t=tr.translations[min(f,len(tr.translations)-1)];q=np.array(tr.rotations[min(f,len(tr.rotations)-1)]);q/=np.linalg.norm(q)
            scale=tr.scales[min(f,len(tr.scales)-1)]
            frame.append([*t,0,*q,*scale,0])
        frames.append(frame)
    frames=np.array(frames,dtype='<f4');assert np.isfinite(frames).all()
    assert np.allclose(np.linalg.norm(frames[:,:,4:8],axis=2),1,atol=1e-5)
    data+=struct.pack('<fI',a.duration,a.num_frames)+frames.tobytes()
dest=Path(args.output)/'SKSE/Plugins/NPCMounts/Crawl.nmc';dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
print('Verified 7 native human crawl clips:',len(data),'bytes')
