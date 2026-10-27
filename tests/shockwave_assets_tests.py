import argparse
import builtins
import math
import struct
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from build_shockwave import DURATION, make_nif, relative_paths
from nif_schema import DirectionalNif

ASSETS = ROOT / 'assets/ImpactVFX'


class ShockwaveAssetsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = make_nif()

    def test_all_runtime_models_reproduce_without_reading_asset_inputs(self):
        with patch.object(Path, 'read_bytes', side_effect=AssertionError('Unexpected asset read')), \
             patch.object(Path, 'read_text', side_effect=AssertionError('Unexpected source read')), \
             patch.object(builtins, 'open', side_effect=AssertionError('Unexpected file input')):
            self.assertEqual(make_nif(), self.data)
        paths = list(relative_paths())
        self.assertEqual(paths, ['Meshes/StellarImpact/Refraction/SoftRefraction.nif'])
        for relative in paths:
            with self.subTest(model=relative):
                self.assertEqual((ASSETS / relative).read_bytes(), self.data)

    def test_three_layers_have_valid_normals_bounds_and_bounded_geometry_cost(self):
        document = DirectionalNif.from_bytes(self.data)
        result = document.validate()
        self.assertEqual(len(document.blocks), 29)
        self.assertEqual(result['typed_blocks_validated'], 28)
        self.assertEqual(document.string(document.values(0)['name']), 'RSRGBRoot_v1')
        self.assertEqual(document.string(document.values(1)['name']), 'RedsandImpactScale')
        self.assertEqual(len(document.indices('NiBillboardNode')), 1)
        self.assertEqual(document.values(document.indices('NiBillboardNode')[0])['billboard_mode'], 3)
        self.assertEqual(struct.unpack('<II', document.blocks[28].data), (8, 529))
        visited = set()
        pending = list(document.roots)
        while pending:
            index = pending.pop()
            if index in visited:
                continue
            visited.add(index)
            if document.blocks[index].type == 'BSXFlags':
                continue
            for field in document.fields(index).values():
                if field.reference:
                    values = field.value if field.fmt.endswith('[]') else (field.value,)
                    pending.extend(value for value in values if value >= 0)
        self.assertEqual(visited, set(range(len(document.blocks))))
        vertex_count = triangle_count = 0
        shapes = document.indices('BSTriShape')
        self.assertEqual(len(shapes), 3)
        for index in shapes:
            geometry = document.values(index)
            vertices, triangles = geometry['vertices'], geometry['triangles']
            vertex_count += len(vertices)
            triangle_count += len(triangles)
            self.assertTrue(all(math.isfinite(value) for vertex in vertices for value in vertex))
            self.assertTrue(all(vertex[-4:] == (255, 255, 255, 255) for vertex in vertices))
            center, radius = geometry['bound'][:3], geometry['bound'][3]
            self.assertTrue(all(math.dist(vertex[:3], center) <= radius + .001 for vertex in vertices))
            for a, b, c in triangles:
                u = tuple(vertices[b][k] - vertices[a][k] for k in range(3))
                v = tuple(vertices[c][k] - vertices[a][k] for k in range(3))
                cross = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
                self.assertGreater(sum(value * value for value in cross), 1.e-8)
        self.assertLessEqual(vertex_count, 866)
        self.assertLessEqual(triangle_count, 1088)
        core = document.values(shapes[0])
        vertices = core['vertices']
        self.assertGreater(max(vertex[2] for vertex in vertices) - min(vertex[2] for vertex in vertices), 70.)
        self.assertGreater(max(vertex[0] for vertex in vertices) - min(vertex[0] for vertex in vertices), 200.)
        self.assertEqual(core['vertex_desc'] >> 44, 59)
        for vertex in vertices:
            normal = tuple(value / 127.5 - 1 for value in vertex[6:9])
            tangent = tuple(value / 127.5 - 1 for value in vertex[10:13])
            self.assertAlmostEqual(sum(value * value for value in normal), 1., delta=.02)
            self.assertAlmostEqual(sum(value * value for value in tangent), 1., delta=.02)
            self.assertAlmostEqual(sum(a * b for a, b in zip(normal, tangent)), 0., delta=.02)

    def test_real_refraction_and_emissive_rgb_fade_controllers_remain_active(self):
        document = DirectionalNif.from_bytes(self.data)
        core_index = document.indices('BSLightingShaderProperty')
        self.assertEqual(len(core_index), 1)
        core = document.values(core_index[0])
        self.assertTrue(core['shader_flags_1'] & (1 << 15))
        self.assertTrue(core['shader_flags_1'] & (1 << 16))
        self.assertTrue(core['shader_flags_1'] & (1 << 31))
        self.assertFalse(core['shader_flags_2'] & 1)
        self.assertEqual(core['alpha'], 0.)
        self.assertEqual(core['refraction_strength'], 0.)
        self.assertEqual(core['uv_scale'], (2., 2.))
        controls = {}
        index = core['controller']
        while index >= 0:
            control = document.values(index)
            self.assertEqual(control['target'], core_index[0])
            controls[control['controlled_variable']] = document.values(document.values(control['interpolator'])['data'])['keys']
            index = control['next_controller']
        self.assertEqual(set(controls), {0, 12})
        self.assertAlmostEqual(max(value for _, value in controls[0]), .28, places=6)
        for keys in controls.values():
            self.assertEqual(keys[0], (0., 0.))
            self.assertEqual(keys[-1][1], 0.)
            self.assertAlmostEqual(keys[-1][0], DURATION)
            peak = max(range(len(keys)), key=lambda index: keys[index][1])
            self.assertTrue(all(b[1] <= a[1] for a, b in zip(keys[peak:], keys[peak + 1:])))
        shaders = document.indices('BSEffectShaderProperty')
        self.assertEqual(len(shaders), 2)
        for index in shaders:
            shader = document.values(index)
            self.assertEqual(document.string(shader['name']), 'RSRGBv1_6')
            self.assertTrue(shader['shader_flags_1'] & (1 << 22))
            self.assertTrue(shader['shader_flags_1'] & (1 << 31))
            self.assertFalse(shader['shader_flags_2'] & 1)
            self.assertEqual(shader['base_color'][3], 0.)
            self.assertAlmostEqual(shader['base_color_scale'], 1.25)
            control = document.values(shader['controller'])
            self.assertEqual(control['target'], index)
            self.assertEqual(control['controlled_variable'], 5)
            keys = document.values(document.values(control['interpolator'])['data'])['keys']
            self.assertEqual(keys[0], (0., 0.))
            self.assertEqual(keys[-1][1], 0.)
            self.assertGreater(max(value for _, value in keys), .1)
        for index in document.indices('NiTransformController') + document.indices('BSLightingShaderPropertyFloatController') + document.indices('BSEffectShaderPropertyFloatController'):
            controller = document.values(index)
            self.assertTrue(controller['flags'] & 8)
            self.assertEqual(controller['start_time'], 0.)
            self.assertAlmostEqual(controller['stop_time'], DURATION)
        expansion = document.values(document.values(document.values(3)['interpolator'])['data'])
        keys = expansion['scale_keys']
        self.assertAlmostEqual(keys[0][1], .08)
        self.assertAlmostEqual(keys[-1][1], 1.55)
        self.assertTrue(all(b[1] >= a[1] for a, b in zip(keys, keys[1:])))
        self.assertEqual(set(document.texture_paths()), {
            'textures\\StellarImpact\\ArtRework\\CoronaRays.dds',
            'textures\\effects\\DarkSwirlsInvert_n.dds',
            'textures\\effects\\VaporTileNormal_n.dds'})


if __name__ == '__main__':
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--assets', type=Path, default=ASSETS)
    args, rest = parser.parse_known_args()
    ASSETS = args.assets
    unittest.main(argv=[sys.argv[0], *rest])
