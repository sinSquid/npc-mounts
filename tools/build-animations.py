"""Offline conversion only. Does not touch the game or installed mods.
Dependencies: numpy, lz4, bethesda-structs, PyNifly's hkx codec, HKX2 converter, hkxcmd.
"""
import argparse,copy,hashlib,json,math,struct,subprocess,sys
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser()
p.add_argument('--codec',required=True);p.add_argument('--bsa-lib',required=True)
p.add_argument('--data',required=True);p.add_argument('--calibration',required=True)
p.add_argument('--hkxconv',required=True);p.add_argument('--hkxcmd',required=True)
p.add_argument('--output',help='Candidate package directory; defaults to repo/dist')
p.add_argument('--crawl-dir',help='Read-only human crawl source; replaces horse retargeting')
args=p.parse_args();sys.path.insert(0,args.codec);sys.path.insert(0,args.bsa_lib)
from anim_skyrim import load_skyrim_animation,load_skyrim_skeleton,write_skyrim_animation,_parse_hkx_sections,_parse_virtual_fixups
from anim_fo4 import AnimationData,TrackData
import lz4.frame
from bethesda_structs.archive.bsa import BSAArchive
R=Path(__file__).resolve().parents[1];W=R/'build/animations';W.mkdir(parents=True,exist_ok=True)
archive=Path(args.data)/'Skyrim - Animations.bsa'
with archive.open('rb') as f:metadata=BSAArchive.archive_struct.parse(f.read(1024*1024))
entries={};index=0
for folder in metadata.directory_blocks:
 for entry in folder.file_records:
  entries[(folder.name.rstrip('\0')+'\\'+metadata.file_names[index]).lower()]=entry;index+=1
def extract(relative):
 entry=entries[relative.lower().replace('/','\\')]
 with archive.open('rb') as f:f.seek(entry.offset);b=f.read(entry.size & BSAArchive.SIZE_MASK)
 if bool(metadata.header.archive_flags.files_compressed)!=bool(entry.size & BSAArchive.COMPRESSED_MASK):
  length=int.from_bytes(b[:4],'little');b=lz4.frame.decompress(b[4:]);assert len(b)==length
 dest=W/'source'/Path(relative);dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(b);return dest
horse_paths=[s for s in entries if s.startswith('meshes\\actors\\horse\\animations\\') and s.endswith('.hkx')]
print('Horse clips:',[s.rsplit('\\',1)[-1] for s in horse_paths])
H=load_skyrim_skeleton(str(extract('meshes/actors/horse/character assets/skeleton.hkx')))
S=load_skyrim_skeleton(str(extract('meshes/actors/character/character assets/skeleton.hkx')))
base=load_skyrim_animation(args.calibration)
def rot(q):
 x,y,z,w=np.asarray(q)/np.linalg.norm(q)
 return np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])
def quat(m):
 c=np.array([1+m[0,0]-m[1,1]-m[2,2],1-m[0,0]+m[1,1]-m[2,2],1-m[0,0]-m[1,1]+m[2,2],1+np.trace(m)])
 i=int(np.argmax(c));q=np.zeros(4);q[i]=math.sqrt(max(0,c[i]))/2;d=4*q[i]
 if i==3:q[:3]=[(m[2,1]-m[1,2])/d,(m[0,2]-m[2,0])/d,(m[1,0]-m[0,1])/d]
 else:
  j=(i+1)%3;k=(i+2)%3;q[j]=(m[j,i]+m[i,j])/d;q[k]=(m[k,i]+m[i,k])/d;q[3]=(m[k,j]-m[j,k])/d
 return q/np.linalg.norm(q)
def pose(s,a=None,f=0):
 t=[np.asarray(b.translation) for b in s.reference_pose];r=[rot(b.rotation) for b in s.reference_pose]
 if a:
  for n,tr in enumerate(a.tracks):
   i=a.track_to_bone_indices[n] if a.track_to_bone_indices else n
   if i>=0:t[i]=np.asarray(tr.translations[min(f,len(tr.translations)-1)]);r[i]=rot(tr.rotations[min(f,len(tr.rotations)-1)])
 return t,r
