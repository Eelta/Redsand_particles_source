
from pathlib import Path
import copy
import json
import math
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from optimize_slash_assets import SlashNif, closure, compact, graph_signature, SECONDARY, optimize


class SlashAssetsTests(unittest.TestCase):
    def test_shared_model_preserves_artwork_motion_fade_and_core_emission(self):
        golden = json.loads((ROOT / 'tests/fixtures/slash-pre-optimization-graphs.json').read_text('utf-8'))
        assets = ROOT / 'assets/ImpactVFX/Meshes/StellarImpact'
        current = {p.relative_to(assets).as_posix(): p for p in assets.rglob('GoldBladeSlash.nif')}
        self.assertEqual(set(current), set(golden))
        self.assertEqual(len(current), 1)
        for name, path in current.items():
            with self.subTest(model=name):
                doc = SlashNif.read(path)
                doc.validate()
                self.assertEqual(len(doc.blocks), 510)
                self.assertEqual(len(closure(doc)[0]), 510)
                restored = copy.deepcopy(doc)
                changed = []
                for index in doc.indices('NiParticleSystem'):
                    fields = doc.fields(index)
                    system = doc.string(fields['name'].value)
                    if system not in SECONDARY:
                        continue
                    changed.append(system)
                    controller = fields['controller'].value
                    while controller != -1:
                        control = doc.fields(controller)
                        if doc.blocks[controller].type == 'NiPSysEmitterCtlr':
                            curve = doc.fields(control['interpolator'].value)['data'].value
                            rate = doc.fields(curve)['keys']
                            for key, (time, value) in enumerate(rate.value):
                                struct.pack_into('<f', restored.blocks[curve].data, rate.offset + key * 8 + 4, value * 2.)
                            births = sum((b[0]-a[0])*(b[1]+a[1])*.5 for a,b in zip(rate.value, rate.value[1:])) * 100.
                        controller = control['next_controller'].value
                    data = fields['data'].value
                    capacity = doc.fields(data)['max_particles']
                    self.assertGreaterEqual(capacity.value, math.ceil(births) + 2)
                    struct.pack_into('<H', restored.blocks[data].data, capacity.offset, capacity.value * 2)
                self.assertEqual(len(changed), 6)




                self.assertEqual(graph_signature(restored), golden[name])
                with self.assertRaises(ValueError):
                    optimize(doc)

    def test_unknown_unreachable_data_and_dangling_references_fail_closed(self):
        doc = SlashNif.read(next((ROOT / 'assets/ImpactVFX').rglob('GoldBladeSlash.nif')))
        unknown = copy.deepcopy(doc)
        unknown.blocks[-1].type = 'UnknownPotentialReferenceBlock'
        with self.assertRaises(NotImplementedError):
            compact(unknown)
        bad = copy.deepcopy(doc)
        root = bad.blocks[bad.roots[0]]
        offset = bad.fields(root.index)['children'].offset
        struct.pack_into('<i', root.data, offset, len(bad.blocks))
        with self.assertRaises(ValueError):
            compact(bad)


if __name__ == '__main__':
    unittest.main()
