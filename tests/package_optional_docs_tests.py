from pathlib import Path
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]


class PackageOptionalDocsTests(unittest.TestCase):
    def test_code_checkout_packages_without_local_publication_documents(self):
        for private_project_present in (False, True):
            with self.subTest(private_project_present=private_project_present), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                output = root / 'output'
                output.mkdir()
                (root / 'source').mkdir()
                (root / 'source/example.cpp').write_text('int value() { return 3; }\n', encoding='utf-8')
                (root / 'LICENSE').write_text('retained license\n', encoding='utf-8')
                original_readme = b'Existing repository README\n'
                (root / 'README.md').write_bytes(original_readme)
                private_files = {
                    'HellbladePlayerTime/source/private.cpp': b'int private_value() { return 7; }\n',
                    'HellbladePlayerTime/assets/SKSE/Plugins/HellbladePlayerTime.ini': b'private settings',
                    'HellbladePlayerTime/package_independent.py': b'private packaging script',
                    'compat/maxsu/src/Events.cpp': b'private bridge source',
                    'compat/vendor/MaxsuBlockSpark-ng/src/main.cpp': b'retired upstream source',
                    'compat/vendor/DKUtil-Maxsu/include/DKUtil.h': b'retired dependency source',
                } if private_project_present else {}
                for relative, content in private_files.items():
                    path = root / relative
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(content)
                runtime = output / 'runtime.zip'
                manifest = output / 'BUILD.json'
                manifest.write_text('{}', encoding='utf-8')
                (root / 'SOURCE-PROVENANCE.json').write_text(json.dumps({'current_maxsu_dll_sha256': 'obsolete'}), encoding='utf-8')
                binaries = {'SKSE/Plugins/Precision.dll': b'precision-fixture'}
                game_files = {**binaries, 'Meshes/StellarImpact/effect.nif': b'model',
                              'Textures/StellarImpact/effect.dds': b'texture', 'SKSE/Plugins/RedsandParticles.ini': b'[Test]\n',
                              'SKSE/Plugins/RedsandImpactVFX.toml': b'schemaVersion = 1\n'}
                noise = {'Docs/RedsandParticles/SOURCE.zip': b'old-source',
                         'Docs/RedsandParticles/LICENSE.txt': b'license',
                         'Docs/SourceAssets/image.png': b'source-image',
                         'README.md': b'old-readme', 'certificate.pem': b'certificate',
                         'SKSE/Plugins/NOTICE.txt': b'notice',
                         'SKSE/Plugins/MaxsuBlockSpark.dll': b'bridge-fixture',
                         'SKSE/Plugins/MuImpactFramework/blocking_sword_impact.ini': b'old-rule',
                         'SKSE/CoreImpactFramework/StellarImpact.json': b'{}',
                         'Meshes/ValhallaCombat/SparksFlareRings.nif': b'old-model',
                         'Textures/effects/Scholar Keriak/SKK_HitStar.dds': b'old-texture'}
                with zipfile.ZipFile(runtime, 'w') as archive:
                    for name, content in {**game_files, **noise}.items():
                        archive.writestr(name, content)
                subprocess.run([sys.executable, '-B', str(ROOT / 'tools/package_public.py'),
                                '--root', str(root), '--archive', str(runtime), '--manifest', str(manifest)],
                               check=True, capture_output=True, text=True)
                self.assertEqual((root / 'README.md').read_bytes(), original_readme)
                for relative, content in private_files.items():
                    self.assertEqual((root / relative).read_bytes(), content)
                with zipfile.ZipFile(runtime) as archive:
                    self.assertEqual(set(archive.namelist()), set(game_files))
                    for name, content in game_files.items():
                        self.assertEqual(archive.read(name), content)
                record = json.loads(manifest.read_text('utf-8'))
                self.assertTrue(record['publicationPackage'])
                self.assertFalse(record['donationPointsEnabled'])
                self.assertNotIn('current_maxsu_dll_sha256', json.loads((root / 'SOURCE-PROVENANCE.json').read_text('utf-8')))
                with zipfile.ZipFile(output / 'BuildData/Sources/Redsand Particles - Corresponding Source.zip') as archive:
                    self.assertIn('source/example.cpp', archive.namelist())
                    self.assertEqual(archive.read('README.md'), original_readme)
                    self.assertEqual(archive.read('LICENSE'), (root / 'LICENSE').read_bytes())
                    self.assertEqual(len(archive.namelist()), record['correspondingSourceFiles'])
                    self.assertFalse(any(Path(name).parts[0].casefold() == 'hellbladeplayertime'
                                         for name in archive.namelist()))
                    self.assertFalse(any(name.startswith(('compat/maxsu/', 'compat/vendor/MaxsuBlockSpark-ng/',
                                                          'compat/vendor/DKUtil-Maxsu/')) for name in archive.namelist()))

    def test_public_packaging_tests_run_in_checkout_without_private_project(self):
        with tempfile.TemporaryDirectory() as temporary:
            checkout = Path(temporary)
            for relative in ('tools/package_public.py', 'tools/runtime_payload.py',
                             'tests/package_optional_docs_tests.py'):
                path = checkout / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(ROOT / relative, path)
            self.assertFalse((checkout / 'HellbladePlayerTime').exists())
            result = subprocess.run([
                sys.executable, '-B', str(checkout / 'tests/package_optional_docs_tests.py'),
                'PackageOptionalDocsTests.test_code_checkout_packages_without_local_publication_documents',
                'PackageOptionalDocsTests.test_runtime_policy_rejects_unsafe_paths_and_case_collisions',
            ], cwd=checkout, check=True, capture_output=True, text=True)
            self.assertIn('Ran 2 tests', result.stderr)
            self.assertIn('OK', result.stderr)
            self.assertFalse((checkout / 'HellbladePlayerTime').exists())

    def test_runtime_policy_rejects_unsafe_paths_and_case_collisions(self):
        sys.path.insert(0, str(ROOT / 'tools'))
        from runtime_payload import is_runtime_file, runtime_payload
        for name in ('../Meshes/file.nif', '/Meshes/file.nif', 'C:/file.nif', r'Meshes\file.nif'):
            with self.assertRaises(ValueError):
                is_runtime_file(name)
        with self.assertRaises(ValueError):
            runtime_payload({'Meshes/StellarImpact/File.nif': b'a', 'Meshes/StellarImpact/file.nif': b'b'})


if __name__ == '__main__':
    unittest.main()
