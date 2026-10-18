"""Read existing ride logs and sampled crawl resources; does not write game files."""
import argparse
import json
import math
import re
import struct
from pathlib import Path


def point(frame, parents, bone):
    p = [0.0, 0.0, 0.0]
    while bone >= 0:
        b = frame[bone]
        x, y, z, w = b[4:8]
        px, py, pz = [p[i] * b[8+i] for i in range(3)]
        p = [b[0]+(1-2*(y*y+z*z))*px+2*(x*y-z*w)*py+2*(x*z+y*w)*pz,
             b[1]+2*(x*y+z*w)*px+(1-2*(x*x+z*z))*py+2*(y*z-x*w)*pz,
             b[2]+2*(x*z-y*w)*px+2*(y*z+x*w)*py+(1-2*(x*x+y*y))*pz]
        bone = parents[bone]
    return p


def bank(path):
    data = path.read_bytes()
    magic, version, bones, count = struct.unpack_from('<4I', data)
    assert (magic, version, bones, count) == (0x434D504E, 1, 99, 8)
    names = [data[16+i*64:16+(i+1)*64].split(b'\0')[0].decode('ascii') for i in range(bones)]
    parents = struct.unpack_from('<99h', data, 16+bones*64)
    spine = names.index('NPC Spine2 [Spn2]')
    at = 16+bones*64+bones*2
    result = []
    for clip in range(count):
        duration, frames = struct.unpack_from('<fI', data, at)
        at += 8
        points = []
        root_scales = []
        for _ in range(frames):
            pose = list(struct.iter_unpack('<12f', data[at:at+bones*48]))
            at += bones*48
            points.append(point(pose, parents, spine))
            root_scales.append(pose[0][8:11])
        result.append(dict(clip=clip, duration=duration, frames=frames,
                           spineMin=[min(p[i] for p in points) for i in range(3)],
                           spineMax=[max(p[i] for p in points) for i in range(3)],
                           rootScaleMin=[min(p[i] for p in root_scales) for i in range(3)],
                           rootScaleMax=[max(p[i] for p in root_scales) for i in range(3)]))
    assert at == len(data)
    return result


def log(path):
    lines = path.read_text(encoding='utf-8').splitlines()
    groups = {}
    current = None
    velocity = None
    for line in lines:
        m = re.search(r'Motion stage=player-after .*velocity=\(([^,]+),([^,]+),([^\)]+)\)', line)
        if m:
            velocity = [float(x) for x in m.groups()]
        m = re.search(r'Ride detail .*input=\(([^,]+),([^\)]+)\) yaw=\(([^,]+),([^\)]+)\).*controllerState=(\d+)', line)
        if m:
            x, y, yaw, mount_yaw = map(float, m.groups()[:4])
            mode = 'S' if y < -.1 else 'W' if y > .1 else 'idle'
            current = groups.setdefault(mode, dict(samples=0, moving=0, oppositeMotion=0,
                                                   pelvisHorizontal=[], vertical=[], airborne=0))
            current['samples'] += 1
            current['airborne'] += int(int(m[5]) in (1, 2))
            if velocity and mode != 'idle':
                forward = velocity[0]*math.sin(yaw)+velocity[1]*math.cos(yaw)
                if math.hypot(*velocity[:2]) > .1:
                    current['moving'] += 1
                    current['oppositeMotion'] += int(forward*y < 0)
        m = re.search(r'Ride visual .*pelvisBack=\(([^,]+),([^,]+),([^\)]+)\)', line)
        if m and current:
            x, y, z = map(float, m.groups())
            current['pelvisHorizontal'].append(math.hypot(x, y))
            current['vertical'].append(z)
    for g in groups.values():
        horizontal = g.pop('pelvisHorizontal')
        vertical = g.pop('vertical')
        g['horizontalMax'] = max(horizontal, default=0)
        g['horizontalAbove20'] = sum(x > 20 for x in horizontal)
        g['verticalRange'] = [min(vertical, default=0), max(vertical, default=0)]
    return dict(file=str(path), build=[x for x in lines if 'Runtime W:' in x],
                groups=groups,
                mountEvents=sum('Mounted ref=' in x for x in lines),
                transitionStarts=sum('Dismount animation started' in x for x in lines),
                note='Velocity direction compares sampled yaw with latest sampled player-after velocity; not an animation identity or final-stage trace.')


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--log', type=Path, required=True)
    p.add_argument('--bank', type=Path, required=True)
    args = p.parse_args()
    print(json.dumps(dict(log=log(args.log), crawlResource=bank(args.bank)), ensure_ascii=False, indent=2))
