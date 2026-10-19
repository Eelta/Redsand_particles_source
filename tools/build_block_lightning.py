
from pathlib import Path
import math
import random
import struct
import json
import hashlib
from nif_schema import DirectionalNif

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / 'assets/Basic'
P = lambda fmt, *v: struct.pack('<' + fmt, *v)
IDENTITY = (1., 0., 0., 0., 1., 0., 0., 0., 1.)


def add(a, b): return tuple(x+y for x, y in zip(a, b))
def sub(a, b): return tuple(x-y for x, y in zip(a, b))
def mul(a, s): return tuple(x*s for x in a)
def cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def norm(a): return mul(a, 1/max(1.e-9, math.sqrt(sum(x*x for x in a))))


def curve(rng, direction, length, count):
    axis = norm(direction)
    u = norm(cross(axis, (0, 0, 1) if abs(axis[2]) < .9 else (0, 1, 0)))
    v = cross(axis, u)

    knots = [[rng.uniform(-1, 1) for _ in range(n+1)] for n in (8, 19, 43)]
    knots2 = [[rng.uniform(-1, 1) for _ in range(n+1)] for n in (8, 19, 43)]
    def noise(t, series):
        result = 0.
        for values, amp in zip(series, (.075, .025, .008)):
            q = t*(len(values)-1); k = min(len(values)-2, int(q)); f = q-k
            result += (values[k]*(1-f)+values[k+1]*f)*amp
        return result
    return [add(mul(axis, length*t), add(mul(u, length*noise(t, knots)*math.sin(math.pi*t)**.35),
            mul(v, length*noise(t, knots2)*math.sin(math.pi*t)**.35))) for t in (i/(count-1) for i in range(count))]


def branches(seed):
    rng = random.Random(seed)
    result = []
    for group in range(12):
        z = .82 - 1.64*(group+.5)/12
        phi = group*2.3999632297 + rng.uniform(-.3, .3)
        direction = (math.sqrt(1-z*z)*math.cos(phi), math.sqrt(1-z*z)*math.sin(phi), z)
        points = curve(rng, direction, rng.uniform(240, 355), 97)
        parent = len(result)
        result.append(dict(parent=-1, origin=mul(norm(direction), rng.uniform(14, 24)), points=points, level=0))
        for at in (38, 65):
            d = norm(add(norm(sub(points[at+1], points[at-1])), tuple(rng.uniform(-1.3, 1.3) for _ in range(3))))
            side = curve(rng, d, rng.uniform(75, 135), 41)
            parent2 = len(result)
            result.append(dict(parent=parent, attach=at, origin=points[at], points=side, level=1))
            twigdir = norm(add(d, tuple(rng.uniform(-1.5, 1.5) for _ in range(3))))
            result.append(dict(parent=parent2, attach=22, origin=side[22], points=curve(rng, twigdir, rng.uniform(35, 70), 23), level=2))
    return result


def tube(branch):
    points = branch['points']; verts, tris = [], []

    for radius, alpha in ((.20, 255),):
        radius *= (.64 ** branch['level'])
        start = len(verts)
        previous = None
        for i, point in enumerate(points):
            t = i/(len(points)-1)
            tangent = norm(sub(points[min(i+1, len(points)-1)], points[max(0, i-1)]))
            if previous is None:
                u = norm(cross(tangent, (0, 0, 1) if abs(tangent[2]) < .9 else (0, 1, 0)))
            else:
                u = norm(sub(previous, mul(tangent, sum(x*y for x, y in zip(previous, tangent)))))
            v = cross(tangent, u); previous = u
            width = radius * (.95-.86*t**2)
            fade = min(1., t*25) * min(1., (1-t)*16)
            for k in range(6):
                a = k*math.tau/6
                p = add(point, mul(add(mul(u, math.cos(a)), mul(v, math.sin(a))), width))
                verts.append((*p, 0., t, k/6, 255, 255, 255, round(alpha*fade)))
            if i:
                for k in range(6):
                    a = start+(i-1)*6+k; b = start+(i-1)*6+(k+1)%6
                    c = start+i*6+k; d = start+i*6+(k+1)%6
                    tris.extend(((a, b, c), (c, b, d)))
    return verts, tris


def stable_paths(seed):

    result = []
    for branch in branches(seed):
        vertices, triangles = tube(branch)
        result.append(dict(parent=branch['parent'], origin=branch['origin'], vertices=vertices, triangles=triangles))
    return result