def world(s,t,r):
 pos=[];rs=[]
 for i,parent in enumerate(s.parents):pos.append(t[i] if parent<0 else pos[parent]+rs[parent]@t[i]);rs.append(r[i] if parent<0 else rs[parent]@r[i])
 return np.asarray(pos),np.asarray(rs)
bt,br=pose(S,base);bp,bw=world(S,bt,br);hp,hw=world(H,*pose(H))
mapping={'NPC COM [COM ]':'Horse_COM','NPC Pelvis [Pelv]':'HorsePelvis','NPC Spine [Spn0]':'HorseSpine1','NPC Spine1 [Spn1]':'HorseSpine2','NPC Spine2 [Spn2]':'HorseRibcage','NPC Neck [Neck]':'HorseNeck3','NPC Head [Head]':'HorseScull'}
for side in ('L','R'):
 front='HorseL' if side=='L' else 'HorseFrontRLeg'
 mapping.update({f'NPC {side} Clavicle [{side}Clv]':'HorseLScapula' if side=='L' else 'HorseFrontRLegScapula',f'NPC {side} UpperArm [{side}Uar]':front+'Humerus',f'NPC {side} Forearm [{side}Lar]':front+'Radius',f'NPC {side} Hand [{side}Hnd]':front+'Metacarpus',f'NPC {side} Thigh [{side}Thg]':'Horse'+side+'Femur',f'NPC {side} Calf [{side}Clf]':'Horse'+side+'Tibia',f'NPC {side} Foot [{side}ft ]':'Horse'+side+'LargeCannon'})
indices={S.bones.index(k):H.bones.index(v) for k,v in mapping.items()}
contacts=[S.bones.index(n) for n in ['NPC L Hand [LHnd]','NPC R Hand [RHnd]','NPC L Foot [Lft ]','NPC R Foot [Rft ]','NPC L Calf [LClf]','NPC R Calf [RClf]']]
ground=float(min(bp[contacts,2]));reports=[]
def run(arguments):
 result=subprocess.run([str(x) for x in arguments],capture_output=True)
 assert result.returncode==0,(arguments,result.stdout,result.stderr)
def sdk(name,a):
 dest=W/(name+'-candidate.hkx');write_skyrim_animation(str(dest),a,ptr_size=8)
 b=bytearray(dest.read_bytes());sections=_parse_hkx_sections(b);section=sections['__data__']
 for relative,kind in _parse_virtual_fixups(b,section,sections['__classnames__']['offset']):
  if kind=='hkaAnimationBinding':assert struct.unpack_from('<I',b,section['offset']+relative+56)[0]==0;struct.pack_into('<I',b,section['offset']+relative+60,0x80000000)
 dest.write_bytes(b)
 xml=W/(name+'.xml');final=W/(name+'.hkx');win=W/(name+'-32.hkx');kf=W/(name+'.kf')
 run([args.hkxconv,'convert',dest,xml]);run([args.hkxcmd,'Convert','-v:AMD64',xml,final]);run([args.hkxcmd,'Convert','-v:WIN32',xml,win])
 skeleton=W/'human32.hkx'
 if not skeleton.exists():
  skeletonXml=W/'human.xml';run([args.hkxconv,'convert',extract('meshes/actors/character/character assets/skeleton.hkx'),skeletonXml]);run([args.hkxcmd,'Convert','-v:WIN32',skeletonXml,skeleton])
 run([args.hkxcmd,'ExportKF',skeleton,win,kf]);c=load_skyrim_animation(str(dest));d=load_skyrim_animation(str(final))
 assert d.num_tracks==99 and c.tracks==d.tracks and c.track_to_bone_indices==d.track_to_bone_indices
 assert kf.stat().st_size>100
 reports.append(dict(name=name,tracks=99,sdkDecode=True,frames=d.num_frames,bytes=final.stat().st_size,sha256=hashlib.sha256(final.read_bytes()).hexdigest()))
 return final
