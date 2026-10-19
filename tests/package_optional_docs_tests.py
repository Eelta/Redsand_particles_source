from pathlib import Path
import json
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]


class PackageOptionalDocsTests(unittest.TestCase):
    def test_code_checkout_packages_without_local_publication_documents(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / 'output'
            output.mkdir()
            (root / 'source').mkdir()
            (root / 'source/example.cpp').write_text('int value() { return 3; }\n', encoding='utf-8')
            (root / 'LICENSE').write_text('retained license\n', encoding='utf-8')
            original_readme = b'Existing repository README\n'
            (root / 'README.md').write_bytes(original_readme)
            runtime = output / 'runtime.zip'
            manifest = output / 'BUILD.json'
            manifest.write_text('{}', encoding='utf-8')
            binaries = {'SKSE/Plugins/Precision.dll': b'precision-fixture',
                        'SKSE/Plugins/MaxsuBlockSpark.dll': b'bridge-fixture'}
            with zipfile.ZipFile(runtime, 'w') as archive:
                for name, content in binaries.items():
                    archive.writestr(name, content)
            subprocess.run([sys.executable, '-B', str(ROOT / 'tools/package_public.py'),
                            '--root', str(root), '--archive', str(runtime), '--manifest', str(manifest)],
                           check=True, capture_output=True, text=True)
            self.assertEqual((root / 'README.md').read_bytes(), original_readme)
            with zipfile.ZipFile(runtime) as archive:
                for name, content in binaries.items():
                    self.assertEqual(archive.read(name), content)
                self.assertEqual(archive.read('Docs/RedsandParticles/README.md'), original_readme)
                self.assertIn('Docs/RedsandParticles/SOURCE.zip', archive.namelist())
                self.assertNotIn('Docs/RedsandParticles/COMPATIBILITY.md', archive.namelist())
            record = json.loads(manifest.read_text('utf-8'))
            self.assertTrue(record['publicationPackage'])
            self.assertFalse(record['donationPointsEnabled'])
            with zipfile.ZipFile(output / 'Redsand Particles - Corresponding Source.zip') as archive:
                self.assertIn('source/example.cpp', archive.namelist())
                self.assertEqual(archive.read('README.md'), original_readme)


if __name__ == '__main__':
    unittest.main()
