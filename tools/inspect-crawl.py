import sys,json
from pathlib import Path
import numpy as np
sys.path.insert(0,sys.argv[1])
from anim_skyrim import load_skyrim_animation
root=Path(sys.argv[2])
for name in ['female/mt_idle.hkx','female/mt_walkforward.hkx','female/mt_runforward.hkx','female/mt_sprintforward.hkx','female/mt_walkbackward.hkx','mt_turnleft60.hkx','mt_jump.hkx','jumpStart.hkx']:
 a=load_skyrim_animation(str(root/name))
 rows=[]
 for bone in [0,4,5,26]:
  ix=a.track_to_bone_indices.index(bone) if a.track_to_bone_indices and bone in a.track_to_bone_indices else bone
  tr=a.tracks[ix];t=np.array(tr.translations)
  rows.append({'bone':bone,'first':t[0].tolist(),'range':np.ptp(t,axis=0).tolist()})
 print(json.dumps({'file':name,'tracks':a.num_tracks,'frames':a.num_frames,'duration':a.duration,'blend':a.blend_hint,'binding':a.track_to_bone_indices[:8] if a.track_to_bone_indices else None,'translation':rows}))
