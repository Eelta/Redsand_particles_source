from pathlib import Path
import argparse
import struct

from optimize_slash_assets import SlashNif, compact
from typed_nif import Field, Reader


NODES = {'NiNode', 'NiBillboardNode', 'BSFadeNode', 'BSOrderedNode', 'BSMasterParticleSystem'}
GEOMETRY = {'BSTriShape', 'NiTriShape', 'NiParticleSystem'}


class ImpactNif(SlashNif):
    def fields(self, index):
        block = self.blocks[index]
        kind = block.type
        if kind not in {
            'NiFloatExtraData', 'NiStringsExtraData', 'NiTextKeyExtraData', 'NiDefaultAVObjectPalette',
            'NiBlendFloatInterpolator', 'NiBlendBoolInterpolator', 'NiBlendPoint3Interpolator',
            'NiTriShape', 'NiTriShapeData',
        }:
            return super().fields(index)

        reader = Reader(block.data)
        fields = {}

        def get(name, fmt, reference=False):
            start = reader.offset
            value = reader.unpack(fmt)
            fields[name] = Field(start, reader.offset - start, fmt, value, reference)
            return value

        if kind == 'NiDefaultAVObjectPalette':
            get('scene', 'i', True)
            for i in range(get('count', 'I')):
                reader.take(reader.unpack('I'))
                get(f'object_{i}', 'i', True)
        elif kind == 'NiFloatExtraData':
            get('name', 'I')
            get('value', 'f')
        elif kind == 'NiStringsExtraData':
            get('name', 'I')
            for _ in range(reader.unpack('I')):
                reader.take(reader.unpack('I'))
        elif kind == 'NiTextKeyExtraData':
            get('name', 'I')
            reader.take(reader.unpack('I') * 8)
        elif kind.startswith('NiBlend'):
            flags, _, _ = reader.unpack('BBf')
            if not flags & 1:
                raise ValueError('Expected a manager-controlled blend interpolator')
            reader.take({'NiBlendFloatInterpolator': 4, 'NiBlendBoolInterpolator': 1,
                         'NiBlendPoint3Interpolator': 12}[kind])
        elif kind == 'NiTriShape':
            get('name', 'I')
            if reader.unpack('I') != 0:
                raise ValueError('Unexpected legacy geometry extra data')
            get('controller', 'i', True)
            get('flags', 'I')
            reader.take(52)
            get('collision', 'i', True)
            get('data', 'i', True)
            get('skin', 'i', True)
            if reader.unpack('I') != 0:
                raise ValueError('Unexpected legacy geometry materials')
            reader.take(5)
            get('shader', 'i', True)
            get('alpha', 'i', True)
        else:
            _, vertices, _, _, positions = reader.unpack('iHBBB')
            if positions not in (0, 1):
                raise ValueError('Unexpected legacy vertex storage')
            reader.take(12 * vertices * positions)
            flags, _, normals = reader.unpack('HIB')
            reader.take(12 * vertices * normals * (3 if flags & 4096 else 1))
            reader.take(16)
            colors = reader.unpack('B')
            reader.take(16 * vertices * colors + 8 * vertices * (flags & 1) * positions)
            reader.take(2)
            get('additional_data', 'i', True)
            triangles, points, present = reader.unpack('HIB')
            if points != triangles * 3 or present not in (0, 1):
                raise ValueError('Unexpected legacy triangle storage')
            reader.take(6 * triangles * present)
            for _ in range(reader.unpack('H')):
                reader.take(reader.unpack('H') * 2)
        reader.finish()
        return fields


def node_name(doc, index):
    return doc.string(struct.unpack_from('<I', doc.blocks[index].data)[0])


def rewrite_refs(doc, index, count, array, values):
    fields = doc.fields(index)
    size = fields[count]
    refs = fields[array]
    doc.blocks[index].data[size.offset:refs.offset + refs.size] = struct.pack(
        '<' + size.fmt + 'i' * len(values), len(values), *values)


def graph_descendants(doc, roots):
    seen, pending = set(), list(roots)
    while pending:
        i = pending.pop()
        if i < 0 or i in seen:
            continue
        seen.add(i)
        if doc.blocks[i].type in NODES:
            pending.extend(doc.values(i)['children'])
    return seen


