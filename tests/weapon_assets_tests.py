from __future__ import annotations

import hashlib
from pathlib import Path
import struct
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import generate_weapon_assets as assets

ORIGINAL_TEMPLATE_SHA256 = {
    "Blue.nif": "4a5c28119015bca56c1bf1fdd740f182a38088631bd968a593c3df85a9af245d",
    "Bound.nif": "0487bb84fccc7620205903fad7114c4ec676898aade549fa2dadf13fb2e9b653",
    "DefaultYellow.nif": "34f340de3beafcf2d20c1c373d3d80c39c943c3ce949f3b1482008296bc3b737",
    "Fire.nif": "2cb0bac75ea7dcdbb167d834ba8a5117623aa39072100a6587234431b5c14fc2",
    "Frost.nif": "00d56bb506b1595eafcb5fc51e05000c217fa9c3f87240e940bb4728eff6a5ac",
    "Green.nif": "feb5804e7304da8a1b489bb44b9d9b21e7d231e628387324d6a2c5c3f4159e52",
    "Purple.nif": "a2dc7acfbf266b221bf89b5d21eae9ad95ad391259718d01f9f9765ddee1acaa",
    "Red.nif": "a45dd10d13279a148d1af9d5d55addd24ae7e823aa8bae4c41c7c64a2398772f",
    "Shock.nif": "868293c59db8e5ae1fd4147c0afa421c10210dc92541ded37e76dc6a267d65ee",
    "Sun.nif": "b9deaa57ad8adbdf3f20d1514c93275623a6086b3bcc362e046b34e1470ac4bb",
}

class WeaponAssetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.sources = {path.name: path.read_bytes() for path in assets.BASE.glob("*.nif")}
        if len(cls.sources) != 10:
            raise AssertionError("Expected ten real base colour NIFs")

    def test_source_block_tables_round_trip_exactly(self):
        for name, data in self.sources.items():
            with self.subTest(name=name):
                doc = assets.NifDocument.parse(data)
                self.assertEqual(doc.serialize(), data)
                self.assertEqual(len(assets.particle_systems(doc)), 1)

    def test_all_ninety_variants_parse_and_only_owned_blocks_change(self):
        for profile in assets.PROFILES:
            for name, data in self.sources.items():
                with self.subTest(profile=profile.name, color=name):
                    original = assets.NifDocument.parse(data)
                    variant_data = (assets.BASE / "WeaponStyles" / profile.name / name).read_bytes()
                    variant = assets.NifDocument.parse(variant_data)
                    self.assertEqual(variant.serialize(), variant_data)
                    self.assertEqual(variant.types, original.types)
                    self.assertEqual(variant.strings, original.strings)
                    self.assertEqual(variant.prefix, original.prefix)
                    self.assertEqual(variant.middle, original.middle)
                    self.assertEqual(variant.footer, original.footer)

                    owned = set()
                    for system in assets.particle_systems(original):
                        owned.add(system.shader)
                        owned.update(index for index in system.modifiers
                                     if original.types[index] in ("NiPSysMeshEmitter", "NiPSysGravityModifier"))
                        owned.update(controller.interpolator for controller in assets.particle_controllers(original, system)
                                     if original.types[controller.index] in (name for name, _ in assets.MOTION_CONTROLLERS))
                    for index, (before, after) in enumerate(zip(original.blocks, variant.blocks)):
                        if index not in owned:
                            self.assertEqual(after, before, f"Unrelated block changed: {index} {original.types[index]}")
                    self.assertEqual(assets.particle_systems(variant), assets.particle_systems(original))

    def test_base_templates_retain_every_original_byte_except_the_documented_changes(self):
        for name, data in self.sources.items():
            with self.subTest(name=name):
                doc = assets.NifDocument.parse(data)
                self.assertEqual(len(doc.blocks), 108)
                self.assertEqual(doc.types[104:], ("NiPSysEmitterSpeedCtlr", "NiFloatInterpolator",
                                                  "NiPSysEmitterInitialRadiusCtlr", "NiFloatInterpolator"))
                birth = bytearray(doc.blocks[86])
                self.assertEqual(struct.unpack_from("<i", birth)[0], 104)
                struct.pack_into("<i", birth, 0, 87)
                doc.blocks[86] = bytes(birth)
                self.assertEqual(struct.unpack_from("<i", doc.blocks[87])[0], -1)
                emitter = bytearray(doc.blocks[96])
                self.assertEqual(struct.unpack_from("<f", emitter, 57)[0], 2.0)
                struct.pack_into("<f", emitter, 57, 3.2)
                doc.blocks[96] = bytes(emitter)
                doc.set_block_types(doc.types[:104], prune_unused=True)
                doc.blocks = doc.blocks[:104]
                restored = doc.serialize()
                assets.NifDocument.parse(restored)
                self.assertEqual(hashlib.sha256(restored).hexdigest(), ORIGINAL_TEMPLATE_SHA256[name])

                emitter = bytearray(doc.blocks[96])
                struct.pack_into("<f", emitter, 57, 2.0)
                doc.blocks[96] = bytes(emitter)
                assets.ensure_motion_controllers(doc)
                self.assertEqual(doc.serialize(), data)

    def test_motion_controllers_reference_the_real_emitter_and_match_category_bases(self):
        paths = list(assets.BASE.glob("*.nif")) + list((assets.BASE / "WeaponStyles").glob("*/*.nif"))
        for path in paths:
            with self.subTest(path=str(path.relative_to(assets.BASE))):
                doc = assets.NifDocument.parse(path.read_bytes())
                for system in assets.particle_systems(doc):
                    chain = assets.particle_controllers(doc, system)
                    self.assertEqual(tuple(doc.types[c.index] for c in chain),
                                     ("NiPSysEmitterCtlr", "NiPSysEmitterSpeedCtlr",
                                      "NiPSysEmitterInitialRadiusCtlr", "NiPSysUpdateCtlr"))
                    birth = chain[0]
                    emitter = next(index for index in system.modifiers if doc.types[index] == "NiPSysMeshEmitter")
                    modifier_name = struct.unpack_from("<I", doc.blocks[emitter])[0]
                    fields = assets.emitter_fields(doc, emitter, system.index)
                    self.assertEqual(birth.modifier_name, modifier_name)
                    self.assertEqual(len({c.interpolator for c in chain if c.interpolator != -1}), 3)
                    for (type_name, key), controller in zip(assets.MOTION_CONTROLLERS, chain[1:3]):
                        self.assertEqual(doc.types[controller.index], type_name)
                        self.assertEqual(controller.flags, birth.flags)
                        self.assertTrue(controller.flags & 0x08)
                        self.assertEqual(controller.timing, birth.timing)
                        self.assertEqual(controller.target, system.index)
                        self.assertEqual(controller.modifier_name, modifier_name)
                        value, key_data = struct.unpack("<fi", doc.blocks[controller.interpolator])
                        self.assertEqual(key_data, -1)
                        self.assertEqual(value, fields[key][1])
                    self.assertGreater(fields["radius"][1] * 0.25 - fields["radius_variation"][1], 0.0,
                                       "Minimum Size must not permit a negative randomized birth radius")
                before = doc.serialize()
                assets.ensure_motion_controllers(doc)
                self.assertEqual(doc.serialize(), before, "Controller generation must be idempotent")

    def test_particle_uvs_sample_the_complete_shape_without_atlas_or_palette(self):
        paths = list(assets.BASE.glob("*.nif")) + list((assets.BASE / "WeaponStyles").glob("*/*.nif"))
        for path in paths:
            with self.subTest(path=str(path.relative_to(assets.BASE))):
                doc = assets.NifDocument.parse(path.read_bytes())
                for system in assets.particle_systems(doc):
                    sampling = assets.particle_texture_sampling(doc, system)
                    self.assertTrue(sampling.texture_indices)
                    self.assertEqual(sampling.subtextures, ((0.0, 1.0, 0.0, 1.0),))
                    self.assertEqual(sampling.aspect, 0.0)
                    self.assertEqual(sampling.aspect_flags, 0)
                    self.assertEqual(sampling.speed_aspect, (0.0, 0.0, 0.0))
                    self.assertNotIn("BSPSysSubTexModifier", (doc.types[index] for index in system.modifiers))
                    flags1, _flags2, uv_transform, greyscale = assets.shader_texture_transform(doc, system.shader)
                    self.assertEqual(uv_transform, (0.0, 0.0, 1.0, 1.0))
                    self.assertEqual(greyscale, "")
                    self.assertEqual(flags1 & 0x30, 0, "Palette flags can replace source RGB or alpha")

    def test_nine_categories_and_no_removed_staff_assets(self):
        expected = {profile.name for profile in assets.PROFILES}
        self.assertEqual(len(expected), 9)
        self.assertNotIn("Staff", expected)
        self.assertEqual({path.name for path in (assets.BASE / "WeaponStyles").iterdir() if path.is_dir()}, expected)
        self.assertEqual({path.stem for path in assets.TEXTURES.glob("*.dds")}, expected)
        self.assertEqual(len(list((assets.BASE / "WeaponStyles").glob("*/*.nif"))), 90)

    def test_texture_change_is_on_named_particle_material_and_preserves_colour(self):
        for profile in assets.PROFILES:
            for name, data in self.sources.items():
                with self.subTest(profile=profile.name, color=name):
                    before = assets.NifDocument.parse(data)
                    after = assets.NifDocument.parse((assets.BASE / "WeaponStyles" / profile.name / name).read_bytes())
                    for system in assets.particle_systems(before):
                        old_source, start, old_end = assets.shader_texture(before, system.shader)
                        source, new_start, new_end = assets.shader_texture(after, system.shader)
                        self.assertEqual(old_source, "textures\\effects\\ElementalDesert\\circle_05.dds")
                        self.assertEqual(source, f"textures\\effects\\ElementalDesert\\WeaponStyles\\{profile.name}.dds")
                        self.assertEqual(start, new_start)
                        self.assertEqual(after.blocks[system.shader][:start], before.blocks[system.shader][:start])
                        self.assertEqual(after.blocks[system.shader][new_end:], before.blocks[system.shader][old_end:])
                        texture_file = ROOT / "assets/Basic" / Path(source.replace("\\", "/"))
                        self.assertTrue(texture_file.is_file())

    def test_emitter_patch_changes_only_verified_floats_and_retains_initial_colour(self):
        for profile in assets.PROFILES:
            for name, data in self.sources.items():
                with self.subTest(profile=profile.name, color=name):
                    before = assets.NifDocument.parse(data)
                    after = assets.NifDocument.parse((assets.BASE / "WeaponStyles" / profile.name / name).read_bytes())
                    for system in assets.particle_systems(before):
                        for index in system.modifiers:
                            block_type = before.types[index]
                            if block_type == "NiPSysMeshEmitter":
                                fields = assets.emitter_fields(before, index, system.index)
                                checked_fields = assets.emitter_fields(after, index, system.index)
                                keys = ("speed", "speed_variation", "radius", "radius_variation", "lifespan", "lifespan_variation")
                                allowed = set()
                                for key in keys:
                                    self.assertAlmostEqual(checked_fields[key][1], getattr(profile, key), places=5)
                                    allowed.update(range(fields[key][0], fields[key][0] + 4))
                                self.assertLessEqual(checked_fields["lifespan"][1] + checked_fields["lifespan_variation"][1], 4.5)
                            elif block_type == "NiPSysGravityModifier":
                                fields = assets.gravity_fields(before, index, system.index)
                                checked_fields = assets.gravity_fields(after, index, system.index)
                                self.assertAlmostEqual(checked_fields["gravity"][1], profile.gravity, places=5)
                                allowed = set(range(fields["gravity"][0], fields["gravity"][0] + 4))
                            else:
                                continue
                            self.assertEqual(len(before.blocks[index]), len(after.blocks[index]))
                            for offset, (old, new) in enumerate(zip(before.blocks[index], after.blocks[index])):
                                if offset not in allowed:
                                    self.assertEqual(old, new, f"Non-parameter byte changed in {block_type}, offset {offset}")

    def test_dds_layout_full_mips_white_rgb_distinct_shapes_and_transparent_guards(self):
        digests = set()
        for profile in assets.PROFILES:
            with self.subTest(profile=profile.name):
                data = (assets.TEXTURES / f"{profile.name}.dds").read_bytes()
                self.assertEqual(data[:4], b"DDS ")
                header = struct.unpack("<31I", data[4:128])
                self.assertEqual(header[0], 124)
                self.assertEqual(header[1], 0x0002100F)
                self.assertEqual(header[2:5], (128, 128, 512))
                self.assertEqual(header[6], 8)
                self.assertEqual(header[18:26], (32, 0x41, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000))
                self.assertEqual(header[26], 0x00401008)
                offset, size = 128, 128
                for level in range(header[6]):
                    pixels = data[offset:offset + size * size * 4]
                    self.assertEqual(len(pixels), size * size * 4)

                    self.assertTrue(all(pixels[index] == 255 for index in range(len(pixels)) if index % 4 != 3))
                    border_indices = ({x for x in range(size)} |
                                      {(size - 1) * size + x for x in range(size)} |
                                      {y * size for y in range(size)} |
                                      {y * size + size - 1 for y in range(size)})
                    self.assertTrue(all(pixels[index * 4 + 3] == 0 for index in border_indices), f"Opaque edge in mip {level}")
                    if level == 0:
                        alpha = pixels[3::4]
                        self.assertGreater(max(alpha), 150)
                        self.assertGreater(sum(value > 20 for value in alpha), 100)
                        self.assertLess(sum(value > 20 for value in alpha), 7000)
                        digests.add(hashlib.sha256(alpha).digest())
                    offset += len(pixels)
                    size //= 2
                self.assertEqual(offset, len(data))
                decoded_size, decoded = assets.decode_dds_top(data)
                self.assertEqual(decoded_size, 128)
                self.assertEqual(decoded, assets.texture_pixels(profile))
        self.assertEqual(len(digests), len(assets.PROFILES))

    def test_generated_assets_are_reproducible(self):
        for profile in assets.PROFILES:
            with self.subTest(profile=profile.name):
                self.assertEqual((assets.TEXTURES / f"{profile.name}.dds").read_bytes(),
                                 assets.dds_bytes(assets.texture_pixels(profile)))
                for name, data in self.sources.items():
                    self.assertEqual((assets.BASE / "WeaponStyles" / profile.name / name).read_bytes(),
                                     assets.variant_bytes(data, profile))

    def test_malformed_sources_fail_closed(self):
        data = self.sources["DefaultYellow.nif"]
        with self.assertRaises(assets.FormatError):
            assets.NifDocument.parse(data[:-1])
        with self.assertRaises(assets.FormatError):
            assets.NifDocument.parse(data + b"unexpected")
        with self.assertRaises(assets.FormatError):
            assets.NifDocument.parse(data.replace(b"20.2.0.7", b"20.2.0.8", 1))
        doc = assets.NifDocument.parse(data)
        system = assets.particle_systems(doc)[0]

        doc.blocks[system.shader] = doc.blocks[system.shader].replace(b"circle_05.dds", b"wrong__05.dds")
        with self.assertRaises(assets.FormatError):
            assets.variant_bytes(doc.serialize(), assets.PROFILES[0])
        doc = assets.NifDocument.parse(data)
        emitter = next(index for index in system.modifiers if doc.types[index] == "NiPSysMeshEmitter")
        doc.blocks[emitter] += b"extra-layout-byte"
        with self.assertRaises(assets.FormatError):
            assets.variant_bytes(doc.serialize(), assets.PROFILES[0])
        doc = assets.NifDocument.parse(data)
        speed = bytearray(doc.blocks[104])
        struct.pack_into("<i", speed, 0, 86)
        doc.blocks[104] = bytes(speed)
        with self.assertRaises(assets.FormatError):
            assets.ensure_motion_controllers(doc)
        doc = assets.NifDocument.parse(data)
        speed = bytearray(doc.blocks[104])
        struct.pack_into("<i", speed, 0, -1)
        doc.blocks[104] = bytes(speed)
        with self.assertRaises(assets.FormatError):
            assets.ensure_motion_controllers(doc)

    def test_preview_uses_actual_decoded_dds_and_is_explicitly_not_a_game_screenshot(self):
        preview = ROOT / "output/weapon-particle-styles.png"
        data = preview.read_bytes()
        self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
        self.assertEqual(struct.unpack(">II", data[16:24]), (750, 710))
        self.assertIn(b"Not a game screenshot", data)

        with mock.patch.object(assets, "shape_alpha", side_effect=AssertionError("Preview redrew an idealized shape")):
            self.assertEqual(assets.preview_bytes(), data)

if __name__ == "__main__":
    unittest.main(verbosity=2)
