"""Read the SDK-exported KF's linear root scale keys, without a spline decoder."""
import struct
import sys
from pathlib import Path

data = Path(sys.argv[1]).read_bytes()
at = data.index(b'\n')+1


def read(fmt):
    global at
    value = struct.unpack_from('<'+fmt, data, at)
    at += struct.calcsize('<'+fmt)
    return value[0] if len(value) == 1 else value


def string(fmt='I'):
    global at
    length = read(fmt)
    value = data[at:at+length].decode('utf-8')
    at += length
    return value


assert read('I') == 0x14020007
assert read('B') == 1
assert read('I') == 11
count = read('I')
read('I')  # Bethesda stream version
for _ in range(3):
    string('B')
types = [string() for _ in range(read('H'))]
indices = [read('H') for _ in range(count)]
sizes = [read('I') for _ in range(count)]
strings, longest = read('I'), read('I')
names = [string() for _ in range(strings)]
for _ in range(read('I')):
    read('I')
blocks = []
for index, size in zip(indices, sizes):
    blocks.append((types[index], at, size))
    at += size
for kind, start, size in blocks:
    if kind != 'NiTransformData':
        continue
    at = start
    rotations = read('I')
    if rotations:
        rotation_type = read('I')
        assert rotation_type == 1
        at += rotations*20
    translations = read('I')
    if translations:
        translation_type = read('I')
        assert translation_type == 1
        at += translations*16
    scales = read('I')
    if scales:
        scale_type = read('I')
        assert scale_type == 1
        keys = [read('2f') for _ in range(scales)]
        print('First transform track SDK scale keys:', scales, 'range',
              min(x[1] for x in keys), max(x[1] for x in keys), keys[:5])
    break
