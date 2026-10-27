import argparse
import hashlib
import json
import math
import struct
from pathlib import Path

from nif_schema import DirectionalNif


IDENTITY = (1., 0., 0., 0., 1., 0., 0., 0., 1.)
DURATION = .85
RADIUS = 103.72
HALF_DEPTH = 37.82
SEGMENTS = 48
LATITUDES = 6
SCALE_KEYS = ((0., .08), (.035, .36), (.12, .72), (.25, 1.05),
              (.43, 1.32), (.64, 1.55), (.85, 1.55))
DISTORTION_KEYS = ((0., 0.), (.025, .28), (.07, .24), (.16, .15),
                   (.29, .065), (.43, .015), (.56, .002), (.64, 0.), (.85, 0.))
CORE_ALPHA_KEYS = ((0., 0.), (.025, .65), (.10, .48), (.24, .22),
                   (.40, .065), (.56, .008), (.64, 0.), (.85, 0.))
RIM_ALPHA_KEYS = ((0., 0.), (.035, .26), (.10, .21), (.22, .12),
                  (.38, .045), (.54, .009), (.68, 0.), (.85, 0.))
ECHO_ALPHA_KEYS = ((0., 0.), (.09, 0.), (.15, .14), (.26, .084),
                   (.42, .0266), (.60, .0028), (.70, 0.), (.85, 0.))


def pack(fmt, *values):
    return struct.pack('<' + fmt, *values)


def normalized(vector):
    length = math.sqrt(sum(value * value for value in vector))
    return tuple(value / length for value in vector)


def normal_byte(value):
    return round(127.5 * (1. + max(-1., min(1., value))))


def shell():
    vertices = []
    triangles = []
    for latitude in range(LATITUDES + 1):
        angle = -math.pi / 2 + math.pi * latitude / LATITUDES
        for segment in range(SEGMENTS + 1):
            azimuth = math.tau * segment / SEGMENTS
            cosine, sine = math.cos(azimuth), math.sin(azimuth)
            x = RADIUS * math.cos(angle) * cosine
            y = RADIUS * math.cos(angle) * sine
            z = HALF_DEPTH * math.sin(angle)
            normal = normalized((x / RADIUS**2, y / RADIUS**2, z / HALF_DEPTH**2))
            tangent = (-sine, cosine, 0.)
            bitangent = (normal[1] * tangent[2] - normal[2] * tangent[1],
                         normal[2] * tangent[0] - normal[0] * tangent[2],
                         normal[0] * tangent[1] - normal[1] * tangent[0])
            vertices.append((x, y, z, bitangent[0], segment / SEGMENTS, latitude / LATITUDES,
                             *(normal_byte(value) for value in (*normal, bitangent[1])),
                             *(normal_byte(value) for value in (*tangent, bitangent[2])),
                             255, 255, 255, 255))
            if latitude and segment:
                a = (latitude - 1) * (SEGMENTS + 1) + segment - 1
                b = a + 1
                c = a + SEGMENTS + 1
                d = c + 1
                if latitude > 1:
                    triangles.append((a, b, c))
                if latitude < LATITUDES:
                    triangles.append((c, b, d))
    return vertices, triangles


def ring(inner, outer):
    vertices = []
    triangles = []
    for segment in range(129):
        angle = math.tau * segment / 128
        for radius, v in ((inner, 0.), (outer, 1.)):
            vertices.append((radius * math.cos(angle), radius * math.sin(angle), 8., 0,
                             segment / 128, v, 255, 255, 255, 255))
        if segment:
            a = (segment - 1) * 2
            triangles.extend(((a, a + 1, a + 2), (a + 1, a + 3, a + 2)))
    return vertices, triangles


def relative_paths():
    yield 'Meshes/StellarImpact/Refraction/SoftRefraction.nif'


