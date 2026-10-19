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
            game_files = {**binaries, 'Meshes/Test/effect.nif': b'model',
                          'Textures/Test/effect.dds': b'texture', 'SKSE/Plugins/Test.ini': b'[Test]\n',
                          'SKSE/Plugins/Routes.toml': b'schemaVersion = 1\n',
                          'SKSE/CoreImpactFramework/Test.json': b'{}'}
            noise = {'Docs/RedsandParticles/SOURCE.zip': b'old-source',
                     'Docs/RedsandParticles/LICENSE.txt': b'license',
                     'Docs/SourceAssets/image.png': b'source-image',
                     'README.md': b'old-readme', 'certificate.pem': b'certificate',
                     'SKSE/Plugins/NOTICE.txt': b'notice'}
            with zipfile.ZipFile(runtime, 'w') as archive:
                for name, content in {**game_files, **noise}.items():
                    archive.writestr(name, content)
            subprocess.run([sys.executable, '-B', str(ROOT / 'tools/package_public.py'),
                            '--root', str(root), '--archive', str(runtime), '--manifest', str(manifest)],
                           check=True, capture_output=True, text=True)
            self.assertEqual((root / 'README.md').read_bytes(), original_readme)
            with zipfile.ZipFile(runtime) as archive:
                for name, content in binaries.items():
                    self.assertEqual(archive.read(name), content)
                self.assertEqual(set(archive.namelist()), set(game_files))
                for name, content in game_files.items():
                    self.assertEqual(archive.read(name), content)
            record = json.loads(manifest.read_text('utf-8'))
            self.assertTrue(record['publicationPackage'])
            self.assertFalse(record['donationPointsEnabled'])
            with zipfile.ZipFile(output / 'BuildData/Sources/Redsand Particles - Corresponding Source.zip') as archive:
                self.assertIn('source/example.cpp', archive.namelist())
                self.assertEqual(archive.read('README.md'), original_readme)
                self.assertEqual(archive.read('LICENSE'), (root / 'LICENSE').read_bytes())

    def test_independent_hellblade_keeps_only_runtime_in_installer(self):
        with tempfile.TemporaryDirectory() as temporary:
            project = Path(temporary) / 'project'
            root = project / 'HellbladePlayerTime'
            root.mkdir(parents=True)
            shutil.copy2(ROOT / 'HellbladePlayerTime/package_independent.py', root / 'package_independent.py')
            (project / 'LICENSE').write_text('project license', encoding='utf-8')
            (project / 'licenses').mkdir()
            (project / 'licenses/TDM-COPYING.txt').write_text('api license', encoding='utf-8')
            (project / 'cmake').mkdir()
            (project / 'cmake/EmbedNotices.cmake').write_text('function(redsand_embed_notices)\nendfunction()\n', encoding='utf-8')
            for folder in ('source', 'tests', 'assets/SKSE/Plugins'):
                (root / folder).mkdir(parents=True)
            (root / 'source/example.cpp').write_text('int value() { return 3; }', encoding='utf-8')
            (root / 'assets/SKSE/Plugins/HellbladePlayerTime.ini').write_bytes(b'[Attack]\nEnabled=false\n')
            for name in ('CMakeLists.txt', 'baseline.json', 'upstream-project.json', 'BUILDING.md'):
                (root / name).write_text('fixture', encoding='utf-8')
            build = project / 'native/build'
            (build / 'Release').mkdir(parents=True)
            (build / 'Release/HellbladeTimedBlock.dll').write_bytes(b'hellblade-fixture')
            (build / 'Release/HellbladeTimedBlock.pdb').write_bytes(b'symbols-fixture')
            (build.parent / 'hellblade-tests.log').write_text('100% tests passed', encoding='utf-8')
            prepared = project / 'prepared'
            (prepared / 'CommonLibSSE-NG').mkdir(parents=True)
            for name in ('COPYING', 'EXCEPTIONS'):
                (prepared / 'CommonLibSSE-NG' / name).write_text('retained license', encoding='utf-8')
            decoder = prepared / 'MinHook'
            (decoder / 'src/hde').mkdir(parents=True)
            (decoder / 'src/hde/hde64.c').write_text('int decoder(void) { return 1; }', encoding='utf-8')
            (decoder / 'LICENSE.txt').write_text('retained decoder license', encoding='utf-8')
            precision = prepared / 'Precision'
            precision.mkdir()
            (precision / 'vcpkg.json').write_text('{}', encoding='utf-8')
            for name in ('directxmath', 'directxtk', 'fmt', 'rapidcsv', 'rsm-binary-io',
                         'simpleini', 'spdlog', 'tomlplusplus', 'xbyak'):
                dependency = prepared / 'vcpkg/buildtrees' / name / 'src/fixture.clean'
                dependency.mkdir(parents=True)
                (dependency / 'example.hpp').write_text('int value();', encoding='utf-8')
                share = precision / 'build-release/vcpkg_installed/x64-windows-static-md/share' / name
                share.mkdir(parents=True)
                (share / 'copyright').write_text('retained copyright', encoding='utf-8')
            publication = project / 'output/Publication'
            subprocess.run([sys.executable, '-B', str(root / 'package_independent.py'),
                            '--build', str(build), '--prepared', str(prepared),
                            '--work', str(project / 'work'), '--output', str(publication)],
                           check=True, capture_output=True, text=True)
            with zipfile.ZipFile(publication / 'Hellblade - Player Time Independence.zip') as archive:
                self.assertEqual(set(archive.namelist()), {'SKSE/Plugins/HellbladeTimedBlock.dll',
                                                         'SKSE/Plugins/HellbladePlayerTime.ini'})
                self.assertEqual(archive.read('SKSE/Plugins/HellbladeTimedBlock.dll'), b'hellblade-fixture')
            self.assertEqual([p.name for p in publication.iterdir()], ['Hellblade - Player Time Independence.zip'])
            with zipfile.ZipFile(project / 'output/BuildData/Sources/Hellblade - Corresponding Source.zip') as archive:
                self.assertIn('Documentation/CREDITS.txt', archive.namelist())
                self.assertIn('Documentation/Licenses/fmt-copyright.txt', archive.namelist())
                self.assertIn('HellbladePlayerTime/source/example.cpp', archive.namelist())
                self.assertEqual(archive.read('licenses/TDM-COPYING.txt'), b'api license')
                self.assertIn('cmake/EmbedNotices.cmake', archive.namelist())
                self.assertIn('Dependencies/MinHook/src/hde/hde64.c', archive.namelist())
                self.assertEqual(archive.read('Dependencies/MinHook/LICENSE.txt'), b'retained decoder license')

    def test_runtime_policy_rejects_unsafe_paths_and_case_collisions(self):
        sys.path.insert(0, str(ROOT / 'tools'))
        from runtime_payload import is_runtime_file, runtime_payload
        for name in ('../Meshes/file.nif', '/Meshes/file.nif', 'C:/file.nif', r'Meshes\file.nif'):
            with self.assertRaises(ValueError):
                is_runtime_file(name)
        with self.assertRaises(ValueError):
            runtime_payload({'Meshes/File.nif': b'a', 'Meshes/file.nif': b'b'})


if __name__ == '__main__':
    unittest.main()