def convert(name,sourceName,reverse=False):
 source=load_skyrim_animation(str(extract('meshes/actors/horse/animations/'+sourceName+'.hkx')))
 a=AnimationData(duration=source.duration,num_frames=source.num_frames,num_tracks=99,frame_duration=source.frame_duration,original_skeleton_name=base.original_skeleton_name,bone_names=list(S.bones),track_to_bone_indices=list(range(99)),tracks=[TrackData() for _ in S.bones])
 # Use the source clip's first pose as a neutral delta; calibration keeps human proportions.
 _,reference=world(H,*pose(H,source,0))
 for frame in range(a.num_frames):
  f=a.num_frames-1-frame if reverse else frame;_,motion=world(H,*pose(H,source,f));rs=[]
  for i,parent in enumerate(S.parents):
   if i in indices:
    j=indices[i];r=motion[j]@reference[j].T@bw[i];r=r if parent<0 else rs[parent].T@r
   else:r=br[i]
   rs.append(r if parent<0 else rs[parent]@r)
   a.tracks[i].translations.append(bt[i].tolist());a.tracks[i].rotations.append(quat(r).tolist());a.tracks[i].scales.append([1.,1.,1.])
  positions,_=world(S,*pose(S,a,frame));a.tracks[4].translations[frame][2]+=max(0.,ground-float(min(positions[contacts,2])))
 if name in ('takeoff','land'):
  a.duration=0.2 if name=='takeoff' else 0.3;a.frame_duration=a.duration/(a.num_frames-1)
 return sdk(name,a)
sources={0:('idle','idle'),1:('walk','walkforward'),2:('run','runforward'),3:('sprint','sprintforward'),4:('back','walkforward'),5:('left','turnloopingl'),6:('right','turnloopingr'),7:('takeoff','jumpstart'),8:('air','jumpfall'),9:('land','jumpland')}
def full_human(original):
 a=copy.deepcopy(original);a.num_tracks=99;a.bone_names=list(S.bones);a.track_to_bone_indices=list(range(99))
 a.tracks=[TrackData(translations=[list(b.translation) for _ in range(a.num_frames)],rotations=[list(b.rotation) for _ in range(a.num_frames)],scales=[list(b.scale) for _ in range(a.num_frames)]) for b in S.reference_pose]
 for n,tr in enumerate(original.tracks):
  index=original.track_to_bone_indices[n] if original.track_to_bone_indices else n
  assert -1<=index<99,index
  if index>=0:a.tracks[index]=copy.deepcopy(tr)
 return a

if args.crawl_dir:
 crawl=Path(args.crawl_dir)
 crawlFiles={0:'female/mt_idle.hkx',1:'female/mt_walkforward.hkx',2:'female/mt_runforward.hkx',3:'female/mt_sprintforward.hkx',4:'female/mt_walkforward.hkx',5:'mt_turnleft60.hkx',6:'mt_turnright60.hkx',7:'female/mt_idle.hkx',8:'female/mt_idle.hkx',9:'female/mt_idle.hkx'}
 clips={};seatBone=S.bones.index('NPC Spine2 [Spn2]');comBone=S.bones.index('NPC COM [COM ]')
 for rank,(name,_) in sources.items():
  original=load_skyrim_animation(str(crawl/crawlFiles[rank]));assert original.num_tracks==99 and original.blend_hint==0
  a=full_human(original)
  # Jump height comes from the player's physical controller, not a second
  # animated root displacement. Hold the crawl pose during airborne phases.
  if rank in (7,8,9):
   for tr in a.tracks:
    for attr in ['translations','rotations','scales']:
     first=list(getattr(tr,attr)[0]);setattr(tr,attr,[first[:] for _ in range(a.num_frames)])
  if rank==4:
   for tr in a.tracks:
    for attr in ['translations','rotations','scales']:getattr(tr,attr).reverse()
  # Fix the back's seating anchor across every gait. Only authored COM changes;
  # limb rotation remains the donor's human crawl, with no runtime node writes.
  for frame in range(a.num_frames):
   pos,rs=world(S,*pose(S,a,frame));offset=bp[seatBone]-pos[seatBone];parent=S.parents[comBone]
   local=offset if parent<0 else rs[parent].T@offset
   a.tracks[comBone].translations[frame]=(np.array(a.tracks[comBone].translations[frame])+local).tolist()
  clips[rank]=sdk(name,a)
