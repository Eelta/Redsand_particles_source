from pathlib import Path
import sys
import math
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from nif_schema import DirectionalNif
from build_block_lightning import make_nif

for variant in range(4):
    path = ROOT/f'assets/Basic/Meshes/RedsandBlockLightning/Arc{variant}.nif'
    data = path.read_bytes()
    assert data == make_nif(90817+variant*7169), 'Published mesh cannot be reproduced from corresponding source'
    doc = DirectionalNif.from_bytes(data); doc.validate()
    assert len(doc.indices('BSTriShape')) == 60
    assert not doc.indices('NiBillboardNode')
    nodes = {i: doc.values(i) for i in doc.indices('NiNode')}
    parent = {child: i for i, n in nodes.items() for child in n['children']}
    def position(i):
        p = nodes[i]['translation']
        if i in parent:
            q = position(parent[i]); return tuple(x+y for x, y in zip(p, q))
        return p
    xyz = []
    for piece, i in enumerate(doc.indices('BSTriShape')):
        fields = doc.values(i)
        vertices = fields['vertices']; origin = position(parent[i])
        xyz.extend(tuple(x+y for x, y in zip(v[:3], origin)) for v in vertices)
        assert all(math.isfinite(x) for v in vertices for x in v)
        part = piece % 5
        rows = 97 if part == 0 else 41 if part in (1, 3) else 23
        assert len(vertices) == rows*6, 'Extra transparent sheath/halo geometry remains'
        assert min(v[4] for v in vertices) == 0 and max(v[4] for v in vertices) == 1
        assert all(v[-4:-1] == (255, 255, 255) for v in vertices), 'Fixed vertex tint overrides the player RGB'
        for k in range(0, len(vertices), 6):
            ring = vertices[k:k+6]
            center = tuple(sum(v[axis] for v in ring)/6 for axis in range(3))
            assert max(math.dist(v[:3], center) for v in ring) <= .2001, 'Wide outer shell remains'

        parent_node = parent.get(parent[i], 0)
        if parent_node:
            ancestor_shape = nodes[parent_node]['children'][0]
            v = doc.values(ancestor_shape)['vertices']
            centers = [tuple(sum(row[axis] for row in v[k:k+6])/6 for axis in range(3)) for k in range(0, len(v), 6)]
            local = nodes[parent[i]]['translation']
            assert min(sum((x-y)**2 for x, y in zip(c, local)) for c in centers) < .00001, 'Articulation/fork joint is detached'
        shader = doc.values(fields['shader'])
        assert shader['shader_flags_1'] & (1 << 22), 'Emissive core lost its own-emission material'
        assert shader['clamp_mode'] == 0, 'UVs outside the current window must clamp to transparent ends, never repeat'
        assert shader['shader_flags_1'] & (1 << 31), 'Scene depth testing disabled'
        assert not shader['shader_flags_2'] & 1, 'Additive arcs must not occlude scenery'
        assert shader['base_color'][3] == 0, 'Asynchronous loading could flash an uninitialized white mesh'
    for axis in range(3): assert max(p[axis] for p in xyz)-min(p[axis] for p in xyz) > 300
    assert doc.texture_paths() == ['Textures/RedsandBlockLightning/Travel.dds']
    assert len(xyz) == 16200
    print(f'Arc{variant}: 60 stationary cores, no outer shell, emissive/depth-tested/reproducible; {len(xyz)} vertices')
profile = (ROOT/'assets/Basic/Textures/RedsandBlockLightning/Travel.dds').read_bytes()
assert len(profile) == 128+256*4 and profile[131] == 0 and profile[-1] == 0
assert max(profile[131::4]) == 255