def make_nif():
    strings = ['RSRGBRoot_v1', 'RedsandImpactScale', 'RedsandWaveExpansion',
               'RedsandWaveCore', 'RedsandWaveRim', 'RedsandWaveEcho', 'RSRGBv1_6',
               'RedsandWaveFacing', 'BSX']

    def net(name, controller=-1, extras=()):
        return pack('II', strings.index(name), len(extras)) + b''.join(pack('i', value) for value in extras) + pack('i', controller)

    def av(name, controller=-1, extras=(), translation=(0., 0., 0.)):
        return net(name, controller, extras) + pack('I3f9ffi', 14, *translation, *IDENTITY, 1., -1)

    def node(name, children, controller=-1, extras=()):
        return av(name, controller, extras) + pack('I', len(children)) + b''.join(pack('i', value) for value in children) + pack('I', 0)

    def time(target, interpolator, variable=None, next_controller=-1):
        result = pack('iH4fii', next_controller, 77, 1., 0., 0., DURATION, target, interpolator)
        return result if variable is None else result + pack('I', variable)

    def float_data(keys):
        return pack('II', len(keys), 1) + b''.join(pack('2f', *value) for value in keys)

    def shape(name, shader, alpha, vertices, triangles, lighting=False):
        if lighting:
            descriptor = (59 << 44) | 8 | (4 << 8) | (5 << 16) | (6 << 20) | (7 << 24)
            vertex_format = '4f2e12B'
            stride = 32
            translation = (0., 0., 8.)
            bound = (0., 0., 0., RADIUS + .01)
        else:
            descriptor = (35 << 44) | 6 | (4 << 8) | (5 << 24)
            vertex_format = '3fI2e4B'
            stride = 24
            translation = (0., 0., 0.)
            bound = (0., 0., 8., max(math.hypot(row[0], row[1]) for row in vertices) + .01)
        data = av(name, translation=translation) + pack('4fiiiQHHI', *bound, -1, shader, alpha,
                descriptor, len(triangles), len(vertices), len(vertices) * stride + len(triangles) * 6)
        data += b''.join(pack(vertex_format, *value) for value in vertices)
        return data + b''.join(pack('3H', *value) for value in triangles) + pack('I', 0)

    def effect_shader(controller):
        texture = b'textures\\StellarImpact\\ArtRework\\CoronaRays.dds'
        result = net('RSRGBv1_6', controller) + pack('II4fI', (1 << 31) | (1 << 22) | (1 << 3),
                 (1 << 5) | (1 << 4), 0., 0., 1., 1., len(texture)) + texture
        return result + pack('4B10fI', 0, 0, 0, 0, 0., 1., 1., 1., 1., .84, .45, 0., 1.25, 0., 0)

    core_vertices, core_triangles = shell()
    rim_vertices, rim_triangles = ring(82., 108.)
    echo_vertices, echo_triangles = ring(60., 86.)
    empty = -3.4028234663852886e38
    lighting_flags = (1 << 31) | (1 << 15) | (1 << 16) | (1 << 3)
    lighting = pack('I', 0) + pack('IIi', 0xffffffff, 0, 8)
    lighting += pack('II4fi3ffI7f', lighting_flags, (1 << 5) | (1 << 4), 0., 0., 2., 2., 11,
                     0., 0., 0., 1., 3, 0., 0., 80., 1., 1., 1., 1.)
    lighting += pack('2f', .3, 2.)
    textures = ('textures\\effects\\DarkSwirlsInvert_n.dds', 'textures\\effects\\VaporTileNormal_n.dds', '', '', '', '', '', '', '')
    texture_set = pack('I', len(textures))
    for path in textures:
        encoded = path.encode('utf-8')
        texture_set += pack('I', len(encoded)) + encoded
    blocks = [
        ('NiNode', node('RSRGBRoot_v1', (1,), extras=(28,))),
        ('NiNode', node('RedsandImpactScale', (27,))),
        ('NiNode', node('RedsandWaveExpansion', (6, 15, 21), controller=3)),
        ('NiTransformController', time(2, 4)),
        ('NiTransformInterpolator', pack('8fi', *([empty] * 8), 5)),
        ('NiTransformData', pack('IIII', 0, 0, len(SCALE_KEYS), 1) + b''.join(pack('2f', *key) for key in SCALE_KEYS)),
        ('BSTriShape', shape('RedsandWaveCore', 7, -1, core_vertices, core_triangles, True)),
        ('BSLightingShaderProperty', lighting),
        ('BSLightingShaderPropertyFloatController', time(7, 9, 0, 12)),
        ('NiFloatInterpolator', pack('fi', empty, 10)),
        ('NiFloatData', float_data(DISTORTION_KEYS)),
        ('BSShaderTextureSet', texture_set),
        ('BSLightingShaderPropertyFloatController', time(7, 13, 12)),
        ('NiFloatInterpolator', pack('fi', 0., 14)),
        ('NiFloatData', float_data(CORE_ALPHA_KEYS)),
        ('BSTriShape', shape('RedsandWaveRim', 16, 17, rim_vertices, rim_triangles)),
        ('BSEffectShaderProperty', effect_shader(18)),
        ('NiAlphaProperty', pack('IIiHB', 0xffffffff, 0, -1, 4109, 0)),
        ('BSEffectShaderPropertyFloatController', time(16, 19, 5)),
        ('NiFloatInterpolator', pack('fi', empty, 20)),
        ('NiFloatData', float_data(RIM_ALPHA_KEYS)),
        ('BSTriShape', shape('RedsandWaveEcho', 22, 23, echo_vertices, echo_triangles)),
        ('BSEffectShaderProperty', effect_shader(24)),
        ('NiAlphaProperty', pack('IIiHB', 0xffffffff, 0, -1, 4109, 0)),
        ('BSEffectShaderPropertyFloatController', time(22, 25, 5)),
        ('NiFloatInterpolator', pack('fi', empty, 26)),
        ('NiFloatData', float_data(ECHO_ALPHA_KEYS)),
        ('NiBillboardNode', node('RedsandWaveFacing', (2,)) + pack('H', 3)),
        ('BSXFlags', pack('II', strings.index('BSX'), 529)),
    ]
    types = list(dict.fromkeys(name for name, _ in blocks))

    def string(value):
        encoded = value.encode('utf-8')
        return pack('I', len(encoded)) + encoded

    result = b'Gamebryo File Format, Version 20.2.0.7\n' + pack('IBIII', 0x14020007, 1, 12, len(blocks), 100) + b'\0\0\0'
    result += pack('H', len(types)) + b''.join(string(name) for name in types)
    result += b''.join(pack('H', types.index(name)) for name, _ in blocks)
    result += b''.join(pack('I', len(data)) for _, data in blocks)
    result += pack('II', len(strings), max(map(len, strings))) + b''.join(string(value) for value in strings) + pack('I', 0)
    result += b''.join(data for _, data in blocks) + pack('Ii', 1, 0)
    document = DirectionalNif.from_bytes(result)
    document.validate()
    for block in document.blocks:
        if block.type != 'BSXFlags':
            document.fields(block.index)
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    data = make_nif()
    paths = list(relative_paths())
    for relative in paths:
        path = args.output / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    print(json.dumps({'output': str(args.output.resolve()), 'files': len(paths),
                      'sha256': hashlib.sha256(data).hexdigest(), 'bytes_per_model': len(data),
                      'source_assets_read': False, 'geometry_layers': 3,
                      'duration': DURATION, 'external_requirements_added': []}))


if __name__ == '__main__':
    main()