else:
 clips={rank:convert(name,source,rank==4) for rank,(name,source) in sources.items()}
riderPath=next(s for s in entries if s.endswith('mc_1hm_idle.hkx') and 'horse_rider' in s)
rider=full_human(load_skyrim_animation(str(extract(riderPath.replace('\\','/')))))
# Author the seat in model-space, not by moving scene nodes after animation/SMP.
# Both actors share a ground-level origin. Camera roots retain normal human idle values.
seat=bp[S.bones.index('NPC Spine2 [Spn2]')]+np.array([0.,0.,12.])
riderPositions,_=world(S,*pose(S,rider,0));seatOffset=seat-riderPositions[S.bones.index('NPC Pelvis [Pelv]')]
com=S.bones.index('NPC COM [COM ]')
parent=S.parents[com]
_,riderWorld=world(S,*pose(S,rider,0))
localSeat=seatOffset if parent<0 else riderWorld[parent].T@seatOffset
for frame in range(rider.num_frames):
 rider.tracks[com].translations[frame]=(np.array(rider.tracks[com].translations[frame])+localSeat).tolist()
# A stable rider pose must not inherit the horse rider idle's own body bob.
for tr in rider.tracks:
 for attribute in ['translations','rotations','scales']:
  value=list(getattr(tr,attribute)[0]);setattr(tr,attribute,[value[:] for _ in range(rider.num_frames)])
humanIdlePath=next(k for k in entries if k.startswith('meshes\\actors\\character\\animations\\') and k.endswith('\\mt_idle.hkx'))
humanIdle=full_human(load_skyrim_animation(str(extract(humanIdlePath.replace('\\','/')))))
cameraIndices=[S.bones.index(n) for n in ['Camera3rd [Cam3]','Camera Control']]
for index in cameraIndices:
 for attribute in ['translations','rotations','scales']:
  value=getattr(humanIdle.tracks[index],attribute)[0]
  setattr(rider.tracks[index],attribute,[list(value) for _ in range(rider.num_frames)])
riderFile=sdk('rider',rider)
riderJumps={}
humanJump={'jumpstart':'mt_jump','jumpfall':'mt_jumpfall','jumpland':'mt_jumpland','jumpstart_sprint':'mt_jumpfast','jumpfall_sprint':'mt_jumpfall','jumpland_sprint':'mt_jumpland'}
for alias in ['jumpstart','jumpfall','jumpland','jumpstart_sprint','jumpfall_sprint','jumpland_sprint']:
 key=next(s for s in entries if s.startswith('meshes\\actors\\character\\animations\\') and s.endswith('\\'+humanJump[alias]+'.hkx') and s.count('\\')<=6)
 original=load_skyrim_animation(str(extract(key.replace('\\','/'))))
 a=full_human(original)
 for i,tr in enumerate(a.tracks):
  for attribute in ['translations','rotations','scales']:
   value=getattr(rider.tracks[i],attribute)[0];setattr(tr,attribute,[list(value) for _ in range(a.num_frames)])
 riderJumps[alias+'.hkx']=sdk('rider-'+alias,a)
 for filename in [humanJump[alias]+'.hkx',humanJump[alias]+'left.hkx',humanJump[alias]+'right.hkx']:
  riderJumps.setdefault(filename,riderJumps[alias+'.hkx'])
