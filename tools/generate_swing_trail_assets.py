from pathlib import Path
import math
import struct
from generate_weapon_assets import NifDocument, particle_systems, particle_controllers, shader_texture, dds_bytes

ROOT = Path(__file__).resolve().parents[1]

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

if __name__ == '__main__':
    for relative, content in [('Meshes/RedsandSwingTrail/Arc.nif', model()), ('Textures/RedsandSwingTrail/Point.dds', texture())]:
        target = ROOT / 'assets/Basic' / relative; target.parent.mkdir(parents=True,exist_ok=True); target.write_bytes(content)
    print('Generated private swing-trail template and mathematical point texture')
