from pathlib import Path
import hashlib
import json
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from generate_legacy_slash_masks import generate, THREADS_SOURCE

ASSETS = ROOT / 'assets/ImpactVFX/Textures/StellarImpact/Slash'
EXPECTED = {
    'Crescent': '9a67c0fe40c5ff527ba4de6b4a488dc9e198ad5020c2914981f35c3eb3c7289e',
    'RedTrace': '8f0046cc405f46931be74726c34450c2c8a23d9da42b9faca9fc003594cf382a',
    'GoldSweepMask': '82c276c6290c1c2f3a263ed48eb36e8eaeff93a3317dbeebb9624f2add29d0fb',
    'GoldCutThreads': 'df3a5ff7724f225b7e3f6e631c34dd28210015bc65121dd3e4e22c0370bc7ef3',
}


class LegacyMaskSourcesTests(unittest.TestCase):
    def test_original_sources_reproduce_all_runtime_dds_without_reading_them(self):
        original_read = Path.read_bytes

        def source_reads_only(path):
            if path.parent.resolve() == ASSETS.resolve():
                raise AssertionError('Generator read a runtime DDS as source')
            return original_read(path)

        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary)
            with mock.patch.object(Path, 'read_bytes', autospec=True, side_effect=source_reads_only):
                self.assertEqual(generate(destination), EXPECTED)
            self.assertEqual({p.name for p in destination.iterdir()}, {name + '.dds' for name in EXPECTED})
            for name in EXPECTED:
                self.assertEqual((destination / (name + '.dds')).read_bytes(), (ASSETS / (name + '.dds')).read_bytes())

    def test_external_texture_source_and_fixed_cc0_origin(self):
        origin = json.loads((ROOT / 'resources/SourceTextures/effekseer-origin.json').read_text(encoding='utf-8'))
        self.assertEqual(hashlib.sha256(THREADS_SOURCE.read_bytes()).hexdigest(), origin['sha256'])
        self.assertEqual(origin['license'], 'CC0-1.0')
        self.assertIn('/8bf8edfedb51ac09af3d4cf788cb81746ea75a82/', origin['source_url'])
        self.assertIn('/8bf8edfedb51ac09af3d4cf788cb81746ea75a82/', origin['license_url'])
        with tempfile.TemporaryDirectory() as temporary:
            invalid = Path(temporary) / 'invalid.png'
            invalid.write_bytes(b'not the fixed upstream source')
            with self.assertRaisesRegex(ValueError, 'Unexpected source texture'):
                generate(Path(temporary) / 'output', invalid)


if __name__ == '__main__':
    unittest.main()