def make_nif(seed):
    bs = stable_paths(seed); strings = ['RedsandBlockLightning_v3', 'RSBL_Alpha']
    for i in range(len(bs)): strings.extend((f'RSBL_P{i:03}', f'RSBL_G{i:03}', f'RSBL_S{i:03}'))
    def net(name): return P('IIi', strings.index(name), 0, -1)
    def av(name, origin=(0, 0, 0)): return net(name)+P('I3f9ffi', 14, *origin, *IDENTITY, 1., -1)
    blocks = []
    def node(name, origin, children):
        return av(name, origin)+P('I', len(children))+b''.join(P('i', x) for x in children)+P('I', 0)

    blocks.append(('NiNode', node(strings[0], (0, 0, 0), [2+3*i for i, b in enumerate(bs) if b['parent'] < 0])))
    blocks.append(('NiAlphaProperty', net('RSBL_Alpha')+P('HB', 4109, 0)))
    for i, branch in enumerate(bs):
        index = 2+3*i
        children = [index+1]+[2+3*j for j, b in enumerate(bs) if b['parent'] == i]
        blocks.append(('NiNode', node(f'RSBL_P{i:03}', branch['origin'], children)))
        verts, tris = branch['vertices'], branch['triangles']
        desc = (1059 << 44) | 6 | (16 << 6) | (20 << 22)
        shape = av(f'RSBL_G{i:03}')+P('4fiiiQHHI', 0., 0., 0., 700., -1, index+2, 1,
                                     desc, len(tris), len(verts), len(verts)*24+len(tris)*6)
        shape += b''.join(P('4f2e4B', *v) for v in verts)+b''.join(P('3H', *t) for t in tris)+P('I', 0)
        blocks.append(('BSTriShape', shape))
        texture = b'Textures/RedsandBlockLightning/Travel.dds'
        shader = net(f'RSBL_S{i:03}')+P('II4fI', (1 << 31) | (1 << 3) | (1 << 22), (1 << 5) | (1 << 4), 0., 0., 1., 1., len(texture))+texture



        shader += P('4B10fI', 0, 0, 0, 0, 1., 1., 1., 1., 1., 1., 1., 0., 14., 0., 0)
        blocks.append(('BSEffectShaderProperty', shader))
    types = list(dict.fromkeys(t for t, _ in blocks))
    def st(s): b = s.encode(); return P('I', len(b))+b
    data = b'Gamebryo File Format, Version 20.2.0.7\n'+P('IBIII', 0x14020007, 1, 12, len(blocks), 100)+b'\0\0\0'
    data += P('H', len(types))+b''.join(st(t) for t in types)
    data += b''.join(P('H', types.index(t)) for t, _ in blocks)+b''.join(P('I', len(b)) for _, b in blocks)
    data += P('II', len(strings), max(map(len, strings)))+b''.join(st(s) for s in strings)+P('I', 0)
    data += b''.join(b for _, b in blocks)+P('Ii', 1, 0)
    doc = DirectionalNif.from_bytes(data); doc.validate()

    for i in range(len(doc.blocks)): doc.fields(i)
    return data


def main():
    hashes = {}
    for variant in range(4):
        data = make_nif(90817+variant*7169)
        rel = f'Meshes/RedsandBlockLightning/Arc{variant}.nif'
        path = DEST/rel; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
        hashes[rel] = hashlib.sha256(data).hexdigest()

    header = P('7I', 124, 0x100f, 1, 1, 4, 0, 0)+bytes(44)
    header += P('8I', 32, 0x41, 0, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000)+P('5I', 0x1000, 0, 0, 0, 0)
    path = DEST/'Textures/RedsandBlockLightning/White.dds'; path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b'DDS '+header+bytes((255, 255, 255, 255)))


    def smooth(t): t = min(1., max(0., t)); return t*t*(3-2*t)
    pixels = bytearray()
    for i in range(256):
        u = i/255
        alpha = round(255*smooth(u/.065)*(1-smooth((u-.86)/.14)))
        pixels.extend((255, 255, 255, alpha))
    header = P('7I', 124, 0x100f, 1, 256, 1024, 0, 0)+bytes(44)
    header += P('8I', 32, 0x41, 0, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000)+P('5I', 0x1000, 0, 0, 0, 0)
    (path.parent/'Travel.dds').write_bytes(b'DDS '+header+bytes(pixels))
    print(json.dumps({'meshes': hashes, 'originalGeometry': True, 'branches': 60, 'stationaryMeshes': 60, 'coreOnly': True, 'screenSpace': False, 'depthTest': True}))


if __name__ == '__main__': main()