aliases=set();aliasBindings={}
for key in entries:
 prefix='meshes\\actors\\character\\animations\\'
 if not key.startswith(prefix) or not key.endswith('.hkx'):continue
 relative=key[len(prefix):].replace('\\','/')
 if '/' in relative and not relative.startswith('female/'):continue
 filename=relative.rsplit('/',1)[-1]
 locomotion=any(token in filename for token in ['walk','run','sprint','turn']) and not any(token in filename for token in ['attack','block','whirlwind','behavior'])
 if locomotion or filename.startswith(('mt_idle','mt_jump')) or filename in ['sneakmtidle.hkx','jumpstart.hkx','jumpfall.hkx','jumpland.hkx','jumpstart_sprint.hkx','jumpfall_sprint.hkx','jumpland_sprint.hkx']:
  aliases.add(filename)
  aliasBindings[relative]=load_skyrim_animation(str(extract(key.replace('\\','/')))).blend_hint
# Neutralize additive overlays instead of applying an absolute whole-body pose twice.
neutral=AnimationData(duration=1.,num_frames=31,num_tracks=99,frame_duration=1./30,original_skeleton_name=base.original_skeleton_name,bone_names=list(S.bones),track_to_bone_indices=list(range(99)),blend_hint=1,
 tracks=[TrackData(translations=[[0.,0.,0.] for _ in range(31)],rotations=[[0.,0.,0.,1.] for _ in range(31)],scales=[[1.,1.,1.] for _ in range(31)]) for _ in S.bones])
neutralFile=sdk('neutral-additive',neutral)
OAR=Path(args.output or R/'dist')/'meshes/actors/character/animations/OpenAnimationReplacer/NPCMounts';OAR.mkdir(parents=True,exist_ok=True)
(OAR/'config.json').write_text(json.dumps(dict(name='NPCMounts',author='npc-mounts',description='Per-reference humanoid horse mode'),indent=2))
form=lambda n:dict(pluginName='NPCMounts.esp',formID=f'{n:X}')
def is_additive(prefix,filename):
 return aliasBindings.get(prefix+filename,aliasBindings.get(filename,aliasBindings.get('female/'+filename,0)))==1
for rank,(name,_) in sources.items():
 folder=OAR/f'{rank:02d}-{name}';folder.mkdir(exist_ok=True)
 cfg=dict(name=name,disabled=True,priority=2147483630+rank,interruptible=True,hasCustomBlendTimeOnInterrupt=True,blendTimeOnInterrupt=0.12,conditions=[dict(condition='FactionRank',requiredVersion='1.0.0',Faction=form(0x800),Comparison='==',**{'Numeric value':{'value':rank}})])
 (folder/'config.json').write_text(json.dumps(cfg,indent=2))
 for filename in aliases:
  for prefix in ['', 'female/']:
   target=folder/(prefix+filename);target.parent.mkdir(exist_ok=True)
   target.write_bytes((neutralFile if is_additive(prefix,filename) else clips[rank]).read_bytes())
folder=OAR/'rider';folder.mkdir(exist_ok=True)
(folder/'config.json').write_text(json.dumps(dict(name='rider',disabled=True,priority=2147483645,interruptible=True,conditions=[dict(condition='IsInFaction',requiredVersion='1.0.0',Faction=form(0x801))]),indent=2))
for filename in aliases:
 for prefix in ['', 'female/']:
  target=folder/(prefix+filename);target.parent.mkdir(exist_ok=True)
  target.write_bytes((neutralFile if is_additive(prefix,filename) else riderJumps.get(filename,riderFile)).read_bytes())
(R/'reports/animations.json').write_text(json.dumps(dict(mapping=mapping,sourceMode='human-crawl' if args.crawl_dir else 'horse-retarget',crawlSources=crawlFiles if args.crawl_dir else None,stableBackAnchor=bp[S.bones.index('NPC Spine2 [Spn2]')].tolist(),clips=reports,aliases=sorted(aliases),additiveOverlaysNeutralized=[k for k,v in aliasBindings.items() if v==1],riderJumpAnnotationsPreserved=True,bakedRiderSeat=seat.tolist(),humanIdleCameraPreserved=True,gameTested=False),indent=2))
print('Built',len(reports),'validated clips and',len(aliases)*22,'scoped animation aliases')
