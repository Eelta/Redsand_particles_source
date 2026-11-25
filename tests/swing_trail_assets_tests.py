from pathlib import Path
import sys
import unittest
import struct
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from generate_swing_trail_assets import model,texture,ribbon_model,ribbon_profile,RIBBON_ROWS,RIBBON_SLOTS
from nif_schema import DirectionalNif
from generate_weapon_assets import NifDocument,particle_systems,particle_controllers,shader_texture,shader_texture_transform,decode_dds_top

class SwingAssets(unittest.TestCase):
    def test_ribbon_profile_has_a_bright_core_and_soft_symmetric_halo(self):
        data = (ROOT/'assets/Basic/Textures/RedsandSwingTrail/RibbonProfile.dds').read_bytes()
        self.assertEqual(data, ribbon_profile())
        size, rgba = decode_dds_top(data)
        alpha = rgba[3:size*4:4]
        self.assertEqual(alpha, alpha[::-1])
        self.assertEqual(alpha[0], 0)
        self.assertGreater(alpha[64], 250)
        self.assertGreater(alpha[101], alpha[116])
        self.assertGreater(alpha[116], 0)

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

    def test_ribbon_has_one_connected_strip_per_instance(self):
        data = (ROOT/'assets/Basic/Meshes/RedsandSwingTrail/Ribbon.nif').read_bytes()
        self.assertEqual(data, ribbon_model())
        doc = DirectionalNif.from_bytes(data)
        doc.validate()
        shapes = doc.indices('BSTriShape')
        self.assertEqual(len(shapes), RIBBON_SLOTS)
        for index in shapes:
            shape = doc.fields(index)
            self.assertEqual(shape['num_vertices'].value, RIBBON_ROWS * 3)
            material = doc.values(shape['shader'].value)
            self.assertEqual(material['base_color'][3], 1., 'material alpha must preserve per-vertex opacity')
            self.assertTrue(material['shader_flags_1'] & 8, 'the effect shader must use vertex alpha')
            self.assertTrue(material['shader_flags_2'] & 32, 'the geometry must use vertex colors')
            triangles = shape['triangles'].value
            self.assertEqual(len(triangles), (RIBBON_ROWS - 1) * 4)
            for row in range(RIBBON_ROWS - 1):
                first = row * 3
                self.assertEqual(triangles[row * 4:row * 4 + 4],
                                 ((first, first + 1, first + 3), (first + 3, first + 1, first + 4),
                                  (first + 1, first + 2, first + 4), (first + 4, first + 2, first + 5)))

if __name__=='__main__':unittest.main()
