"""Encode verified local human crawl poses for native animation-output playback."""
import argparse,struct,sys
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser();p.add_argument('--codec',required=True);p.add_argument('--output',required=True);p.add_argument('--crawl-dir',help='Original human clips, before back-anchor COM modification');p.add_argument('--bound-clip',required=True,help='Verified 99-track human bound idle clip');p.add_argument('--oar-sneak',action='store_true');args=p.parse_args()
sys.path.insert(0,args.codec)
from anim_skyrim import load_skyrim_animation,load_skyrim_skeleton
w=Path(__file__).resolve().parents[1]/'build/animations'
s=load_skyrim_skeleton(str(w/'source/meshes/actors/character/character assets/skeleton.hkx'))
assert len(s.bones)==99
data=struct.pack('<4I',0x434d504e,1,99,8)
for name in s.bones:
    b=name.encode('ascii');assert len(b)<64;data+=b+b'\0'*(64-len(b))
data+=struct.pack('<99h',*s.parents)
sources={'idle':'female/mt_idle.hkx','walk':'female/mt_walkforward.hkx','run':'female/mt_runforward.hkx','sprint':'female/mt_sprintforward.hkx','back':'female/mt_walkbackward.hkx','left':'mt_turnleft60.hkx','right':'mt_turnright60.hkx','bound':args.bound_clip}
if args.oar_sneak:
    assert args.crawl_dir
    # Use the three selected shared crawl clips; backward reverses forward,
    # and turning keeps the crawl idle while actor heading changes.
    sources={'idle':'sneakmtidle.hkx','walk':'sneakwalk_forward.hkx','run':'sneakrun_forward.hkx','sprint':'sneakrun_forward.hkx','back':'sneakwalk_forward.hkx','left':'sneakmtidle.hkx','right':'sneakmtidle.hkx','bound':args.bound_clip}
def rotation(q):
    x,y,z,w=q
    return np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])
def world(frame):
    positions=[];basis=[]
    for i,parent in enumerate(s.parents):
        t=frame[i,:3];r=rotation(frame[i,4:8])@np.diag(frame[i,8:11])
        positions.append(t if parent<0 else positions[parent]+basis[parent]@t)
        basis.append(r if parent<0 else basis[parent]@r)
    return np.array(positions),basis
contacts=[s.bones.index(n) for n in ['NPC L Hand [LHnd]','NPC R Hand [RHnd]','NPC L Foot [Lft ]','NPC R Foot [Rft ]']]
com=s.bones.index('NPC COM [COM ]')
for name in sources:
    source=Path(sources[name]) if name=='bound' else Path(args.crawl_dir)/sources[name] if args.crawl_dir else w/(name+'.hkx')
    a=load_skyrim_animation(str(source))
    assert a.num_tracks in ([99,156] if args.oar_sneak and name!='bound' else [99])
    assert (not a.track_to_bone_indices or a.track_to_bone_indices[:99]==list(range(99))) and a.num_frames>=2 and a.duration>0 and a.blend_hint==0
    frames=[]
    for f in range(a.num_frames):
        frame=[]
        for tr in a.tracks[:99]:
            t=tr.translations[min(f,len(tr.translations)-1)];q=np.array(tr.rotations[min(f,len(tr.rotations)-1)]);q/=np.linalg.norm(q)
            scale=tr.scales[min(f,len(tr.scales)-1)]
            frame.append([*t,0,*q,*scale,0])
        frames.append(frame)
    frames=np.array(frames,dtype='<f4');assert np.isfinite(frames).all()
    assert (frames[:,:,8:11]>0).all(), 'Invalid nonpositive decoded scale: '+name
    if args.oar_sneak and name=='back':frames=frames[::-1].copy()
    if args.crawl_dir and name!='bound':
        # One constant grounding correction per clip; never chase lifted limbs per frame.
        heights=np.array([world(frame)[0][contacts,2].min() for frame in frames])
        correction=3.0-float(heights.min())
        assert abs(correction)<20 and heights.max()-heights.min()<30, 'Invalid contact excursion: '+name
        for frame in frames:
            _,basis=world(frame)
            frame[com,:3]+=np.linalg.solve(basis[s.parents[com]],np.array([0.,0.,correction]))
        grounded=np.array([world(frame)[0][contacts,2].min() for frame in frames])
        assert abs(grounded.min()-3.0)<.002
        print(name,'contact range before',heights.min(),heights.max(),'after',grounded.min(),grounded.max(),'constant correction',correction)
    assert np.allclose(np.linalg.norm(frames[:,:,4:8],axis=2),1,atol=1e-5)
    data+=struct.pack('<fI',a.duration,a.num_frames)+frames.tobytes()
dest=Path(args.output)/'SKSE/Plugins/NPCMounts/Crawl.nmc';dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
print('Verified 8 native clips (7 crawl + 1 bound overlay):',len(data),'bytes')
