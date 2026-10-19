import argparse
import hashlib
import json
import struct
from pathlib import Path

from nif_schema import DirectionalNif
from typed_nif import Block


OLD_TEXTURE = r'textures\effects\Scholar Keriak\SKK_HitStar.dds'
NEW_TEXTURE = r'textures\StellarImpact\StarRing.dds'
MODEL_PATH = Path('Meshes/StellarImpact/Block/StarRing.nif')
TEXTURE_PATH = Path('Textures/StellarImpact/StarRing.dds')
ROOT_TAG = 'RSRGBRoot_v1'
SHADER_TAG = 'RSRGBv1_5'
GLOW_TAG = 'RSOriginalGlow_v1'
SEQUENCE_TAG = 'RSBlockSequence_v1'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def migrate(data):
    model = DirectionalNif.from_bytes(data)
    root = model.roots[0]
    if model.blocks[root].type != 'BSFadeNode':
        raise ValueError('Expected the original BSFadeNode container')
    original_root_fields = model.values(root)
    model.blocks[root].type = 'NiNode'
    if model.values(root) != original_root_fields:
        raise ValueError('Root container layouts differ')
    model.strings[model.values(root)['name']] = ROOT_TAG
    shader_name = len(model.strings)
    model.strings.extend((SHADER_TAG, GLOW_TAG, SEQUENCE_TAG))
    values = model.values(root)
    extra = len(model.blocks)
    model.blocks.append(Block(extra, 'NiFloatExtraData', bytearray(struct.pack('<If', shader_name + 2, 0.))))
    references = (*values['extra_data'], extra)
    prefix = struct.pack('<II', values['name'], len(references)) + struct.pack('<' + 'i' * len(references), *references)
    model.blocks[root].data[:8 + 4 * len(values['extra_data'])] = prefix
    shaders = model.indices('BSEffectShaderProperty')
    if model.replace_embedded_paths({OLD_TEXTURE: NEW_TEXTURE}) != 1:
        raise ValueError('Expected one ScholarKeriak texture reference')
    for index in shaders:
        values = model.values(index)
        if values['num_extra_data'] != 0:
            raise ValueError('Unexpected existing shader metadata')
        extra = len(model.blocks)
        model.blocks.append(Block(extra, 'NiFloatExtraData', bytearray(struct.pack('<If', shader_name + 1, 1.))))
        model.blocks[index].data[:8] = struct.pack('<IIi', shader_name, 1, extra)
    model.validate()
    return model.to_bytes()


def verify(original, converted, texture, output_root):
    before = DirectionalNif.from_bytes(original)
    after = DirectionalNif.from_bytes(converted)
    shaders = before.indices('BSEffectShaderProperty')
    if len(after.blocks) != len(before.blocks) + len(shaders) + 1 or after.roots != before.roots:
        raise ValueError('Unexpected model topology change')
    changed = []
    for index, block in enumerate(before.blocks):
        candidate = after.blocks[index]
        if candidate.type != block.type and not (index == before.roots[0] and block.type == 'BSFadeNode' and candidate.type == 'NiNode'):
            raise ValueError('Original block type changed')
        if index == before.roots[0]:
            old_fields = before.values(index)
            new_fields = after.values(index)
            fields = [key for key in old_fields if old_fields[key] != new_fields[key]]
            if set(fields) != {'num_extra_data', 'extra_data'} or new_fields['extra_data'][:-1] != old_fields['extra_data']:
                raise ValueError('Original root metadata changed')
            marker = after.blocks[new_fields['extra_data'][-1]]
            marker_name, marker_value = struct.unpack('<If', marker.data)
            if marker.type != 'NiFloatExtraData' or after.string(marker_name) != SEQUENCE_TAG or marker_value != 0.:
                raise ValueError('Missing one-shot sequence metadata')
            changed.append({'block': index, 'fields': fields})
            continue
        if index not in shaders:
            if candidate.data != block.data:
                raise ValueError(f'Original non-shader block changed: {index}')
            continue
        old_fields = before.values(index)
        new_fields = after.values(index)
        fields = [key for key in old_fields if old_fields[key] != new_fields[key]]
        if set(fields) - {'name', 'num_extra_data', 'extra_data', 'source_texture'}:
            raise ValueError(f'Material or controller values changed: {index}')
        if after.string(new_fields['name']) != SHADER_TAG or len(new_fields['extra_data']) != 1:
            raise ValueError('Missing block color metadata')
        marker = after.blocks[new_fields['extra_data'][0]]
        marker_name, marker_value = struct.unpack('<If', marker.data)
        if marker.type != 'NiFloatExtraData' or after.string(marker_name) != GLOW_TAG or marker_value != 1.:
            raise ValueError('Missing original glow metadata')
        changed.append({'block': index, 'fields': fields})
    if after.string(after.values(after.roots[0])['name']) != ROOT_TAG:
        raise ValueError('Missing root size metadata')
    paths = after.texture_paths()
    if OLD_TEXTURE in paths or NEW_TEXTURE not in paths:
        raise ValueError('External ScholarKeriak texture path remains')
    if (output_root / TEXTURE_PATH).read_bytes() != texture:
        raise ValueError('Original DDS bytes changed')
    if (output_root / MODEL_PATH).read_bytes() != converted:
        raise ValueError('Generated model differs from installed asset')
    return {'original_blocks': len(before.blocks), 'blocks': len(after.blocks),
            'original_root_type': before.blocks[before.roots[0]].type,
            'root_type': after.blocks[after.roots[0]].type,
            'changed_original_blocks': changed, 'original_geometry_controllers_unchanged': True,
            'original_material_values_unchanged': True, 'dds_unchanged': True,
            'model_sha256': sha(converted), 'texture_sha256': sha(texture),
            'textures': paths, 'validation': after.validate()}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-model', type=Path, required=True)
    parser.add_argument('--source-texture', type=Path, required=True)
    parser.add_argument('--output-root', type=Path, required=True)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    original = args.source_model.read_bytes()
    texture = args.source_texture.read_bytes()
    converted = migrate(original)
    if not args.check:
        for relative, data in ((MODEL_PATH, converted), (TEXTURE_PATH, texture)):
            destination = args.output_root / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
    print(json.dumps(verify(original, converted, texture, args.output_root), indent=2))


if __name__ == '__main__':
    main()
