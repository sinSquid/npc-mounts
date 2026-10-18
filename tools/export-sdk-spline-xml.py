"""Export raw compressed data to SDK-readable XML, without sampling/repairing it.

HavokPy supplies base objects; the original packfile supplies the omitted spline
members. The SDK, not the Python spline decoder, will decompress this output.
"""
import re
import struct
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

sys.path.insert(0, sys.argv[1])
sys.path.insert(0, sys.argv[2])
import havokpy
import anim_skyrim as hkx

source, destination = map(Path, sys.argv[3:5])
raw = source.read_bytes()
assert raw[16] == 8
sections = hkx._parse_hkx_sections(raw)
data = sections['__data__']
fixups = hkx._parse_local_fixups(raw, data)
objects = hkx._parse_virtual_fixups(raw, data, sections['__classnames__']['offset'])
relative = next(r for r, name in objects if name == 'hkaSplineCompressedAnimation')
absolute = data['offset']+relative
root = ET.fromstring(havokpy.hkPackfile(str(source)).to_xml(11))
animation = root.find('.//hkobject[@class="hkaSplineCompressedAnimation"]')
assert animation is not None
fields = [('numFrames', 0x38, 'I'), ('numBlocks', 0x3c, 'I'),
          ('maxFramesPerBlock', 0x40, 'I'), ('maskAndQuantizationSize', 0x44, 'I'),
          ('blockDuration', 0x48, 'f'), ('blockInverseDuration', 0x4c, 'f'),
          ('frameDuration', 0x50, 'f')]
for name, offset, fmt in fields:
    ET.SubElement(animation, 'hkparam', name=name).text = str(struct.unpack_from('<'+fmt, raw, absolute+offset)[0])
for name, offset in [('blockOffsets', 0x58), ('floatBlockOffsets', 0x68),
                     ('transformOffsets', 0x78), ('floatOffsets', 0x88)]:
    values = hkx._read_hkarray_u32(raw, data['offset'], relative, offset, fixups, 8)
    ET.SubElement(animation, 'hkparam', name=name, numelements=str(len(values))).text = ' '.join(map(str, values))
blob = hkx._read_hkarray_u8(raw, data['offset'], relative, 0x98, fixups, 8)
ET.SubElement(animation, 'hkparam', name='data', numelements=str(len(blob))).text = ' '.join(map(str, blob))
ET.SubElement(animation, 'hkparam', name='endian').text = '0'
text = ET.tostring(root, encoding='unicode')
identifiers = {x.attrib['name']: '#%04d' % i for i, x in enumerate(root.findall('.//hkobject[@name]'))}
for old, new in identifiers.items():
    text = re.sub(re.escape(old)+r'(?![0-9a-fA-F])', new, text)
destination.write_text('<?xml version="1.0"?>\n'+text, encoding='utf-8')
print('Raw compressed bytes preserved:', len(blob), '->', destination)
