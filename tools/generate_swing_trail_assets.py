from pathlib import Path
import math
import struct
from generate_weapon_assets import NifDocument, particle_systems, particle_controllers, shader_texture, dds_bytes
from nif_schema import DirectionalNif

ROOT = Path(__file__).resolve().parents[1]
RIBBON_SLOTS = 32
RIBBON_ROWS = 512

def model():
    doc = NifDocument.parse((ROOT / 'assets/Basic/Meshes/RedsandParticles/Default.nif').read_bytes())
    for system in particle_systems(doc):
        _, start, end = shader_texture(doc, system.shader)
        path = b'textures\\RedsandSwingTrail\\Point.dds'
        block = doc.blocks[system.shader]
        doc.blocks[system.shader] = block[:start] + struct.pack('<I', len(path)) + path + block[end:]
        birth = particle_controllers(doc, system)[0]
        interpolator = doc.blocks[birth.interpolator]
        doc.blocks[birth.interpolator] = struct.pack('<f', 0.) + interpolator[4:]
    for index, kind in enumerate(doc.types):
        if kind == 'NiPSysData':
            block = bytearray(doc.blocks[index]); struct.pack_into('<H', block, 4, 4096); doc.blocks[index] = bytes(block)
    return doc.serialize()

def texture(size=128):
    pixels = bytearray()
    for y in range(size):
        for x in range(size):
            radius = math.hypot((x+.5-size/2)/(size/2), (y+.5-size/2)/(size/2))
            edge = min(1., max(0., (radius-.25)/.7))
            alpha = 1.-edge*edge*(3.-2.*edge)
            pixels.extend((255,255,255,round(alpha*255)))
    return dds_bytes(bytes(pixels),size)

def ribbon_profile(size=128):
    def fade(low, high, value):
        t = min(1., max(0., (value - low) / (high - low)))
        return 1. - t*t*(3. - 2.*t)

    row = bytearray()
    for x in range(size):
        distance = abs(2. * (x + .5) / size - 1.)
        core = fade(.26, .46, distance)
        halo = .32 * fade(.36, .96, distance)
        row.extend((255, 255, 255, round(255. * (core + (1. - core) * halo))))
    return dds_bytes(bytes(row) * size, size)

def ribbon_model():
    def pack(fmt, *values):
        return struct.pack('<' + fmt, *values)

    def string(value):
        value = value.encode('ascii')
        return pack('I', len(value)) + value

    names = ['RedsandRibbonPool', 'RedsandRibbonAlpha', 'RedsandRibbonMaterial']
    names.extend(f'RedsandRibbon{i:02}' for i in range(RIBBON_SLOTS))
    identity = (1., 0., 0., 0., 1., 0., 0., 0., 1.)

    def net(name):
        return pack('IIi', name, 0, -1)

    def av(name):
        return net(name) + pack('I3f9ffi', 14, 0., 0., 0., *identity, 1., -1)

    root_children = [2 + 2 * slot for slot in range(RIBBON_SLOTS)]
    blocks = [
        ('NiNode', av(0) + pack('I', len(root_children)) + pack('i' * len(root_children), *root_children) + pack('I', 0)),
        ('NiAlphaProperty', net(1) + pack('HB', 4109, 0)),
    ]
    triangles = bytearray()
    for row in range(RIBBON_ROWS - 1):
        for column in range(2):
            a = row * 3 + column
            triangles += pack('6H', a, a + 1, a + 3, a + 3, a + 1, a + 4)
    descriptor = (1059 << 44) | 6 | (16 << 6) | (20 << 22)
    texture_path = b'Textures/RedsandSwingTrail/RibbonProfile.dds'
    for slot in range(RIBBON_SLOTS):
        shape_id = root_children[slot]
        # Start with a distinct, invisible strip in each slot so no writable buffer can be deduplicated.
        vertices = b''.join(pack('4f2e4B', row * .01, column - 1., float(slot), 0., column * .5, .5, 255, 255, 255, 0)
                            for row in range(RIBBON_ROWS) for column in range(3))
        shape = av(3 + slot) + pack('4fiiiQHHI', 0., 0., 0., 64., -1, shape_id + 1, 1,
                                    descriptor, len(triangles) // 6, RIBBON_ROWS * 3, len(vertices) + len(triangles))
        blocks.append(('BSTriShape', shape + vertices + triangles + pack('I', 0)))
        shader = net(2) + pack('II4fI', (1 << 31) | (1 << 3) | (1 << 22), (1 << 5) | (1 << 4),
                               0., 0., 1., 1., len(texture_path)) + texture_path
        # The shader multiplies material alpha by the opacity uploaded with each vertex.
        shader += pack('4B10fI', 0, 0, 0, 0, 1., 1., 1., 1., 1., 1., 1., 1., 14. + slot / 1024., 0., 0)
        blocks.append(('BSEffectShaderProperty', shader))
    types = list(dict.fromkeys(kind for kind, _ in blocks))
    data = b'Gamebryo File Format, Version 20.2.0.7\n' + pack('IBIII', 0x14020007, 1, 12, len(blocks), 100) + bytes(3)
    data += pack('H', len(types)) + b''.join(string(kind) for kind in types)
    data += b''.join(pack('H', types.index(kind)) for kind, _ in blocks)
    data += b''.join(pack('I', len(block)) for _, block in blocks)
    data += pack('II', len(names), max(map(len, names))) + b''.join(string(name) for name in names) + pack('I', 0)
    data += b''.join(block for _, block in blocks) + pack('Ii', 1, 0)
    document = DirectionalNif.from_bytes(data)
    document.validate()
    return data

if __name__ == '__main__':
    for relative, content in [('Meshes/RedsandSwingTrail/Arc.nif', model()), ('Textures/RedsandSwingTrail/Point.dds', texture()),
                              ('Meshes/RedsandSwingTrail/Ribbon.nif', ribbon_model()),
                              ('Textures/RedsandSwingTrail/RibbonProfile.dds', ribbon_profile())]:
        target = ROOT / 'assets/Basic' / relative; target.parent.mkdir(parents=True,exist_ok=True); target.write_bytes(content)
    print('Generated swing-trail meshes and analytic point and ribbon textures')
