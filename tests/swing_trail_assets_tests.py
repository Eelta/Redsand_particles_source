from pathlib import Path
import sys
import unittest
import struct
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from generate_swing_trail_assets import model,texture
from generate_weapon_assets import NifDocument,particle_systems,particle_controllers,shader_texture,shader_texture_transform,decode_dds_top

class SwingAssets(unittest.TestCase):
    def test_reproducible_private_mesh_and_analytic_texture(self):
        data=(ROOT/'assets/Basic/Meshes/RedsandSwingTrail/Arc.nif').read_bytes()
        self.assertEqual(data,model())
        doc=NifDocument.parse(data)
        self.assertEqual(doc.serialize(),data)
        system,=particle_systems(doc)
        flags1,flags2,_,_=shader_texture_transform(doc,system.shader)
        self.assertTrue(flags1 & 8, 'renderer must honor per-particle vertex alpha for smooth fading')
        self.assertTrue(flags2 & 32, 'renderer must honor per-particle vertex colors')
        birth=particle_controllers(doc,system)[0]
        self.assertEqual(struct.unpack_from('<f',doc.blocks[birth.interpolator])[0],0.)
        self.assertEqual(shader_texture(doc,system.shader)[0],r'textures\RedsandSwingTrail\Point.dds')
        dds=(ROOT/'assets/Basic/Textures/RedsandSwingTrail/Point.dds').read_bytes()
        self.assertEqual(dds,texture())
        size,rgba=decode_dds_top(dds)
        self.assertEqual(size,128)
        self.assertGreater(rgba[(64*size+64)*4+3],245)
        self.assertEqual(rgba[3],0)
        self.assertLess(rgba[(64*size+4)*4+3],2)
        self.assertGreater(rgba[(64*size+40)*4+3],180, 'point core became too narrow for small sprites')

if __name__=='__main__':unittest.main()
