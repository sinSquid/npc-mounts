import sys,json
from pathlib import Path
sys.path.insert(0,r'D:\Codex\2026-09-25\https-xlog-xluser-xunlei-cn-app\work\tools\pynifly-29\io_scene_nifly')
from pyn.pynifly import NifFile
mods=Path(r'E:\iniRePather-Skyrim\Mod Organizer 2\mods')
for p in [mods/'DD拘束框架Devious Devices for SE-AE-VR/meshes/devious/furniture/kimy/restraintpost_static.nif',mods/'【实验室】LeashFramework 1.1.7/meshes/Leash/neck_rope.nif']:
 n=NifFile(str(p))
 print(json.dumps({'path':str(p),'nodes':list(n.nodes),'textures':[s.textures for s in n.shapes]},ensure_ascii=False))
