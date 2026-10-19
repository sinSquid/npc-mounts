"""Add missing vanilla swim aliases to an existing rider package; preserve existing files."""
import argparse, hashlib, json, sys
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--bsa-lib',required=True);p.add_argument('--codec',required=True)
p.add_argument('--archive',required=True);p.add_argument('--stage',required=True)
p.add_argument('--report',required=True)
a=p.parse_args();sys.path.insert(0,a.bsa_lib);sys.path.insert(0,a.codec)
import lz4.frame
from bethesda_structs.archive.bsa import BSAArchive
from anim_skyrim import load_skyrim_animation
root=Path(__file__).resolve().parents[1]
report=json.loads(Path(a.report).read_text())
with open(a.archive,'rb') as f: metadata=BSAArchive.archive_struct.parse(f.read(1024*1024))
index=0;aliases={}
work=root/'build/swim-inspection';work.mkdir(exist_ok=True)
for folder in metadata.directory_blocks:
    for entry in folder.file_records:
        key=(folder.name.rstrip('\0')+'\\'+metadata.file_names[index]).lower();index+=1
        prefix='meshes\\actors\\character\\animations\\'
        if not key.startswith(prefix) or not key.endswith('.hkx'):continue
        relative=key[len(prefix):]
        if '\\' in relative and not relative.startswith('female\\'):continue
        filename=relative.rsplit('\\',1)[-1]
        if 'swim' not in filename:continue
        with open(a.archive,'rb') as f:f.seek(entry.offset);data=f.read(entry.size & BSAArchive.SIZE_MASK)
        if bool(metadata.header.archive_flags.files_compressed)!=bool(entry.size & BSAArchive.COMPRESSED_MASK):
            size=int.from_bytes(data[:4],'little');data=lz4.frame.decompress(data[4:]);assert len(data)==size
        path=work/relative.replace('\\','/');path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
        clip=load_skyrim_animation(str(path));assert clip.blend_hint in (0,1)
        aliases[relative.replace('\\','/')]=clip.blend_hint
assert aliases, 'No vanilla swimming clips found'
folder=Path(a.stage)/'meshes/actors/character/animations/OpenAnimationReplacer/NPCMounts/rider'
valid={item['sha256'] for item in report['clips']}
for filename in sorted({Path(key).name for key in aliases}):
    for prefix in ('','female/'):
        key=prefix+filename
        additive=aliases.get(key,aliases.get(filename,aliases.get('female/'+filename)))==1
        source=root/'build/animations'/('neutral-additive.hkx' if additive else 'rider.hkx')
        data=source.read_bytes();assert hashlib.sha256(data).hexdigest() in valid
        target=folder/key
        if target.exists():assert target.read_bytes()==data
        else:target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
report['aliases']=sorted(set(report['aliases'])|{Path(key).name for key in aliases})
report['swimSourceBindings']=aliases
Path(a.report).write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(aliases,indent=2))
