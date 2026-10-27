from pathlib import Path
import sys
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from nif_schema import DirectionalNif


class SharedImpactAssets(unittest.TestCase):
    def test_only_shared_models_remain_and_legacy_paths_are_not_referenced(self):
        meshes = ROOT / 'assets/ImpactVFX/Meshes/StellarImpact'
        self.assertFalse((meshes / 'Runtime').exists(), 'Retired preset copies must not return to the source asset tree')
        manifest = tomllib.loads((ROOT / 'assets/ImpactVFX/SKSE/Plugins/RedsandImpactVFX.toml').read_text('utf-8-sig'))
        for route in manifest['Native'] + manifest['Route']:
            paths = [route['model']] if 'model' in route else [route['baseModel']] + route['models'] + ([route['offModel']] if route.get('offModel') else [])
            for path in paths:
                self.assertNotIn('runtime', path.lower())
                self.assertNotIn('AzureCrescentSlash', path)
                self.assertTrue((meshes.parent / path.replace('\\', '/')).is_file(), path)
            if 'models' in route:
                self.assertEqual(len(route['models']), 64)
                self.assertEqual(set(route['models']), {route['baseModel']})

    def test_five_distinct_block_resources_and_legacy_ring_alias(self):
        root = ROOT / 'assets/ImpactVFX/Meshes/StellarImpact/Block'
        meshes = list(root.glob('*.nif'))
        self.assertEqual(len({p.read_bytes() for p in meshes}), 5)
        self.assertEqual((root / 'PerfectRing.nif').read_bytes(), (root / 'TimedFlare.nif').read_bytes())
        for mesh in meshes:
            DirectionalNif.read(mesh)


if __name__ == '__main__':
    unittest.main()
