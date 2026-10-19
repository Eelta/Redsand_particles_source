
from pathlib import Path
import argparse
import copy
import hashlib
import json
import math
import struct
from nif_schema import CandidateNif
from typed_nif import Field, Reader


class SlashNif(CandidateNif):
    def fields(self, index):
        kind = self.blocks[index].type
        if kind not in {'BSXFlags', 'NiStringExtraData'}:
            return super().fields(index)
        reader = Reader(self.blocks[index].data)
        result = {}
        for name in ('name', 'value'):
            offset = reader.offset
            result[name] = Field(offset, 4, 'I', reader.unpack('I'))
        reader.finish()
        return result


def references(fields):
    for field in fields.values():
        if field.reference:
            yield from field.value if isinstance(field.value, tuple) else (field.value,)


def closure(doc):

    fields = {block.index: doc.fields(block.index) for block in doc.blocks}
    for parsed in fields.values():
        if any(ref < -1 or ref >= len(doc.blocks) for ref in references(parsed)):
            raise ValueError('Invalid block reference')
    seen, pending = set(), list(doc.roots)
    while pending:
        index = pending.pop()
        if index == -1 or index in seen:
            continue
        seen.add(index)
        pending.extend(references(fields[index]))
    return sorted(seen), fields


def compact(doc):
    kept, fields = closure(doc)
    remap = {old: new for new, old in enumerate(kept)}
    result = copy.deepcopy(doc)
    result.blocks = [result.blocks[index] for index in kept]
    for new, (old, block) in enumerate(zip(kept, result.blocks)):
        block.index = new
        for field in fields[old].values():
            if field.reference:
                values = field.value if isinstance(field.value, tuple) else (field.value,)
                mapped = [remap[value] if value != -1 else -1 for value in values]
                if mapped:
                    struct.pack_into('<' + 'i' * len(mapped), block.data, field.offset, *mapped)
    result.roots = [remap[root] for root in doc.roots]
    result.validate()
    assert len(closure(result)[0]) == len(result.blocks)
    return result, kept


SECONDARY = {'pSparkSlashAlot', 'pSparkSlashout', 'pDistanceSpark', 'pSpray'}


def optimize(doc):
    if len(doc.blocks) != 633:
        raise ValueError('Expected the unoptimized 633-block source; refusing to thin it twice')
    if doc.string(doc.fields(doc.roots[0])['name'].value) != 'RSRGBRoot_v1':
        raise ValueError('Expected an owned slash root')
    result = copy.deepcopy(doc)
    changed, protected, born_before, born_after = [], [], 0., 0.
    for index in doc.indices('NiParticleSystem'):
        fields = doc.fields(index)
        name = doc.string(fields['name'].value)
        factor = .5 if name in SECONDARY else 1.
        controller = fields['controller'].value
        visited = set()
        rate_data = None
        while controller != -1:
            if controller in visited:
                raise ValueError('Cyclic controller chain')
            visited.add(controller)
            control = doc.fields(controller)
            if doc.blocks[controller].type == 'NiPSysEmitterCtlr':
                if rate_data is not None:
                    raise ValueError('Multiple birth controllers')
                rate_data = doc.fields(control['interpolator'].value)['data'].value
            controller = control['next_controller'].value
        if rate_data is None:
            raise ValueError('Missing birth controller')
        rate = doc.fields(rate_data)
        if rate['interpolation'].value != 1:
            raise ValueError('Expected linear birth curve')
        keys = rate['keys'].value
        births = sum((b[0] - a[0]) * (a[1] + b[1]) * .5 for a, b in zip(keys, keys[1:])) * 100.
        born_before += births
        born_after += births * factor
        if factor == 1.:
            protected.append(name)
            continue
        offset = rate['keys'].offset
        for key, (time, value) in enumerate(keys):
            struct.pack_into('<f', result.blocks[rate_data].data, offset + key * 8 + 4, value * factor)
        capacity_index = fields['data'].value
        capacity = doc.fields(capacity_index)['max_particles']


        maximum = math.ceil(capacity.value * factor)
        if maximum < math.ceil(births * factor) + 2:
            raise ValueError('Insufficient maximum-density headroom')
        struct.pack_into('<H', result.blocks[capacity_index].data, capacity.offset, maximum)
        changed.append({'name': name, 'curve': rate_data, 'data': capacity_index,
                        'old_capacity': capacity.value, 'new_capacity': maximum})
    if len(changed) != 6 or len(protected) != 6:
        raise ValueError('Unexpected slash system layout')
    result, kept = compact(result)
    return result, {'blocks_before': len(doc.blocks), 'blocks_after': len(result.blocks),
                    'kept_original_indices': kept, 'secondary_systems': changed,
                    'protected_systems': protected, 'births_at_max_before': born_before,
                    'births_at_max_after': born_after,
                    'birth_reduction_fraction': 1. - born_after / born_before}


def graph_signature(doc):
    normalized, _ = compact(doc)
    return hashlib.sha256(normalized.to_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description='Conservatively prune unreachable blocks and thin secondary slash streams.\n\nOnly exact owned GoldBladeSlash scenes are accepted. Every block must have a\nknown schema, including unreachable blocks; unknown data fails closed. Reference\nclosure includes controllers, modifiers, emitter objects and all extra data.\n')
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    results, staged = {}, []
    for path in sorted(args.assets.rglob('GoldBladeSlash.nif')):
        doc = SlashNif.read(path)
        optimized, result = optimize(doc)
        result.update(before_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                      after_graph_sha256=graph_signature(optimized),
                      bytes_before=path.stat().st_size, bytes_after=len(optimized.to_bytes()))
        results[path.relative_to(args.assets).as_posix()] = result
        staged.append((path, optimized))
    if len(results) != 65:
        raise ValueError(f'Expected all 65 preset models; found {len(results)}')
    if args.apply:
        for path, optimized in staged:
            optimized.write(path)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'models': len(results), 'applied': args.apply,
                      'removed_blocks_per_model': sorted({r['blocks_before']-r['blocks_after'] for r in results.values()}),
                      'birth_reduction': sorted({round(r['birth_reduction_fraction'], 6) for r in results.values()})}))


if __name__ == '__main__':
    main()
