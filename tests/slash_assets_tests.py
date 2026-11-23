from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from optimize_slash_assets import closure, compact
from prune_impact_layers import ImpactNif, graph_descendants, node_name


MESHES = ROOT / 'assets/ImpactVFX/Meshes/StellarImpact'


class ImpactAssetsTests(unittest.TestCase):
    def test_cut_keeps_one_pointed_shape_and_its_fade(self):
        doc = ImpactNif.read(MESHES / 'Slash/GoldBladeSlash.nif')
        self.assertEqual(len(closure(doc)[0]), len(doc.blocks))
        self.assertFalse(any('PSys' in block.type for block in doc.blocks))
        shape, = doc.indices('BSTriShape')
        geometry = doc.values(shape)
        self.assertEqual(node_name(doc, shape), 'White hot blade contact')
        self.assertIn('RSRGBCutLength_v1', {node_name(doc, i) for i in graph_descendants(doc, doc.roots)})
        self.assertEqual(doc.values(geometry['shader'])['name'], doc.strings.index('RSRGBv1_4'))
        self.assertGreaterEqual(doc.values(geometry['shader'])['controller'], 0)
        vertices = geometry['vertices']
        for x in (min(v[0] for v in vertices), max(v[0] for v in vertices)):
            self.assertTrue(all(v[1:3] == (0., 0.) for v in vertices if v[0] == x))
        self.assertTrue(any(v[0] == 0 and v[1] != 0 for v in vertices))

    def test_hit_and_block_sparks_have_no_round_layers(self):
        meshes = [*(MESHES / 'Hit').glob('*.nif'), *(MESHES / 'Block').glob('*.nif')]
        for path in sorted(meshes):
            with self.subTest(model=path.name):
                doc = ImpactNif.read(path)
                self.assertEqual(len(closure(doc)[0]), len(doc.blocks))
                names = {node_name(doc, i).lower() for i in graph_descendants(doc, doc.roots)}
                self.assertFalse(any('corona' in name or name in {'torus01', 'circle01', 'flash02', 'penblight'} for name in names))
                self.assertTrue(doc.indices('NiParticleSystem'))
                self.assertFalse(any('corona' in texture.lower() for texture in doc.texture_paths()))

    def test_compaction_rejects_unknown_or_dangling_data(self):
        path = MESHES / 'Slash/GoldBladeSlash.nif'
        unknown = ImpactNif.read(path)
        unknown.blocks[-1].type = 'UnknownPotentialReferenceBlock'
        with self.assertRaises(NotImplementedError):
            compact(unknown)
        bad = ImpactNif.read(path)
        root = bad.blocks[bad.roots[0]]
        struct.pack_into('<i', root.data, bad.fields(root.index)['children'].offset, len(bad.blocks))
        with self.assertRaises(ValueError):
            compact(bad)


if __name__ == '__main__':
    unittest.main()