def strip_layers(doc, *, cut_only=False, companion=False):
    scene = graph_descendants(doc, doc.roots)
    if cut_only:
        names = {'RSRGBRoot_v1', 'RedsandImpactScale', 'RSRGBCutLength_v1', 'White hot blade contact'}
        kept = {i for i in scene if node_name(doc, i) in names}
        if {node_name(doc, i) for i in kept} != names or len(kept) != len(names):
            raise ValueError('Unexpected cut scene hierarchy')
        removed = scene - kept
    else:
        names = {'Fine contact corona', 'Expanding fine corona', 'Delayed corona',
                 'RedsandWaveRim', 'RedsandWaveEcho', 'Torus01', 'pENBLight'}
        names |= {'Flash02', 'EnbParticleLight01:0'}
        if companion:
            names.add('pFireBall-Emitter')
        roots = set()
        for i in scene:
            kind = doc.blocks[i].type
            if node_name(doc, i) in names:
                roots.add(i)
            if kind in GEOMETRY:
                shader = doc.values(i)['shader']
                if shader >= 0 and doc.blocks[shader].type == 'BSEffectShaderProperty':
                    texture = doc.values(shader)['source_texture'].lower()
                    if texture.endswith(('coronarays.dds', 'contactcorona.dds', 'ringsflare.dds',
                                         'ringsflare_alt.dds', 'rippleparticle.dds', 'fxbrightfash01.dds')):
                        roots.add(i)
        removed = graph_descendants(doc, roots)
    if not removed or removed.intersection(doc.roots):
        raise ValueError('Expected a scene with removable layers and a retained root')

    remaining_names = {node_name(doc, i) for i in scene - removed}
    discarded_properties = set()
    for i in removed:
        if doc.blocks[i].type in GEOMETRY:
            values = doc.values(i)
            discarded_properties.update({values['shader'], values['alpha']} - {-1})
    for i in scene - removed:
        kind = doc.blocks[i].type
        if kind in GEOMETRY:
            values = doc.values(i)
            discarded_properties.difference_update((values['shader'], values['alpha']))
        if kind in NODES:
            values = doc.values(i)
            children = tuple(j for j in values['children'] if j not in removed)
            if children != values['children']:
                rewrite_refs(doc, i, 'num_children', 'children', children)
            if kind == 'BSMasterParticleSystem':
                systems = tuple(j for j in values['particle_systems'] if j not in removed)
                if systems != values['particle_systems']:
                    rewrite_refs(doc, i, 'num_particle_systems', 'particle_systems', systems)

    for i in doc.indices('NiControllerSequence'):
        fields = doc.fields(i)
        count = fields['num_controlled_blocks'].value
        first = fields['controlled_0_interpolator'].offset if count else fields['weight'].offset
        last = fields['weight'].offset
        original = doc.blocks[i].data
        entries = []
        for j in range(count):
            prefix = f'controlled_{j}_'
            controller = fields[prefix + 'controller'].value
            target = doc.values(controller).get('target', -1) if controller >= 0 else -1
            name = doc.string(fields[prefix + 'node_name'].value)
            if ((name and name not in remaining_names) or
                    target in removed or target in discarded_properties):
                continue
            start = fields[prefix + 'interpolator'].offset
            end = fields[prefix + 'interpolator_id'].offset + 4
            entries.append(original[start:end])
        original[first:last] = b''.join(entries)
        struct.pack_into('<I', original, fields['num_controlled_blocks'].offset, len(entries))

    for i in doc.indices('NiMultiTargetTransformController'):
        targets = doc.values(i)['extra_targets']
        values = tuple(j for j in targets if j not in removed)
        if values != targets:
            rewrite_refs(doc, i, 'num_extra_targets', 'extra_targets', values)
    for i in doc.indices('NiDefaultAVObjectPalette'):
        original = doc.blocks[i].data
        reader = Reader(original)
        scene_index, count = reader.unpack('iI')
        entries = []
        for _ in range(count):
            offset = reader.offset
            name = bytes(reader.take(reader.unpack('I'))).decode('utf-8')
            if reader.unpack('i') not in removed and name in remaining_names:
                entries.append(original[offset:reader.offset])
        reader.finish()
        doc.blocks[i].data = bytearray(struct.pack('<iI', scene_index, len(entries)) + b''.join(entries))

    result, kept = compact(doc, reachable_only=True)
    if removed.intersection(kept) or discarded_properties.intersection(kept):
        raise ValueError('Removed geometry or material still has a live reference')
    result.type_names = list(dict.fromkeys(block.type for block in result.blocks))
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--cut-only', action='store_true')
    parser.add_argument('--companion', action='store_true')
    args = parser.parse_args()
    doc = ImpactNif.read(args.source)
    result = strip_layers(doc, cut_only=args.cut_only, companion=args.companion)
    result.write(args.output)
    print(f'{args.source.name}: {len(doc.blocks)} -> {len(result.blocks)} blocks')


if __name__ == '__main__':
    main()
