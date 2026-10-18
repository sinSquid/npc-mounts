"""Encode the locally installed Phantom Horse rider clip for native playback."""
import argparse,hashlib,json,struct,sys
from pathlib import Path
import numpy as np

p=argparse.ArgumentParser()
p.add_argument('--codec',required=True)
p.add_argument('--source',required=True)
p.add_argument('--output',required=True)
args=p.parse_args()
sys.path.insert(0,args.codec)
from anim_skyrim import load_skyrim_animation,load_skyrim_skeleton

repo=Path(__file__).resolve().parents[1]
skeleton=load_skyrim_skeleton(str(repo/'build/animations/source/meshes/actors/character/character assets/skeleton.hkx'))
source=Path(args.source)
clip=load_skyrim_animation(str(source))
assert len(skeleton.bones)==99 and clip.num_tracks==99 and clip.num_frames==13
assert (not clip.track_to_bone_indices or clip.track_to_bone_indices==list(range(99))) and clip.duration>0 and clip.blend_hint==0

frames=[]
for frame_index in range(clip.num_frames):
    frame=[]
    for track in clip.tracks:
        t=track.translations[min(frame_index,len(track.translations)-1)]
        q=np.array(track.rotations[min(frame_index,len(track.rotations)-1)],dtype=np.float64)
        q/=np.linalg.norm(q)
        scale=track.scales[min(frame_index,len(track.scales)-1)]
        frame.append([*t,0,*q,*scale,0])
    frames.append(frame)
frames=np.array(frames,dtype='<f4')
assert np.isfinite(frames).all() and (frames[:,:,8:11]>0).all()
assert np.allclose(np.linalg.norm(frames[:,:,4:8],axis=2),1,atol=1e-5)

data=struct.pack('<4If',0x524d504e,1,99,13,clip.duration)
for name in skeleton.bones:
    encoded=name.encode('ascii');assert len(encoded)<64
    data+=encoded+b'\0'*(64-len(encoded))
data+=struct.pack('<99h',*skeleton.parents)+frames.tobytes()
dest=Path(args.output)/'SKSE/Plugins/NPCMounts/Rider.nmr'
dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
report={'source':str(source),'sourceSha256':hashlib.sha256(source.read_bytes()).hexdigest().upper(),'duration':clip.duration,'frames':13,'bones':99,'bytes':len(data),'gameTested':False}
(repo/'reports/rider-motion.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print('Built exact Phantom Horse rider motion:',dest)
